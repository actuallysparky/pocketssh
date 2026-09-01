/*
 * T-Pager Base Runtime
 *
 * Contract:
 * - Dedicated TPAGER_TARGET entrypoint (separate from T-Deck deck_base.cpp).
 * - Reuse proven bring-up modules from TPAGER_DIAG for display/input/SD.
 * - Forward hardware keyboard/encoder events into the existing SSHTerminal flow.
 */

#include <cinttypes>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "nvs_flash.h"
#include "ssh_terminal.hpp"
#include "tpager_display.hpp"
#include "tpager_encoder.hpp"
#include "tpager_sd.hpp"
#include "tpager_tca8418.hpp"
#include "tpager_xl9555.hpp"

namespace {

constexpr const char *kTag = "tpager_base";

constexpr i2c_port_t kI2CPort = I2C_NUM_0;
constexpr gpio_num_t kI2CSda = GPIO_NUM_3;
constexpr gpio_num_t kI2CScl = GPIO_NUM_2;
constexpr uint32_t kI2CFreqHz = 400000;

constexpr uint8_t kTCA8418Addr = 0x34;
constexpr gpio_num_t kKeyboardIrq = GPIO_NUM_6;

constexpr gpio_num_t kEncoderA = GPIO_NUM_40;
constexpr gpio_num_t kEncoderB = GPIO_NUM_41;
constexpr gpio_num_t kEncoderCenter = GPIO_NUM_7;
constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr gpio_num_t kDisplayBacklight = GPIO_NUM_42;

constexpr const char *kKeysDir = "/sdcard/ssh_keys";
constexpr const char *kKeysDirAlt = "/sd/ssh_keys";
constexpr size_t kMaxKeySize = 16 * 1024;
// SSH setup walks Wi-Fi, SD configuration, libssh2, and the terminal layout.
// It must never share the tiny USB FIFO reader's stack.
constexpr uint32_t kSerialCommandStackBytes = 16 * 1024;

constexpr TickType_t ticks_from_ms(uint32_t ms)
{
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks == 0 ? 1 : ticks;
}

constexpr TickType_t kI2CTimeoutTicks = ticks_from_ms(20);

tpager::Xl9555 g_xl9555;
tpager::Tca8418 g_tca8418;
tpager::Tca8418State g_tca8418_state;
tpager::Encoder g_encoder;
tpager::DiagDisplay g_display;

SSHTerminal *g_terminal = nullptr;

TaskHandle_t g_runtime_task_handle = nullptr;
volatile uint32_t g_keyboard_irq_count = 0;

int32_t g_keyboard_events = 0;
int32_t g_keyboard_presses = 0;
int32_t g_keyboard_releases = 0;
int32_t g_encoder_net = 0;
int32_t g_encoder_transitions = 0;
bool g_shutdown_requested = false;
// -1 = terminal screen could not be created, 0 = handoff pending, 1 = active.
// Kept intentionally simple because it is read by a diagnostic task only.
volatile int g_terminal_ui_state = 0;
// -1 = keyboard unavailable, 0 = initialization pending, 1 = ready.
volatile int g_keyboard_state = 0;
// Boot progress is emitted through the USB peripheral directly so a blocked
// logging VFS or LVGL task cannot hide the startup boundary during Pager
// bring-up.  This temporary trace is intentionally numeric and contains no
// profile, key, or session information.
volatile int g_boot_stage = 0;
bool g_keyboard_prerequisites_ready = false;
bool g_alt_held = false;
bool g_caps_held = false;
bool g_encoder_center_held = false;
bool g_encoder_center_hold_fired = false;
TickType_t g_encoder_center_press_tick = 0;

extern "C" void tpager_request_shutdown(void);

void IRAM_ATTR keyboard_irq_isr(void *)
{
    g_keyboard_irq_count = g_keyboard_irq_count + 1;
    if (g_runtime_task_handle == nullptr) {
        return;
    }
    BaseType_t high_priority_wakeup = pdFALSE;
    vTaskNotifyGiveFromISR(g_runtime_task_handle, &high_priority_wakeup);
    if (high_priority_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void emit_usb_boot_trace()
{
    char line[48] = {};
    const int len = std::snprintf(line, sizeof(line), "PAGER_BOOT stage=%d\n", g_boot_stage);
    if (len <= 0 || !usb_serial_jtag_ll_txfifo_writable()) {
        return;
    }
    const size_t bytes = std::min<size_t>(static_cast<size_t>(len), sizeof(line) - 1);
    (void)usb_serial_jtag_ll_write_txfifo(reinterpret_cast<const uint8_t *>(line), bytes);
    usb_serial_jtag_ll_txfifo_flush();
}

void boot_trace_task(void *)
{
    while (g_boot_stage < 100) {
        emit_usb_boot_trace();
        vTaskDelay(ticks_from_ms(1000));
    }
    vTaskDelete(nullptr);
}

esp_err_t i2c_init()
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = kI2CSda;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = kI2CScl;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = kI2CFreqHz;

    ESP_RETURN_ON_ERROR(i2c_param_config(kI2CPort, &conf), kTag, "i2c_param_config failed");
    esp_err_t ret = i2c_driver_install(kI2CPort, conf.mode, 0, 0, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(kTag, "I2C already initialized");
        return ESP_OK;
    }
    return ret;
}

bool probe_tca8418()
{
    return tpager::tca8418_probe(g_tca8418) == ESP_OK;
}

bool keyboard_power_reset(uint8_t kb_power_pin)
{
    if (tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_KB_RESET, true) != ESP_OK ||
        tpager::xl9555_set_dir(g_xl9555, kb_power_pin, true) != ESP_OK) {
        return false;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, kb_power_pin, false));
    ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_KB_RESET, false));
    vTaskDelay(ticks_from_ms(30));
    ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, kb_power_pin, true));
    vTaskDelay(ticks_from_ms(30));
    ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_KB_RESET, true));
    vTaskDelay(ticks_from_ms(30));

    for (int i = 0; i < 5; ++i) {
        if (probe_tca8418()) {
            return true;
        }
        vTaskDelay(ticks_from_ms(20));
    }
    return false;
}

void append_terminal_text(const char *text)
{
    if (g_terminal == nullptr || text == nullptr) {
        return;
    }
    // Boot-order contract: SD preload can run before LVGL init. In that phase,
    // skip UI writes to avoid taking an uninitialized LVGL lock.
    if (!g_display.initialized) {
        return;
    }
    if (!lvgl_port_lock(25)) {
        return;
    }
    g_terminal->append_text(text);
    lvgl_port_unlock();
}

void handle_terminal_key(char key)
{
    if (g_terminal == nullptr) {
        return;
    }
    if (!lvgl_port_lock(25)) {
        return;
    }
    g_terminal->handle_key_input(key);
    lvgl_port_unlock();
}

std::string trim_ascii(const std::string &input)
{
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return input.substr(begin, end - begin);
}

bool starts_with_ascii(const std::string &value, const char *prefix)
{
    if (prefix == nullptr) {
        return false;
    }
    const size_t prefix_len = std::strlen(prefix);
    if (value.size() < prefix_len) {
        return false;
    }
    return value.compare(0, prefix_len, prefix) == 0;
}

struct SerialCommandRequest {
    std::string line;
};

void serial_command_task(void *arg)
{
    auto *request = static_cast<SerialCommandRequest *>(arg);
    if (request == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    std::string line = std::move(request->line);
    delete request;

    if (g_terminal != nullptr && !line.empty()) {
        // This is intentionally the same public entry point as automated
        // terminal control on the T-Deck. It dispatches a complete command
        // locally while disconnected and sends a complete line to an active
        // remote shell without holding the LVGL lock across network work.
        g_terminal->run_control_command(line);
    }

    const UBaseType_t remaining_words = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(kTag, "serial command complete; stack free=%u B",
             static_cast<unsigned>(remaining_words * sizeof(StackType_t)));
    vTaskDelete(nullptr);
}

void inject_terminal_command(const std::string &line)
{
    if (line.empty()) {
        return;
    }

    auto *request = new (std::nothrow) SerialCommandRequest{line};
    if (request == nullptr) {
        ESP_LOGW(kTag, "serial ctl: command allocation failed");
        return;
    }

    const BaseType_t task_ok = xTaskCreatePinnedToCore(serial_command_task,
                                                        "tpager_serial_cmd",
                                                        kSerialCommandStackBytes,
                                                        request,
                                                        4,
                                                        nullptr,
                                                        1);
    if (task_ok != pdPASS) {
        ESP_LOGW(kTag, "serial ctl: failed to start command worker");
        delete request;
    }
}

void handle_serial_control_line(const std::string &raw_line)
{
    static constexpr const char *kCtlPrefix = "__pocketctl ";
    if (!starts_with_ascii(raw_line, kCtlPrefix)) {
        return;
    }

    const std::string payload = trim_ascii(raw_line.substr(std::strlen(kCtlPrefix)));
    if (payload.empty()) {
        return;
    }

    if (payload == "ping") {
        ESP_LOGI(kTag, "POCKETCTL pong");
        return;
    }

    if (payload == "serialrx" || starts_with_ascii(payload, "serialrx ")) {
        ESP_LOGI(kTag, "POCKETCTL run: %s", payload.c_str());
        inject_terminal_command(payload);
        return;
    }

    if (starts_with_ascii(payload, "cmd ")) {
        const std::string cmd = trim_ascii(payload.substr(4));
        if (!cmd.empty()) {
            ESP_LOGI(kTag, "POCKETCTL run cmd: %s", cmd.c_str());
            inject_terminal_command(cmd);
        }
        return;
    }

    ESP_LOGW(kTag, "POCKETCTL unknown command: %s", payload.c_str());
}

bool to_terminal_char(const tpager::Tca8418Event &ev, char *out_key)
{
    if (out_key == nullptr || !ev.pressed) {
        return false;
    }

    switch (ev.key) {
    case tpager::Tca8418Key::Character:
    case tpager::Tca8418Key::Space:
        if (ev.ch != '\0') {
            *out_key = ev.ch;
            return true;
        }
        break;
    case tpager::Tca8418Key::Enter:
        *out_key = '\n';
        return true;
    case tpager::Tca8418Key::Backspace:
        *out_key = '\b';
        return true;
    default:
        break;
    }
    return false;
}

bool has_pem_extension(const char *name)
{
    if (name == nullptr) {
        return false;
    }
    if ((name[0] == '.' && name[1] == '_') || (name[0] == '_' && std::strchr(name, '~') != nullptr)) {
        return false;
    }
    const size_t len = std::strlen(name);
    if (len >= 4 && strcasecmp(name + len - 4, ".pem") == 0) {
        return true;
    }
    // Some FAT variants expose short names without dot separators (e.g. FOO~1PEM).
    return (len >= 3) && (strcasecmp(name + len - 3, "pem") == 0);
}

void load_ssh_keys_from_sd()
{
    if (g_terminal == nullptr) {
        return;
    }

    tpager::SdDiagStats stats = {};
    const esp_err_t mount_ret = tpager::sd_mount_and_scan_keys(&stats);
    if (mount_ret != ESP_OK) {
        ESP_LOGW(kTag, "SD mount/scan failed: %s; trying existing mountpoints", esp_err_to_name(mount_ret));
    }

    const char *active_keys_dir = kKeysDir;
    DIR *dir = opendir(kKeysDir);
    if (dir == nullptr) {
        active_keys_dir = kKeysDirAlt;
        dir = opendir(kKeysDirAlt);
    }
    if (dir == nullptr) {
        append_terminal_text("No /sdcard/ssh_keys or /sd/ssh_keys directory\n");
        return;
    }

    int32_t keys_loaded = 0;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!has_pem_extension(entry->d_name)) {
            continue;
        }

        const std::string filepath = std::string(active_keys_dir) + "/" + entry->d_name;

        FILE *f = std::fopen(filepath.c_str(), "rb");
        if (f == nullptr) {
            ESP_LOGW(kTag, "Failed to open key %s", filepath.c_str());
            continue;
        }

        std::fseek(f, 0, SEEK_END);
        const long file_size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (file_size <= 0 || file_size > static_cast<long>(kMaxKeySize)) {
            ESP_LOGW(kTag, "Skipping key %s (size=%ld)", filepath.c_str(), file_size);
            std::fclose(f);
            continue;
        }

        std::vector<char> key_data(static_cast<size_t>(file_size) + 1, '\0');
        const size_t bytes_read = std::fread(key_data.data(), 1, static_cast<size_t>(file_size), f);
        std::fclose(f);
        if (bytes_read != static_cast<size_t>(file_size)) {
            ESP_LOGW(kTag, "Short read for key %s", filepath.c_str());
            continue;
        }

        g_terminal->load_key_from_memory(entry->d_name, key_data.data(), bytes_read);
        keys_loaded++;
    }

    closedir(dir);
    char summary[80];
    std::snprintf(summary, sizeof(summary), "Loaded %" PRId32 " key(s) from SD\n", keys_loaded);
    append_terminal_text(summary);
}

void poll_keyboard()
{
    while (true) {
        tpager::Tca8418Event ev = {};
        const esp_err_t ret = tpager::tca8418_poll_event(g_tca8418, &g_tca8418_state, &ev);
        if (ret == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (ret != ESP_OK) {
            ESP_LOGW(kTag, "keyboard poll failed: %s", esp_err_to_name(ret));
            break;
        }
        if (!ev.valid) {
            break;
        }

        if (ev.key == tpager::Tca8418Key::Alt) {
            g_alt_held = ev.pressed;
        } else if (ev.key == tpager::Tca8418Key::Caps) {
            g_caps_held = ev.pressed;
        }

        g_keyboard_events++;
        if (ev.pressed) {
            g_keyboard_presses++;
        } else {
            g_keyboard_releases++;
        }

        char key = '\0';
        if (to_terminal_char(ev, &key)) {
            if (ev.erase_previous_space) {
                handle_terminal_key('\b');
            }
            handle_terminal_key(key);
        }
    }

    tpager::diag_display_set_keyboard_stats(&g_display, g_keyboard_events, g_keyboard_presses, g_keyboard_releases,
                                            gpio_get_level(kKeyboardIrq));
}

void poll_encoder()
{
    tpager::EncoderEvent ev = {};
    if (tpager::encoder_poll(&g_encoder, &ev) != ESP_OK) {
        return;
    }

    g_encoder_transitions += ev.transitions;
    if (ev.moved) {
        g_encoder_net += ev.delta;
        if (g_terminal != nullptr && g_tca8418_state.symbol) {
            // The orange Symbols key normally remains a momentary keyboard
            // chord.  While it is held, the dial substitutes for the
            // T-Deck's horizontal gestures: counter-clockwise is the
            // left-swipe control sheet and clockwise is the right-swipe SSH
            // server picker.  Repeated detents keep the chosen sheet open.
            if (ev.delta < 0) {
                g_terminal->show_special_keys_panel();
            } else if (ev.delta > 0) {
                g_terminal->show_server_picker();
            }
        } else if (g_terminal != nullptr && g_terminal->cycle_active_overlay(ev.delta)) {
            // Once a sheet is open, the unmodified dial selects a button and
            // the center button activates it.  This provides the touch-panel
            // interaction model on the physical-keyboard-only T-Pager.
        } else if (g_terminal != nullptr && lvgl_port_lock(25)) {
            int32_t steps = ev.delta;

            // Encoder interaction contract:
            // - default      : command history
            // - Orange Symbols + encoder: terminal sheets (handled above)
            // - ALT + encoder: cursor left/right on input line
            // - CAPS + encoder: terminal output scroll up/down
            // CAPS mode has priority if both modifiers are held.
            const bool scroll_mode = g_caps_held;
            const bool cursor_mode = !scroll_mode && g_alt_held;

            while (steps > 0) {
                if (scroll_mode) {
                    g_terminal->scroll_terminal_output(-1);
                } else if (cursor_mode) {
                    g_terminal->move_cursor_right();
                } else {
                    g_terminal->navigate_history(1);
                }
                steps--;
            }
            while (steps < 0) {
                if (scroll_mode) {
                    g_terminal->scroll_terminal_output(1);
                } else if (cursor_mode) {
                    g_terminal->move_cursor_left();
                } else {
                    g_terminal->navigate_history(-1);
                }
                steps++;
            }
            lvgl_port_unlock();
        }
    }
    if (ev.button_changed) {
        if (ev.button_pressed) {
            // A sheet action consumes the center press.  Do not also submit
            // a newline or arm the long-press shutdown gesture.
            if (g_terminal != nullptr && g_terminal->activate_active_overlay()) {
                g_encoder_center_held = false;
                g_encoder_center_hold_fired = false;
                g_encoder_center_press_tick = 0;
                tpager::diag_display_set_encoder_stats(&g_display, g_encoder_net, g_encoder_transitions);
                return;
            }
            g_encoder_center_held = true;
            g_encoder_center_hold_fired = false;
            g_encoder_center_press_tick = xTaskGetTickCount();
            handle_terminal_key('\n');
        } else {
            g_encoder_center_held = false;
            g_encoder_center_hold_fired = false;
            g_encoder_center_press_tick = 0;
        }
    }

    if (g_encoder_center_held && !g_encoder_center_hold_fired) {
        const TickType_t held_ticks = xTaskGetTickCount() - g_encoder_center_press_tick;
        if (held_ticks >= pdMS_TO_TICKS(2000)) {
            g_encoder_center_hold_fired = true;
            append_terminal_text("Center hold: shutdown requested\n");
            tpager_request_shutdown();
        }
    }

    tpager::diag_display_set_encoder_stats(&g_display, g_encoder_net, g_encoder_transitions);
}

void serial_control_task(void *)
{
    // Host automation contract: allow prefixed commands over USB serial so CI
    // tooling can trigger "serialrx" without manual keyboard entry.  Read the
    // native USB JTAG FIFO directly: the generic stdin VFS can be write-only
    // on the Pager even though the same physical endpoint is usable for logs.

    std::string line;
    line.reserve(256);
    char buf[96];

    while (true) {
        if (g_terminal != nullptr && g_terminal->is_serial_rx_in_progress()) {
            vTaskDelay(ticks_from_ms(20));
            continue;
        }

        const ssize_t nread = static_cast<ssize_t>(
            usb_serial_jtag_ll_read_rxfifo(reinterpret_cast<uint8_t *>(buf), sizeof(buf)));

        if (nread <= 0) {
            vTaskDelay(ticks_from_ms(20));
            continue;
        }

        for (ssize_t i = 0; i < nread; ++i) {
            const char ch = buf[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (!line.empty()) {
                    handle_serial_control_line(line);
                    line.clear();
                }
                continue;
            }
            if (line.size() < 512) {
                line.push_back(ch);
            }
        }
    }
}

void runtime_task(void *)
{
    g_runtime_task_handle = xTaskGetCurrentTaskHandle();

    while (true) {
        // Keep a short poll timeout so brief key taps (especially Space fallback)
        // are handled with low latency even if IRQ edges are imperfect.
        (void)ulTaskNotifyTake(pdTRUE, ticks_from_ms(10));
        if (g_keyboard_state == 1) {
            poll_keyboard();
        }
        poll_encoder();
    }
}

void runtime_health_task(void *)
{
    // The USB Serial/JTAG console is the only non-visual observation channel
    // available on a connected Pager.  A small periodic record makes a
    // post-flash capture useful without adding any network listener or
    // retaining user/session data.
    while (true) {
        const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(kTag, "PAGER_HEALTH ui=%d kbd=%d psram=%u/%uK", g_terminal_ui_state, g_keyboard_state,
                 static_cast<unsigned>(psram_free / 1024),
                 static_cast<unsigned>(psram_total / 1024));
        vTaskDelay(ticks_from_ms(5000));
    }
}

bool configure_keyboard_interrupt()
{
    gpio_config_t irq_cfg = {};
    irq_cfg.mode = GPIO_MODE_INPUT;
    irq_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    irq_cfg.pin_bit_mask = (1ULL << kKeyboardIrq);
    esp_err_t ret = gpio_config(&irq_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "keyboard IRQ gpio_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_intr_type(kKeyboardIrq, GPIO_INTR_NEGEDGE));
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(kKeyboardIrq, keyboard_irq_isr, nullptr));
    return true;
}

void keyboard_init_task(void *)
{
    // I2C keyboard bring-up must never prevent the terminal from becoming
    // usable. A bad peripheral transaction therefore degrades keyboard input
    // while leaving display, Wi-Fi, and the terminal core alive.
    if (!g_keyboard_prerequisites_ready) {
        ESP_LOGW(kTag, "keyboard prerequisites unavailable");
        g_keyboard_state = -1;
        append_terminal_text("Keyboard unavailable; terminal remains usable\n");
        vTaskDelete(nullptr);
        return;
    }

    bool keyboard_ok = keyboard_power_reset(tpager::XL9555_PIN_KB_POWER_EN_PRIMARY);
    if (!keyboard_ok) {
        keyboard_ok = keyboard_power_reset(tpager::XL9555_PIN_KB_POWER_EN_FALLBACK);
    }
    if (keyboard_ok) {
        const esp_err_t matrix_ret = tpager::tca8418_configure_matrix(&g_tca8418, 4, 10);
        if (matrix_ret != ESP_OK) {
            ESP_LOGW(kTag, "tca8418_configure_matrix failed: %s", esp_err_to_name(matrix_ret));
            keyboard_ok = false;
        }
    }
    if (keyboard_ok) {
        const esp_err_t flush_ret = tpager::tca8418_flush_fifo(g_tca8418);
        if (flush_ret != ESP_OK) {
            ESP_LOGW(kTag, "tca8418_flush_fifo failed: %s", esp_err_to_name(flush_ret));
            keyboard_ok = false;
        }
    }
    if (keyboard_ok) {
        keyboard_ok = configure_keyboard_interrupt();
    }

    g_keyboard_state = keyboard_ok ? 1 : -1;
    append_terminal_text(keyboard_ok ? "Keyboard ready\n" : "Keyboard unavailable; terminal remains usable\n");
    ESP_LOGI(kTag, "keyboard init: %s", keyboard_ok ? "PASS" : "DEGRADED");
    vTaskDelete(nullptr);
}

bool initialize_terminal_ui_with_retry()
{
    // The diagnostic frame deliberately starts LVGL before the full terminal.
    // Its first DMA flush may hold LVGL's recursive mutex longer than a short
    // boot-time timeout.  `0` is the esp_lvgl_port contract for waiting until
    // the mutex is available; use it once here so a healthy display cannot be
    // left permanently on the diagnostic frame merely because the first flush
    // was slow.  This runs before the input/network tasks are started, so it
    // cannot starve an interactive path.
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(kTag, "Failed to acquire LVGL lock for terminal UI");
        g_terminal_ui_state = -1;
        tpager::diag_display_set_stage(&g_display, "Stage: terminal UI unavailable");
        return false;
    }

    lv_obj_t *screen = g_terminal != nullptr ? g_terminal->create_terminal_screen() : nullptr;
    if (screen != nullptr) {
        lv_scr_load(screen);
#ifdef POCKETSSH_VERSION
        g_terminal->append_text("PocketSSH v" POCKETSSH_VERSION "\n");
#else
        g_terminal->append_text("PocketSSH T-Pager\n");
#endif
        g_terminal->append_text("Keyboard + encoder active\n");
        const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        char psram_status[96] = {};
        std::snprintf(psram_status, sizeof(psram_status),
                      "PSRAM: %u KiB total, %u KiB free\n\n",
                      static_cast<unsigned>(psram_total / 1024),
                      static_cast<unsigned>(psram_free / 1024));
        g_terminal->append_text(psram_status);
        lvgl_port_unlock();
        g_terminal_ui_state = 1;
        return true;
    }

    lvgl_port_unlock();
    ESP_LOGE(kTag, "Failed to create terminal UI");
    g_terminal_ui_state = -1;
    tpager::diag_display_set_stage(&g_display, "Stage: terminal UI unavailable");
    return false;
}

void show_boot_psram_status()
{
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    char status[96] = {};
    // The full 512 KiB PSRAM integrity check already proved this hardware,
    // but repeating it before every UI handoff can leave a cold boot waiting
    // in the allocator. Keep startup non-blocking and report live capacity.
    // The preceding "Last line:" prefix is added by the display helper.
    std::snprintf(status, sizeof(status), "PSRAM %u/%uK ready",
                  static_cast<unsigned>(psram_free / 1024),
                  static_cast<unsigned>(psram_total / 1024));
    tpager::diag_display_set_last_line(&g_display, status);
}

void boot_wifi_task(void *)
{
    // Startup contract: defer auto-connect off app_main so input runtime starts
    // immediately and watchdog-sensitive LVGL/network work does not block boot.
    vTaskDelay(ticks_from_ms(200));
    if (g_terminal != nullptr) {
        g_terminal->try_boot_wifi_auto_connect();
    }
    vTaskDelete(nullptr);
}

void shutdown_task(void *)
{
    append_terminal_text("Powering down...\n");
    ESP_LOGW(kTag, "Shutdown requested: entering deep sleep");

    // Stop IRQ traffic while we wind down.
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_intr_disable(kKeyboardIrq));
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(kKeyboardIrq));

    // Best-effort comms quiesce.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());

    // Turn display backlight off.
    gpio_reset_pin(kDisplayBacklight);
    gpio_set_direction(kDisplayBacklight, GPIO_MODE_OUTPUT);
    gpio_set_level(kDisplayBacklight, 0);

    // Power-gate peripherals controlled via expander.
    if (tpager::xl9555_probe(g_xl9555) == ESP_OK) {
        (void)tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_SD_POWER_EN, true);
        (void)tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_KB_RESET, true);
        (void)tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_KB_POWER_EN_PRIMARY, true);
        (void)tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_KB_POWER_EN_FALLBACK, true);
        ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_SD_POWER_EN, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_KB_RESET, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_KB_POWER_EN_PRIMARY, false));
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_KB_POWER_EN_FALLBACK, false));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::sd_unmount());

    // Wake sources: BOOT key or encoder center press.
    gpio_config_t wake_cfg = {};
    wake_cfg.mode = GPIO_MODE_INPUT;
    wake_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    wake_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    wake_cfg.pin_bit_mask = (1ULL << kBootButton) | (1ULL << kEncoderCenter);
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&wake_cfg));

    const uint64_t wake_mask = (1ULL << kBootButton) | (1ULL << kEncoderCenter);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW));
#else
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW));
#endif

    // Avoid immediate wake loops if BOOT/encoder center is still held at sleep entry.
    for (int i = 0; i < 250; ++i) {
        const bool boot_pressed = (gpio_get_level(kBootButton) == 0);
        const bool center_pressed = (gpio_get_level(kEncoderCenter) == 0);
        if (!boot_pressed && !center_pressed) {
            break;
        }
        vTaskDelay(ticks_from_ms(20));
    }

    vTaskDelay(ticks_from_ms(150));
    esp_deep_sleep_start();
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void tpager_request_shutdown(void)
{
    if (g_shutdown_requested) {
        return;
    }
    g_shutdown_requested = true;
    xTaskCreatePinnedToCore(shutdown_task, "tpager_shutdown_task", 6144, nullptr, 6, nullptr, 1);
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(kTag, "===== TPAGER TARGET BOOT =====");
    BaseType_t boot_trace_ok =
        xTaskCreatePinnedToCore(boot_trace_task, "tpager_boot_trace", 2048, nullptr, 2, nullptr, 0);
    if (boot_trace_ok != pdPASS) {
        ESP_LOGW(kTag, "Failed to start raw USB boot trace");
    }

    // Initialize I2C/peripheral controls before display starts using shared SPI.
    // This lets SD mount happen while SPI is otherwise idle.
    g_boot_stage = 1;
    bool i2c_ready = false;
    ret = i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "i2c_init failed: %s", esp_err_to_name(ret));
    } else {
        i2c_ready = true;
    }

    bool xl9555_ready = false;
    bool tca_ready = false;
    if (i2c_ready) {
        ret = tpager::xl9555_init(&g_xl9555, kI2CPort, 0x20, kI2CTimeoutTicks);
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "xl9555_init failed: %s", esp_err_to_name(ret));
        } else {
            xl9555_ready = true;
        }

        ret = tpager::tca8418_init(&g_tca8418, kI2CPort, kTCA8418Addr, kI2CTimeoutTicks);
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "tca8418_init failed: %s", esp_err_to_name(ret));
        } else {
            tca_ready = true;
        }
    }

    if (xl9555_ready && tpager::xl9555_probe(g_xl9555) == ESP_OK) {
        if (tpager::xl9555_set_dir(g_xl9555, tpager::XL9555_PIN_SD_POWER_EN, true) == ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tpager::xl9555_write_pin(g_xl9555, tpager::XL9555_PIN_SD_POWER_EN, true));
        }
    } else {
        ESP_LOGW(kTag, "XL9555 not reachable, skipping SD power control");
    }
    g_boot_stage = 2;

    // Create terminal backend early so keys can preload before LVGL/display starts.
    if (g_terminal == nullptr) {
        g_terminal = new SSHTerminal();
    }
    load_ssh_keys_from_sd();
    g_boot_stage = 3;

    ret = tpager::diag_display_init(&g_display);
    if (ret == ESP_OK) {
        tpager::diag_display_set_stage(&g_display, "Stage: init I2C");
        show_boot_psram_status();
    }
    g_boot_stage = 4;

    g_keyboard_prerequisites_ready = tca_ready && xl9555_ready;
    tpager::diag_display_set_stage(&g_display, "Stage: terminal init");

    BaseType_t health_ok =
        xTaskCreatePinnedToCore(runtime_health_task, "tpager_health", 3072, nullptr, 2, nullptr, 0);
    if (health_ok != pdPASS) {
        ESP_LOGW(kTag, "Failed to start health task");
    }
    g_boot_stage = 5;

    // Bring up the user-facing terminal before the optional dial. The initial
    // LVGL handoff is user-visible and must not be delayed by a peripheral
    // whose pins may be electrically unsettled during cold boot.
    (void)initialize_terminal_ui_with_retry();
    g_boot_stage = 6;

    tpager::diag_display_set_stage(&g_display, "Stage: encoder init");
    ret = tpager::encoder_init(&g_encoder, kEncoderA, kEncoderB, kEncoderCenter);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "encoder init failed: %s", esp_err_to_name(ret));
    }
    tpager::diag_display_set_encoder_stats(&g_display, g_encoder_net, g_encoder_transitions);
    tpager::diag_display_set_keyboard_stats(&g_display, g_keyboard_events, g_keyboard_presses, g_keyboard_releases,
                                            gpio_get_level(kKeyboardIrq));

    BaseType_t runtime_ok =
        xTaskCreatePinnedToCore(runtime_task, "tpager_runtime_task", 8192, nullptr, 5, nullptr, 1);
    if (runtime_ok != pdPASS) {
        ESP_LOGE(kTag, "Failed to start runtime task; keyboard/encoder disabled");
    }
    BaseType_t keyboard_boot_ok =
        xTaskCreatePinnedToCore(keyboard_init_task, "tpager_keyboard_init", 4096, nullptr, 4, nullptr, 0);
    if (keyboard_boot_ok != pdPASS) {
        g_keyboard_state = -1;
        ESP_LOGW(kTag, "Failed to start keyboard init task");
    }
    BaseType_t serial_ctl_ok =
        xTaskCreatePinnedToCore(serial_control_task, "tpager_serial_ctl", 4096, nullptr, 3, nullptr, 0);
    if (serial_ctl_ok != pdPASS) {
        ESP_LOGW(kTag, "Failed to start serial control task; __pocketctl disabled");
    }

    // Profile contract: if wifi_config includes AutoConnect=true profiles, try
    // them once during boot so alias-based SSH can work immediately.
    BaseType_t wifi_boot_ok =
        xTaskCreatePinnedToCore(boot_wifi_task, "tpager_boot_wifi", 8192, nullptr, 4, nullptr, 0);
    if (wifi_boot_ok != pdPASS) {
        ESP_LOGW(kTag, "Failed to start boot wifi task; skipping auto-connect");
    }

    g_boot_stage = 100;

    while (true) {
        vTaskDelay(ticks_from_ms(1000));
    }
}
