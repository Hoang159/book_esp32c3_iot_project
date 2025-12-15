# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

add_compile_options(-fdiagnostics-color=always)

# Determine whether to define RAIMAKER_PATH
if(NOT DEFINED ENV{RAIMAKER_PATH})
    set(RAIMAKER_PATH "I:/University/Practice/Code/ESP_IDF/esp-rainmaker")
    message(WARNING "RAIMAKER_PATH not set, using default: ${RAIMAKER_PATH}")
else()
    set(RAIMAKER_PATH $ENV{RAIMAKER_PATH})
endif()

# Add RainMaker components and other common application components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components/light_driver
                        ${CMAKE_CURRENT_LIST_DIR}/../components/button
                        ${CMAKE_CURRENT_LIST_DIR}/../components/app_storage
                        ${CMAKE_CURRENT_LIST_DIR}/../components/app_wifi
			            ${CMAKE_CURRENT_LIST_DIR}/../components/qrcode
                        ${RAIMAKER_PATH}/components/esp_rainmaker
                        ${RAIMAKER_PATH}/components/esp_schedule
                        ${RAIMAKER_PATH}/components/rmaker_common
                        )

set(EXCLUDE_COMPONENTS "coap" "asio")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Project version
set(PROJECT_VER "1.0")
project(6_performance_optimize VERSION ${PROJECT_VER})

if(CONFIG_APP_PROJECT_VER_FROM_CONFIG)
    add_compile_definitions(APP_PROJECT_VER="${CONFIG_APP_PROJECT_VER}")
endif()