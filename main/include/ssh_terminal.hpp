/*
 * SSHTerminal Header
 * Provides LVGL-based SSH terminal interface with WiFi connectivity.
 * Supports command history, battery monitoring, and interactive shell sessions.
 */

#ifndef SSH_TERMINAL_HPP
#define SSH_TERMINAL_HPP

#include "lvgl.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "libssh2.h"
#include "battery_measurement.hpp"
#include "terminal_core.hpp"

#define SSH_MAX_LINE_LENGTH 128
#define SSH_MAX_LINES 100
#define SSH_BUFFER_SIZE 4096

class SSHTerminal
{
public:
    SSHTerminal();
    ~SSHTerminal();

    lv_obj_t* create_terminal_screen();
    
    esp_err_t connect(const char* host, int port, const char* username, const char* password,
                      const std::string &strict_host_key_checking = "ask", int server_alive_interval = 0,
                      int server_alive_count_max = 3);
    esp_err_t connect_with_key(const char* host, int port, const char* username, const char* privkey_data,
                               size_t privkey_len, const std::string &strict_host_key_checking = "ask",
                               int server_alive_interval = 0, int server_alive_count_max = 3);
    esp_err_t disconnect();
    bool is_connected();
    
    void append_text(const char* text);
    void clear_terminal();
    void handle_key_input(char key);
    void run_control_command(const std::string &line);
    void send_command(const char* cmd);
    void navigate_history(int direction);
    void delete_current_history_entry();
    void send_current_history_command();
    void move_cursor_left();
    void move_cursor_right();
    void move_cursor_home();
    void move_cursor_end();
    void scroll_terminal_output(int steps);
    void toggle_special_keys_panel();
    void remember_reconnect_alias(const std::string &alias);
    void reconnect_last_session();
    
    esp_err_t init_wifi(const char* ssid, const char* password);
    bool is_wifi_connected();
    void try_boot_wifi_auto_connect();
    bool is_serial_rx_in_progress() const;
    void set_serial_rx_in_progress(bool in_progress);
    
    lv_obj_t* get_screen() { return terminal_screen; }
    
    void update_status_bar();
    
    // SSH key management
    void load_key_from_memory(const char* keyname, const char* key_data, size_t key_len);
    const char* get_loaded_key(const char* keyname, size_t* len);
    std::vector<std::string> get_loaded_key_names();
    
private:
    lv_obj_t* terminal_screen;
    lv_obj_t* terminal_output;
    // The local prompt retains the lightweight textarea.  SSH output uses a
    // fixed-cell span grid so SGR foreground colors and decorations are
    // rendered without flattening the terminal core into plain text.
    lv_obj_t* terminal_grid;
    std::vector<lv_obj_t*> terminal_grid_rows;
    lv_obj_t* input_label;
    lv_obj_t* status_bar;
    lv_obj_t* byte_counter_label;
    lv_obj_t* side_panel;
    lv_obj_t* side_panel_title;
    // The overlay deliberately reuses these objects across pages.  The
    // T-Deck can boot without usable PSRAM, so adding a button per F-key is
    // needlessly risky during startup.
    std::vector<lv_obj_t*> side_panel_buttons;
    uint8_t side_panel_page;
    
    std::string current_input;
    size_t cursor_pos;
    size_t bytes_received;
    std::vector<std::string> command_history;
    int history_index;
    
    lv_timer_t* cursor_blink_timer;
    bool cursor_visible;
    
    lv_timer_t* battery_update_timer;
    lv_timer_t* debug_metrics_timer;
    
    bool history_needs_save;
    lv_timer_t* history_save_timer;
    
    std::string text_buffer;
    int64_t last_display_update;
    pocketssh::TerminalCore terminal_core;
    // T-Deck Plus owns this PSRAM slab; TerminalCore only borrows it so the
    // same parser remains dependency-free in host tests.
    pocketssh::TerminalCell *terminal_scrollback_storage;
    
    bool wifi_connected;
    bool boot_wifi_auto_connect_attempted;
    bool ssh_connected;
    bool battery_initialized;
    bool wifi_error;
    std::atomic<bool> serial_rx_in_progress;
    int flash_headroom_percent;
    
    BatteryMeasurement battery;
    
    int ssh_socket;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    SemaphoreHandle_t ssh_tx_mutex;
    QueueHandle_t ssh_input_queue;
    std::atomic<uint32_t> ssh_input_enqueued{0};
    std::atomic<uint32_t> ssh_input_written{0};
    
    char* hostname;
    int port_number;
    
    // SSH key storage: keyname -> key content
    std::map<std::string, std::string> loaded_keys;

    // Connection context shown in the status bar.
    std::string connected_wifi_ssid;
    std::string connected_ssh_host;
    std::string reconnect_alias;
    std::string device_clipboard;
    std::string pending_host_key_host;
    std::string pending_host_key_type;
    std::string pending_host_key_material;
    std::string pending_host_key_fingerprint;
    int pending_host_key_port = 0;
    bool pending_host_key_changed = false;
    std::string temporary_host_key_host;
    std::string temporary_host_key_type;
    std::string temporary_host_key_material;
    int temporary_host_key_port = 0;
    int server_alive_interval_seconds = 0;
    int server_alive_count_max = 3;
    int keepalive_failures = 0;

    // Terminal font mode contract:
    // - false: compact mode (~67x13)
    // - true : larger mode (~53x9)
    bool terminal_font_big;
    std::string theme_color_name;
    uint32_t theme_color_hex;
    bool theme_color_from_nvs;

    // Touch scrubbing state for left/right cursor drag on the input line.
    bool touch_scrub_active;
    bool touch_scrub_moved;
    bool touch_scrub_axis_locked;
    bool touch_scrub_vertical_mode;
    int32_t touch_scrub_last_x;
    int32_t touch_scrub_last_y;
    int32_t touch_scrub_accum_x;
    int32_t terminal_output_touch_scroll_y;
    bool terminal_grid_touch_scroll_active;
    int32_t terminal_grid_touch_last_y;
    int32_t terminal_grid_touch_accum_y;
    bool terminal_grid_selection_active;
    size_t terminal_selection_start_row;
    size_t terminal_selection_start_col;
    size_t terminal_selection_end_row;
    size_t terminal_selection_end_col;
    
    void update_terminal_display();
    void show_ssh_terminal_view();
    void set_ssh_terminal_layout(bool active);
    void rebuild_terminal_grid();
    void render_terminal_grid_row(size_t row_index);
    void update_input_display();
    void process_received_data(const char* data, size_t len);
    void flush_display_buffer();
    void send_terminal_bytes(const std::string &bytes);
    void sync_terminal_geometry(bool notify_remote);
    bool verify_host_key(const char *host, int port, const std::string &strict_host_key_checking);
    bool save_pending_host_key();
    void copy_visible_terminal();
    void paste_device_clipboard();
    
    void load_history_from_nvs();
    void save_history_to_nvs();
    void clear_history_nvs();
    std::string strip_ansi_codes(const char* data, size_t len);
    void send_special_key(const char* sequence);
    void create_side_panel();
    void populate_side_panel_page();
    void toggle_side_panel();
    static void gesture_event_cb(lv_event_t* e);
    static void special_key_event_cb(lv_event_t* e);
    static void input_touch_event_cb(lv_event_t* e);
    static void output_touch_event_cb(lv_event_t* e);
    static void restore_output_scroll_async(void *user_data);
    static void cursor_blink_cb(lv_timer_t* timer);
    static void battery_update_cb(lv_timer_t* timer);
    static void debug_metrics_cb(lv_timer_t* timer);
    static void history_save_cb(lv_timer_t* timer);
    static void ssh_receive_task(void* param);

    void apply_terminal_font_mode();
    void set_terminal_font_mode(bool big_mode, bool announce);
    void load_default_terminal_font_mode_from_config();
    void apply_theme_colors();
    void load_theme_color_preference();
    void load_theme_color_from_nvs();
    void save_theme_color_to_nvs();
    bool set_theme_color_by_name(const std::string &name, bool announce);
    void update_debug_metrics();
    
    static int waitsocket(int socket_fd, LIBSSH2_SESSION *session);
    esp_err_t ssh_authenticate(const char* username, const char* password);
    esp_err_t ssh_authenticate_pubkey(const char* username, const char* privkey_data, size_t privkey_len);
    esp_err_t ssh_open_channel();
};

#endif
