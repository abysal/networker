cmake_minimum_required(VERSION 3.20)

project(Networker)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES ./src/main.cpp ./src/tftp/tftp.cpp)



add_executable(Networker ${SOURCES})
