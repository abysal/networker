cmake_minimum_required(VERSION 3.20)

project(Networker)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES ./src/main.cpp ./src/tftp/tftp.cpp)


set(PCAP_SDK $ENV{NPCAP_SDK_DIR})

add_executable(Networker ${SOURCES})

target_include_directories(Networker PUBLIC "${PCAP_SDK}/include")
target_link_libraries(Networker PUBLIC "${PCAP_SDK}/Lib/x64/wpcap.lib")