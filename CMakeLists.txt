cmake_minimum_required(VERSION 3.20)
project(ThemeGallery LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# SDL2_image: built-in stb decoder for PNG/JPG, all external codecs off
set(SDL2IMAGE_BACKEND_STB ON  CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG         ON  CACHE BOOL "" FORCE)
set(SDL2IMAGE_JPG         ON  CACHE BOOL "" FORCE)
set(SDL2IMAGE_SVG         ON  CACHE BOOL "" FORCE)
set(SDL2IMAGE_AVIF        OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JXL         OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TIF         OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_WEBP        OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED    OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS       OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_INSTALL     OFF CACHE BOOL "" FORCE)

# SDL2_ttf: system freetype, no harfbuzz
set(SDL2TTF_VENDORED      OFF CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ      OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES       OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL2_image
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
  GIT_TAG        release-2.8.2
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES "")

FetchContent_Declare(SDL2_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG        release-2.22.0
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES "")

FetchContent_MakeAvailable(SDL2_image SDL2_ttf)

add_executable(ThemeGallery
  src/main.cpp
  src/Log.cpp
  src/EsSettings.cpp
  src/Input.cpp
  src/Config.cpp
  src/Paths.cpp
  src/Layout.cpp
  src/Gfx.cpp
  src/TextCache.cpp
  src/Icons.cpp
  src/ImageCache.cpp
  src/App.cpp
  src/CarouselScreen.cpp
  src/ListScreen.cpp)

target_compile_options(ThemeGallery PRIVATE -Wall -Wextra)
target_link_libraries(ThemeGallery PRIVATE
  SDL2::SDL2
  SDL2_image::SDL2_image
  SDL2_ttf::SDL2_ttf
  CURL::libcurl
  Threads::Threads)