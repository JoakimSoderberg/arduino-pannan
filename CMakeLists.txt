

set(CMAKE_TOOLCHAIN_FILE arduino-cmake/cmake/ArduinoToolchain.cmake)

cmake_minimum_required(VERSION 2.8)

project(pannan)

option(USE_ETHERNET "Is this meant for use with Arduino Ethernet?" ON)

set(ARDUINO_DEFAULT_BOARD ethernet) # Default Board ID, when not specified

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/arduino-cmake/cmake/Platform)
include(Arduino)

message("Arduino SDK v${ARDUINO_SDK_VERSION}")
print_board_list()
print_programmer_list()

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

generate_arduino_firmware(pannan
    SRCS pannan.cpp
    HDRS pannan.h
    LIBS 
        DallasTemperature 
        Ethernet
    PORT /dev/tty.usbserial-A600exfH
    SERIAL picocom @SERIAL_PORT@ --baud 9600 --nolock --echo
    BOARD ethernet)

generate_arduino_firmware(setnames
    SRCS setnames.cpp
    HDRS pannan.h
    LIBS 
        DallasTemperature
    PORT /dev/tty.usbserial-A600exfH
    SERIAL picocom @SERIAL_PORT@ --baud 9600 --nolock --echo
    BOARD ethernet)

