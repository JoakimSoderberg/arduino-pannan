

set(CMAKE_TOOLCHAIN_FILE arduino-cmake/cmake/ArduinoToolchain.cmake)

cmake_minimum_required(VERSION 2.8)

project(pannan)

option(PANNAN_CLIENT "Turn on HTTP client" ON)
option(PANNAN_SERVER "Turn on HTTP server" ON)
option(PANNAN_DS2762 "Turn on DS2762 thermocouple support" ON)
option(PANNAN_NAMES "Turn on support for setting names via webserver (does not fit together with thermocouple)" OFF)

# TODO: Option to set serial port.
# TODO: Option to set serial port program.

set(ARDUINO_DEFAULT_BOARD ethernet) # Default Board ID, when not specified

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/arduino-cmake/cmake/Platform)
include(Arduino)

message("Arduino SDK v${ARDUINO_SDK_VERSION}")
print_board_list()
print_programmer_list()

if (PANNAN_CLIENT)
    add_definitions(-DPANNAN_CLIENT)
endif()

if (PANNAN_SERVER)
    add_definitions(-DPANNAN_SERVER)
endif()

if (PANNAN_DS2762 AND PANNAN_NAMES)
    message(FATAL_ERROR "Both DS2762 and name support does not fit")
endif()

if (PANNAN_DS2762)
    add_definitions(-DPANNAN_DS2762)
endif()

if (PANNAN_NAMES)
    add_definitions(-DPANNAN_NAME_SUPPORT)
endif()


##
## Ethernet library.
##
set(ETHER_DIR ${CMAKE_SOURCE_DIR}/Ethernet)
include_directories(${ETHER_DIR}/src)

generate_arduino_library(Ethernet
    SRCS    ${ETHER_DIR}/src/Dhcp.cpp
            ${ETHER_DIR}/src/Dns.cpp
            ${ETHER_DIR}/src/Ethernet.cpp
            ${ETHER_DIR}/src/EthernetClient.cpp
            ${ETHER_DIR}/src/EthernetServer.cpp
            ${ETHER_DIR}/src/EthernetUdp.cpp
            ${ETHER_DIR}/src/utility/socket.cpp
            ${ETHER_DIR}/src/utility/w5100.cpp
    HDRS    ${ETHER_DIR}/src/Dhcp.h
            ${ETHER_DIR}/src/Dns.h
            ${ETHER_DIR}/src/Ethernet.h
            ${ETHER_DIR}/src/EthernetClient.h
            ${ETHER_DIR}/src/EthernetServer.h
            ${ETHER_DIR}/src/EthernetUdp.h
            ${ETHER_DIR}/src/utility/socket.h
            ${ETHER_DIR}/src/utility/w5100.h
    )

#
# Button library.
#
set(BUTTON_DIR ${CMAKE_SOURCE_DIR}/Button)
include_directories(${BUTTON_DIR}/src)

generate_arduino_library(Button
    SRCS    ${BUTTON_DIR}/Button.cpp
    HDRS    ${BUTTON_DIR}/Button.h)

#
# DS2762 "High-Precision Li+ Battery Monitor" library.
# (Used by the thermocouple sensor)
#
set(DS2762_DIR ${CMAKE_SOURCE_DIR}/DS2762)
include_directories(${DS2762_DIR})

generate_arduino_library(DS2762
    SRCS    ${DS2762_DIR}/DS2762.cpp
    HDRS    ${DS2762_DIR}/DS2762.h)

#
# k-thermocouple-lib
#
set(KTHERM_DIR ${CMAKE_SOURCE_DIR}/kthermlib)
include_directories(${KTHERM_DIR})

add_definitions(-DKTHERM_WITH_BINARY_SEARCH -DKTHERM_WITH_PROGMEM)
generate_arduino_library(ktherm
    SRCS    ${KTHERM_DIR}/thermocouple.c
    HDRS    ${KTHERM_DIR}/thermocouple.h)

##
## OneWire library.
##
include_directories(${CMAKE_SOURCE_DIR}/OneWire)
generate_arduino_library(OneWire
    SRCS ${CMAKE_SOURCE_DIR}/OneWire/OneWire.cpp
    HDRS ${CMAKE_SOURCE_DIR}/OneWire/OneWire.h)

##
## Dallas temperature library.
##
set(DALLAS_DIR ${CMAKE_SOURCE_DIR}/DallasTemperature)
include_directories(${DALLAS_DIR})

generate_arduino_library(DallasTemperature
    SRCS ${DALLAS_DIR}/DallasTemperature.cpp
    HDRS ${DALLAS_DIR}/DallasTemperature.h
    LIBS OneWire)

##
## Memory Free library.
##
set(MEMORY_FREE_DIR ${CMAKE_SOURCE_DIR}/MemoryFree)
include_directories(${MEMORY_FREE_DIR})

generate_arduino_library(MemoryFree
    SRCS ${MEMORY_FREE_DIR}/MemoryFree.cpp
    HDRS ${MEMORY_FREE_DIR}/MemoryFree.h)

#
# Pannan firmware.
#
generate_arduino_firmware(pannan
    SRCS pannan.cpp
    HDRS pannan.h
    LIBS 
        DallasTemperature
        DS2762
        ktherm
        MemoryFree
    PORT /dev/tty.usbserial-A600exfH
    SERIAL picocom @SERIAL_PORT@ --baud 9600 --nolock --echo
    BOARD ethernet)

#
# Setnames (Serial protocol for setting sensor names).
#
generate_arduino_firmware(setnames
    SRCS setnames.cpp
    HDRS pannan.h
    LIBS 
        DallasTemperature
    PORT /dev/tty.usbserial-A600exfH
    SERIAL picocom @SERIAL_PORT@ --baud 9600 --nolock --echo
    BOARD ethernet)

