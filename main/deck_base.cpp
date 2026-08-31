/*
 * Deck Base Application
 * Main application entry point for PocketSSH. Initializes hardware peripherals including
 * GPIO, display, touch input, and manages FreeRTOS tasks for keyboard input and trackball navigation.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"

#include "esp_lcd_touch_gt911.h"
#include "driver/gpio.h"
#if defined(TDECKPLUS_TARGET)
#include "hal/usb_serial_jtag_ll.h"
#endif

#include "utilities.h"
#include "c3_keyboard.hpp"
#include "ssh_terminal.hpp"

#include "lvgl.h"

// The supplied GIF is predecoded at build time. Frame changes are then just
// image-pointer swaps, avoiding the GIF decoder watchdog failure on ESP32-S3.
extern const lv_image_dsc_t pocketssh_splash_frames[16];

#if defined(BSP_LCD_DRAW_BUFF_SIZE)
#define DRAW_BUF_SIZE BSP_LCD_DRAW_BUFF_SIZE
#else
#define DRAW_BUF_SIZE (BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT)
#endif

static const char *TAG = "main";

static i2c_master_bus_handle_t i2c_handle;
static lv_obj_t *ssh_screen;
static SSHTerminal *ssh_terminal = NULL;

// Splash screen variables
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *splash_img = NULL;
static lv_timer_t *splash_timer = NULL;
static uint8_t splash_frame_index = 0;
static constexpr uint8_t SPLASH_FRAME_COUNT = 16;

static void dismiss_splash_screen_locked()
{
    if (!splash_screen) {
        return;
    }

    if (splash_timer) {
        lv_timer_delete(splash_timer);
        splash_timer = NULL;
    }

    ESP_LOGW(TAG, "splash dismiss: deleting overlay");
    lv_obj_delete(splash_screen);
    splash_screen = NULL;
    splash_img = NULL;

    if (ssh_screen) {
        lv_obj_invalidate(ssh_screen);
        lv_refr_now(NULL);
    }
}

// Dismiss splash screen from non-LVGL tasks such as the keyboard task.
void dismiss_splash_screen()
{
    if (!splash_screen) {
        return;
    }
    if (bsp_display_lock(1000)) {
        dismiss_splash_screen_locked();
        bsp_display_unlock();
    } else {
        ESP_LOGW(TAG, "splash dismiss skipped: display lock timeout");
    }
}

// Touch event handler for splash screen - skip on touch
void splash_touch_cb(lv_event_t * e)
{
    dismiss_splash_screen_locked();
}

// Keep the supplied animation visible while Wi-Fi initializes behind it.  A
// hardware key or touch can dismiss it earlier; otherwise it leaves only once
// the existing boot connection reports success.
void splash_timer_cb(lv_timer_t * timer)
{
    if (ssh_terminal != nullptr && ssh_terminal->is_wifi_connected()) {
        dismiss_splash_screen_locked();
        return;
    }
    splash_frame_index = static_cast<uint8_t>((splash_frame_index + 1) % SPLASH_FRAME_COUNT);
    if (splash_img != nullptr) {
        lv_image_set_src(splash_img, &pocketssh_splash_frames[splash_frame_index]);
    }
}

void show_splash_screen()
{
    // Create splash as a top-layer overlay. This avoids a boot-time screen swap
    // on T-Deck Plus, where SD/Wi-Fi work can otherwise leave the old frame visible.
    splash_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(splash_screen, lv_pct(100), lv_pct(100));
    lv_obj_align(splash_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(splash_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_screen, 0, 0);
    lv_obj_set_style_pad_all(splash_screen, 0, 0);
    
    // Add touch event to skip splash screen
    lv_obj_add_event_cb(splash_screen, splash_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(splash_screen, LV_OBJ_FLAG_CLICKABLE);
    
    splash_img = lv_image_create(splash_screen);
    splash_frame_index = 0;
    lv_image_set_src(splash_img, &pocketssh_splash_frames[splash_frame_index]);
    lv_obj_align(splash_img, LV_ALIGN_CENTER, 0, 0);

    splash_timer = lv_timer_create(splash_timer_cb, 100, NULL);
}

void keypad_task(void *param)
{
    C3Keyboard keyboard(i2c_handle);
    if (keyboard.init() != ESP_OK)
    {
        ESP_LOGE("KEYPAD", "Failed to initialize keypad!");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        uint32_t key = keyboard.get_key();
        if (key)
        {
            ESP_LOGI("KEYPAD", "Key Pressed: %c", (unsigned char)key);

            // If splash screen is active, dismiss it on any key press
            if (splash_screen) {
                dismiss_splash_screen();
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;  // Skip processing this key
            }

            // Terminal text/update helpers take the display lock when they touch LVGL.
            // Avoid holding it across long commands such as SD config reads and Wi-Fi connect.
            if (ssh_terminal && ssh_screen) {
                // Do not log the character itself: serial logs must not expose
                // remote passwords. This marker is only a routing diagnostic.
                ESP_LOGW("KEYPAD", "keypad routed to terminal (remote=%d)",
                         ssh_terminal->is_connected() ? 1 : 0);
                ssh_terminal->handle_key_input((char)key);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Shorter delay for better responsiveness
    }
}

void trackball_task(void *param)
{
    static bool last_up = true;
    static bool last_down = true;
    static bool last_left = true;
    static bool last_right = true;
    static bool last_press = true;
    static uint32_t press_start_time = 0;
    const uint32_t LONG_PRESS_MS = 1000; // 1 second for long press
    
    while (1) {
        bool up = gpio_get_level(BOARD_TBOX_G01);
        bool down = gpio_get_level(BOARD_TBOX_G03);
        bool left = gpio_get_level(BOARD_TBOX_G02);
        bool right = gpio_get_level(BOARD_TBOX_G04);
        bool press = gpio_get_level(BOARD_BOOT_PIN);
        
        // Detect falling edge (button press)
        if (!up && last_up) {
            if (ssh_terminal) {
                ssh_terminal->navigate_history(1); // Older command
            }
        }
        if (!down && last_down) {
            if (ssh_terminal) {
                ssh_terminal->navigate_history(-1); // Newer command
            }
        }
        if (!left && last_left) {
            if (ssh_terminal) {
                ssh_terminal->move_cursor_left();
            }
        }
        if (!right && last_right) {
            if (ssh_terminal) {
                ssh_terminal->move_cursor_right();
            }
        }
        
        // Trackball press button handling
        if (!press && last_press) {
            // Button just pressed - record start time
            press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        } else if (press && !last_press) {
            // Button released - check duration
            uint32_t press_duration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - press_start_time;
            
            if (ssh_terminal) {
                if (press_duration >= LONG_PRESS_MS) {
                    // Long press: open/close special keys panel
                    ESP_LOGI("TRACKBALL", "Long press detected (%lu ms) - toggling special keys", press_duration);
                    ssh_terminal->toggle_special_keys_panel();
                } else {
                    // Short press: Execute current input (like Enter key)
                    ESP_LOGI("TRACKBALL", "Short press detected (%lu ms) - executing current input", press_duration);
                    ssh_terminal->handle_key_input('\n');
                }
            }
        }
        
        last_up = up;
        last_down = down;
        last_left = left;
        last_right = right;
        last_press = press;
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t _bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    /* Initilize I2C */
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Initialize I2C Fail");
        return ret;
    }

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_V_RES,
        .y_max = BSP_LCD_H_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_16,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_LOGI("Touch", "Initialize LCD Touch: GT911");
    esp_lcd_panel_io_i2c_config_t tp_io_config = {};
    tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    tp_io_config.control_phase_bytes = 1;
    tp_io_config.dc_bit_offset = 0;
    tp_io_config.lcd_cmd_bits = 16;
    tp_io_config.flags.disable_control_phase = 1;
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;

    i2c_handle = bsp_i2c_get_handle();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle), "TOuch", "");
    return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
}

void device_init(void)
{
    /* Initialize power on pin */
    gpio_reset_pin(BOARD_POWERON);
    gpio_set_direction(BOARD_POWERON, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_POWERON, 1);

    /* Initialize GPIO for SD card CS */
    gpio_reset_pin(BOARD_SDCARD_CS);
    gpio_set_direction(BOARD_SDCARD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_SDCARD_CS, 1);

    /* Initialize radio CS pin */
    gpio_reset_pin(RADIO_CS_PIN);
    gpio_set_direction(RADIO_CS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RADIO_CS_PIN, 1);

    /* Initialize TFT CS pin */
    gpio_reset_pin(BOARD_TFT_CS);
    gpio_set_direction(BOARD_TFT_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_TFT_CS, 1);

    /* Configure MISO with pull-up for SD card (must be done before SPI bus init) */
    gpio_reset_pin(BOARD_SPI_MISO);
    gpio_set_direction(BOARD_SPI_MISO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_SPI_MISO, GPIO_PULLUP_ONLY);

    // Initialize trackball GPIOs for command history navigation
    gpio_reset_pin(BOARD_TBOX_G01);
    gpio_set_direction(BOARD_TBOX_G01, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_TBOX_G01, GPIO_PULLUP_ONLY);
    
    gpio_reset_pin(BOARD_TBOX_G03);
    gpio_set_direction(BOARD_TBOX_G03, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_TBOX_G03, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BOARD_TBOX_G02);
    gpio_set_direction(BOARD_TBOX_G02, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_TBOX_G02, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BOARD_TBOX_G04);
    gpio_set_direction(BOARD_TBOX_G04, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_TBOX_G04, GPIO_PULLUP_ONLY);
    
    // Initialize trackball press button (BOOT button on GPIO 0)
    gpio_reset_pin(BOARD_BOOT_PIN);
    gpio_set_direction(BOARD_BOOT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_BOOT_PIN, GPIO_PULLUP_ONLY);
}

// Forward declaration
void load_ssh_keys_from_sd(SSHTerminal* terminal);
#if defined(TDECKPLUS_TARGET)
void pocketssh_set_cached_wifi_config_text(const char *path, const char *text);
void pocketssh_set_cached_ssh_config_text(const char *path, const char *text);
void pocketssh_set_cached_known_hosts_text(const char *path, const char *text);
#endif

#if defined(TDECKPLUS_TARGET)
static bool read_text_file_for_cache(const char *path, size_t max_bytes, std::string *text)
{
    if (path == nullptr || text == nullptr) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size <= 0 || size > static_cast<long>(max_bytes)) {
        fclose(file);
        ESP_LOGW(TAG, "config cache: ignoring %s size=%ld", path, size);
        return false;
    }
    rewind(file);

    text->clear();
    text->resize(static_cast<size_t>(size));
    const size_t read_len = fread(text->data(), 1, text->size(), file);
    fclose(file);
    if (read_len != text->size()) {
        ESP_LOGW(TAG, "config cache: short read %s (%d/%d)",
                 path, static_cast<int>(read_len), static_cast<int>(text->size()));
        text->clear();
        return false;
    }
    return true;
}

static void cache_wifi_config_from_mounted_sd()
{
    constexpr size_t kMaxWifiConfigBytes = 16 * 1024;
    const char *exact_candidates[] = {
        "/sdcard/ssh_keys/wifi_config",
        "/sdcard/wifi_config",
    };

    auto try_cache_path = [](const char *path) -> bool {
        std::string text;
        if (!read_text_file_for_cache(path, kMaxWifiConfigBytes, &text)) {
            return false;
        }
        pocketssh_set_cached_wifi_config_text(path, text.c_str());
        return true;
    };

    for (const char *path : exact_candidates) {
        if (try_cache_path(path)) {
            return;
        }
    }

    const char *short_dirs[] = {"/sdcard/ssh_keys", "/sdcard"};
    const char *short_prefixes[] = {"WIFI_C~", "WIFI_CO~", "WIFIC~", "WIFICO~"};
    for (const char *dir : short_dirs) {
        for (const char *prefix : short_prefixes) {
            for (int index = 1; index <= 9; ++index) {
                char path[96];
                snprintf(path, sizeof(path), "%s/%s%d", dir, prefix, index);
                if (try_cache_path(path)) {
                    return;
                }
            }
        }
    }

    pocketssh_set_cached_wifi_config_text(nullptr, nullptr);
}

static void cache_ssh_config_from_mounted_sd()
{
    constexpr size_t kMaxSshConfigBytes = 32 * 1024;
    const char *exact_candidates[] = {
        "/sdcard/ssh_keys/ssh_config",
        "/sdcard/ssh_config",
    };

    auto try_cache_path = [](const char *path) -> bool {
        std::string text;
        if (!read_text_file_for_cache(path, kMaxSshConfigBytes, &text)) {
            return false;
        }
        pocketssh_set_cached_ssh_config_text(path, text.c_str());
        return true;
    };

    for (const char *path : exact_candidates) {
        if (try_cache_path(path)) {
            return;
        }
    }

    const char *short_dirs[] = {"/sdcard/ssh_keys", "/sdcard"};
    const char *short_prefixes[] = {"SSH_C~", "SSH_CO~", "SSHC~", "SSHCO~"};
    for (const char *dir : short_dirs) {
        for (const char *prefix : short_prefixes) {
            for (int index = 1; index <= 9; ++index) {
                char path[96];
                snprintf(path, sizeof(path), "%s/%s%d", dir, prefix, index);
                if (try_cache_path(path)) {
                    return;
                }
            }
        }
    }

    pocketssh_set_cached_ssh_config_text(nullptr, nullptr);
}

static void cache_known_hosts_from_mounted_sd()
{
    constexpr size_t kMaxKnownHostsBytes = 32 * 1024;
    constexpr const char *kPath = "/sdcard/ssh_keys/known_hosts";
    std::string text;
    if (read_text_file_for_cache(kPath, kMaxKnownHostsBytes, &text)) {
        pocketssh_set_cached_known_hosts_text(kPath, text.c_str());
    } else {
        // An absent store is an intentional, distinct first-use state.
        pocketssh_set_cached_known_hosts_text(nullptr, nullptr);
    }
}
#endif

static bool starts_with(const std::string &value, const char *prefix)
{
    const size_t len = strlen(prefix);
    return value.size() >= len && value.compare(0, len, prefix) == 0;
}

static std::string trim_ascii(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && isspace((unsigned char)value[begin])) {
        begin++;
    }
    size_t end = value.size();
    while (end > begin && isspace((unsigned char)value[end - 1])) {
        end--;
    }
    return value.substr(begin, end - begin);
}

static void inject_terminal_command(const std::string &line)
{
    if (!ssh_terminal || line.empty()) {
        return;
    }
    ssh_terminal->run_control_command(line);
}

void serial_control_task(void *)
{
#if defined(TDECKPLUS_TARGET)
    ESP_LOGW(TAG, "serial ctl: source=usb_serial_jtag_fifo");
#else
    ESP_LOGI(TAG, "serial ctl: source=stdin");
#endif

    const int stdin_fd = fileno(stdin);
    if (stdin_fd >= 0) {
        const int flags = fcntl(stdin_fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(stdin_fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    std::string line;
    line.reserve(256);
    char buf[96];

    while (true) {
        if (ssh_terminal && ssh_terminal->is_serial_rx_in_progress()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        ssize_t nread = -1;
#if defined(TDECKPLUS_TARGET)
        nread = static_cast<ssize_t>(
            usb_serial_jtag_ll_read_rxfifo(reinterpret_cast<uint8_t *>(buf), sizeof(buf)));
#else
        nread = stdin_fd >= 0 ? read(stdin_fd, buf, sizeof(buf)) : -1;
#endif
        if (nread <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        for (ssize_t i = 0; i < nread; ++i) {
            const char ch = buf[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                const std::string raw = line;
                line.clear();
                static constexpr const char *kCtlPrefix = "__pocketctl ";
                if (starts_with(raw, kCtlPrefix)) {
                    const std::string payload = trim_ascii(raw.substr(strlen(kCtlPrefix)));
                    if (payload == "ping") {
                        ESP_LOGW(TAG, "POCKETCTL pong");
                    } else if (payload == "serialrx" || starts_with(payload, "serialrx ")) {
                        ESP_LOGW(TAG, "POCKETCTL run: %s", payload.c_str());
                        inject_terminal_command(payload);
                    } else if (starts_with(payload, "cmd ")) {
                        const std::string cmd = trim_ascii(payload.substr(4));
                        ESP_LOGW(TAG, "POCKETCTL run cmd: %s", cmd.c_str());
                        inject_terminal_command(cmd);
                    } else if (!payload.empty()) {
                        ESP_LOGW(TAG, "POCKETCTL unknown command: %s", payload.c_str());
                    }
                }
                continue;
            }
            if (line.size() < 512) {
                line.push_back(ch);
            }
        }
    }
}

void boot_wifi_task(void *)
{
#if defined(TDECKPLUS_TARGET)
    // Bring Wi-Fi up behind the splash instead of waiting for it to disappear.
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGW(TAG, "wifi auto boot: starting from cached T-Deck Plus config");
#endif
    if (ssh_terminal) {
        ssh_terminal->try_boot_wifi_auto_connect();
    }
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize device GPIOs */
    device_init();

    /* Load SSH keys from SD card BEFORE LVGL initialization */
    // Note: Create a temporary terminal instance just for loading keys
    SSHTerminal* temp_terminal = new SSHTerminal();
    load_ssh_keys_from_sd(temp_terminal);

    /* Initialize display and LVGL */
    lv_display_t *disp = bsp_display_start();

    /* Set display brightness to 100% */
    bsp_display_backlight_on();

    /* Initialize touch */
    esp_lcd_touch_handle_t touch_handle = NULL;
    const bsp_touch_config_t bsp_touch_cfg = {};
    _bsp_touch_new(&bsp_touch_cfg, &touch_handle);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
        .scale = {.x = 0, .y = 0},
    };

    lvgl_port_add_touch(&touch_cfg);

    bsp_display_lock(0);

    // Use the terminal instance that already has loaded keys
    ssh_terminal = temp_terminal;
    ssh_screen = ssh_terminal->create_terminal_screen();
    lv_scr_load(ssh_screen);

    // Show splash animation as an overlay on top of the already-active terminal.
    show_splash_screen();
    
    // Display version and initial instructions
#ifdef POCKETSSH_VERSION
    ssh_terminal->append_text("PocketSSH v" POCKETSSH_VERSION "\n");
#else
    ssh_terminal->append_text("PocketSSH Terminal Ready\n");
#endif

    // Display loaded SSH keys
    auto loaded_keys = ssh_terminal->get_loaded_key_names();
    if (!loaded_keys.empty()) {
        ssh_terminal->append_text("\nLoaded SSH keys:\n");
        for (const auto& keyname : loaded_keys) {
            ssh_terminal->append_text("  - ");
            ssh_terminal->append_text(keyname.c_str());
            ssh_terminal->append_text("\n");
        }
        ssh_terminal->append_text("\n");
    } else {
        ssh_terminal->append_text("\nNo SSH keys found on SD card.\n");
        ssh_terminal->append_text("Place .pem files in /sdcard/ssh_keys/\n\n");
    }
    
    // Update status bar to show initial battery voltage
    ssh_terminal->update_status_bar();

    bsp_display_unlock();

    xTaskCreate(keypad_task, "keypad_task", 16384, NULL, 5, NULL);
    
    xTaskCreate(trackball_task, "trackball_task", 4096, NULL, 5, NULL);

    xTaskCreate(serial_control_task, "serial_ctl", 12288, NULL, 3, NULL);
    xTaskCreate(boot_wifi_task, "boot_wifi", 8192, NULL, 4, NULL);
}

/*
 * Load SSH private keys from SD card
 * Reads all .pem files from /sdcard/ssh_keys/ directory and loads them into memory
 * Must be called BEFORE LVGL initialization to avoid SD card access conflicts
 */
void load_ssh_keys_from_sd(SSHTerminal* terminal)
{
    if (!terminal) {
        ESP_LOGE(TAG, "Terminal pointer is NULL");
        return;
    }

    ESP_LOGI(TAG, "Starting SD card key loading...");

    // SD card mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_card_t *card = NULL;
    const char mount_point[] = "/sdcard";
    
    ESP_LOGI(TAG, "Mounting SD card...");
    
    // Configure SPI bus for SD card (T-Deck Plus uses SPI mode, not SDMMC)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SPI_MOSI,
        .miso_io_num = BOARD_SPI_MISO,
        .sclk_io_num = BOARD_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
#if defined(TDECKPLUS_TARGET)
    constexpr spi_host_device_t sd_host = SPI2_HOST;
    gpio_set_direction(BOARD_SDCARD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_SDCARD_CS, 1);
    gpio_set_direction(BOARD_TFT_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_TFT_CS, 1);
    gpio_set_direction(RADIO_CS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RADIO_CS_PIN, 1);
#else
    constexpr spi_host_device_t sd_host = SPI3_HOST;
#endif

    esp_err_t ret = spi_bus_initialize(sd_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus (%s)", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "SPI bus initialized");
    
    // Configure SPI host for SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = sd_host;
    host.max_freq_khz = 4000;
    
    // Configure SD card SPI device
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BOARD_SDCARD_CS;
    slot_config.host_id = sd_host;
    
    ESP_LOGI(TAG, "Attempting to mount SD card on SPI%d...", static_cast<int>(sd_host) + 1);
    
    // Mount SD card using SPI mode
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card (%s)", esp_err_to_name(ret));
        }
        return;
    }
    
    ESP_LOGI(TAG, "SD card mounted successfully");

#if defined(TDECKPLUS_TARGET)
    cache_wifi_config_from_mounted_sd();
    cache_ssh_config_from_mounted_sd();
    cache_known_hosts_from_mounted_sd();
#endif

    // Open the ssh_keys directory
    const char* keys_dir = "/sdcard/ssh_keys";
    ESP_LOGI(TAG, "Opening directory: %s", keys_dir);
    DIR* dir = opendir(keys_dir);
    
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open directory %s - creating it", keys_dir);
        mkdir(keys_dir, 0755);
        dir = opendir(keys_dir);
        if (!dir) {
            ESP_LOGE(TAG, "Still cannot open directory after creation");
            esp_vfs_fat_sdcard_unmount(mount_point, card);
            spi_bus_free(sd_host);
            return;
        }
    }

    ESP_LOGI(TAG, "Directory opened, scanning for .pem files...");

    // Read all .pem files from directory
    struct dirent* entry;
    int keys_loaded = 0;
    int files_found = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        files_found++;
        ESP_LOGI(TAG, "Found file: %s", entry->d_name);
        
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Check if file has .pem or .PEM extension (case-insensitive)
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5) {
            ESP_LOGD(TAG, "Skipping file (name too short): %s", entry->d_name);
            continue;
        }
        
        const char* ext = entry->d_name + name_len - 4;
        if (strcasecmp(ext, ".pem") != 0) {
            ESP_LOGI(TAG, "Skipping non-PEM file: %s", entry->d_name);
            continue;
        }
        
        ESP_LOGI(TAG, "Processing PEM file: %s", entry->d_name);
        
        // Build full file path
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", keys_dir, entry->d_name);
        
        // Open and read the key file
        FILE* f = fopen(filepath, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open key file: %s", filepath);
            continue;
        }
        
        // Get file size
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (file_size <= 0 || file_size > 16384) {
            ESP_LOGW(TAG, "Invalid key file size: %s (%ld bytes)", entry->d_name, file_size);
            fclose(f);
            continue;
        }
        
        // Allocate buffer and read key data
        char* key_data = (char*)malloc(file_size + 1);
        if (!key_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for key: %s", entry->d_name);
            fclose(f);
            continue;
        }
        
        size_t bytes_read = fread(key_data, 1, file_size, f);
        fclose(f);
        
        if (bytes_read != (size_t)file_size) {
            ESP_LOGE(TAG, "Failed to read complete key file: %s", entry->d_name);
            free(key_data);
            continue;
        }
        
        key_data[file_size] = '\0';  // Null terminate
        
        // Load key into terminal's memory
        terminal->load_key_from_memory(entry->d_name, key_data, file_size);
        free(key_data);
        keys_loaded++;
    }
    
    closedir(dir);
    
    ESP_LOGI(TAG, "Total files found: %d, Keys loaded: %d", files_found, keys_loaded);
    
    // Unmount SD card and deinitialize SPI bus
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    spi_bus_free(sd_host);
    ESP_LOGI(TAG, "SD card unmounted, %d SSH keys loaded", keys_loaded);
}
