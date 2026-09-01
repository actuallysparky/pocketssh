# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Project version
set(PROJECT_VER "2.0.0-dev")

# Each target owns its generated sdkconfig in its build directory.  The
# versioned project baseline retains required BSP/display and Launcher-safe
# partition settings; target defaults are then applied to select the RAM bus.
# The Pager has QSPI (quad-mode) PSRAM, while the T-Deck Plus uses octal PSRAM.
# A caller may still explicitly provide SDKCONFIG or SDKCONFIG_DEFAULTS for a
# one-off diagnostic build.
if(TPAGER_TARGET OR TPAGER_DIAG)
    set(POCKETSSH_TARGET_SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.project.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.tpager.defaults")
else()
    set(POCKETSSH_TARGET_SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.project.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.tdeckplus.defaults")
endif()

if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH
        "PocketSSH generated SDK configuration")
endif()

if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${POCKETSSH_TARGET_SDKCONFIG_DEFAULTS}" CACHE STRING
        "PocketSSH target SDK configuration defaults")
endif()

# TPAGER_TARGET contract:
# - Propagate only the libssh2 packet-size override globally so managed
#   components pick it up.
# - Do not publish TPAGER_TARGET to every component to avoid accidental macro
#   collisions outside the app code.
if(TPAGER_TARGET)
    add_compile_definitions(LIBSSH2_MAX_SSH_PACKET_LEN=8000)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(PocketSSH)
