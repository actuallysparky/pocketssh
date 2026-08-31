option(TPAGER_DIAG "Build T-Pager diagnostic firmware instead of full PocketSSH UI" OFF)
option(TPAGER_TARGET "Enable T-Pager target code paths" OFF)

if(TPAGER_DIAG)
    # Diagnostic firmware is intentionally minimal so hardware bring-up loops are fast.
    set(SOURCES
        "tpager_diag.cpp"
        "tpager_display.cpp"
        "tpager_sd.cpp"
        "tpager_xl9555.cpp"
        "tpager_tca8418.cpp"
        "tpager_encoder.cpp"
        "esp_lcd_st7796.c"
    )
elseif(TPAGER_TARGET)
    # TPAGER_TARGET runtime path: dedicated app entrypoint for the T-Pager hardware.
    set(SOURCES
        "tpager_base.cpp"
        "battery_measurement.cpp"
        "ssh_terminal.cpp"
        "terminal_core.cpp"
        "tpager_display.cpp"
        "tpager_sd.cpp"
        "tpager_xl9555.cpp"
        "tpager_tca8418.cpp"
        "tpager_encoder.cpp"
        "esp_lcd_st7796.c"
    )
else()
    set(SOURCES
        "deck_base.cpp"
        "battery_measurement.cpp"
        "c3_keyboard.cpp"
        "ssh_terminal.cpp"
        "terminal_core.cpp"
        "lvgl_pocketssh_img/pocketssh_splash_static.c"
    )
endif()

# List of include directories
set(INCLUDE_DIRS
    "."
    "include"
)

if(TPAGER_DIAG)
    set(REQUIRED_COMPONENTS
        driver
        esp_lcd
        esp_lvgl_port
        lvgl
        esp_timer
        fatfs
        sdmmc
        nvs_flash
    )
elseif(TPAGER_TARGET)
    set(REQUIRED_COMPONENTS
        driver
        esp_lcd
        esp_lvgl_port
        lvgl
        esp_timer
        fatfs
        sdmmc
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        app_update
        bootloader_support
    )
else()
    set(REQUIRED_COMPONENTS
        lvgl
        nvs_flash
        esp_lcd_touch_gt911
        fatfs
        sdmmc
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        hal
        app_update
        bootloader_support
    )
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES ${REQUIRED_COMPONENTS}
)

# Add version number as compile definition
target_compile_definitions(${COMPONENT_LIB} PRIVATE POCKETSSH_VERSION="${PROJECT_VER}")

if(TPAGER_TARGET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TPAGER_TARGET=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TDECKPLUS_TARGET=1
        POCKETSSH_TARGET_LABEL="PocketSSH T-Deck Plus"
        POCKETSSH_DEFAULT_SERIALRX_FILENAME="PocketSSH-TDeckPlus.bin"
    )
endif()

if(TPAGER_DIAG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TPAGER_DIAG=1 TPAGER_TARGET=1)
endif()
