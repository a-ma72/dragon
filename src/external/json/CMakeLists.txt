cmake_minimum_required(VERSION 3.10)

project(json VERSION 3.12.0 LANGUAGES CXX)


add_library(${PROJECT_NAME} INTERFACE)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_11)

target_include_directories(
    ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hpp/>
)
