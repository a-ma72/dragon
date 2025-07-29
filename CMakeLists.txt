#[[
Build package
=============
  cmake -S . -B build -DCMAKE_INSTALL_PREFIX=build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release --parallel 6
  cmake --install build --prefix bin
  cpack --config  build\CPackConfig.cmake
#]]

cmake_minimum_required(VERSION 3.10)
project(dragon VERSION 0.2)

set(CMAKE_CXX_STANDARD 20)


set(BUILD_SHARED_LIBS OFF)

set(SDL_AUDIO OFF)
set(SDL_CAMERA OFF)
set(SDL_DIALOG OFF)
set(SDL_DISKAUDIO OFF)
set(SDL_DUMMYAUDIO OFF)
set(SDL_DUMMYCAMERA OFF)
set(SDL_DUMMYVIDEO OFF)
set(SDL_HAPTIC OFF)
set(SDL_HIDAPI OFF)
set(SDL_JOYSTICK OFF)
set(SDL_JOYSTICK_GAMEINPUT OFF)
set(SDL_POWER OFF)
set(SDL_SENSOR OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_VULKAN OFF)

set(SDLIMAGE_AVIF OFF)
set(SDLIMAGE_XCF OFF)
set(SDLIMAGE_LBM OFF)
set(SDLIMAGE_PCX OFF)
set(SDLIMAGE_PNM OFF)
set(SDLIMAGE_QOI OFF)
set(SDLIMAGE_TGA OFF)
set(SDLIMAGE_XPM OFF)
set(SDLIMAGE_XV OFF)


add_subdirectory(src/external/sdl3)
add_subdirectory(src/external/sdl_ttf)
add_subdirectory(src/external/sdl_image)
add_subdirectory(src/external/giflib-5.2.2 EXCLUDE_FROM_ALL)
add_subdirectory(src/external/json)


add_executable(dragon WIN32
        src/main.cpp
        src/dragon.rc
)
target_link_libraries(dragon
        SDL3::SDL3-static
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        giflib
        json
)

install(TARGETS dragon DESTINATION .)

install(FILES
    ${CMAKE_SOURCE_DIR}/src/logos/dragon.png
    ${CMAKE_SOURCE_DIR}/src/fonts/Freeman-Regular.TTF
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/app.png
    # C:/Windows/Fonts/ARIALNB.TTF
    DESTINATION .
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
