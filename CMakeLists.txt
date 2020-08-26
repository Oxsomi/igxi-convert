cmake_minimum_required(VERSION 3.12.4)
project(igxi-convert)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB_RECURSE hpp "include/*.hpp")
file(GLOB_RECURSE cpp "src/*.cpp")

# Link library

add_library(
	igxi-convert STATIC
	${hpp}
	${cpp}
	CMakeLists.txt
)

target_include_directories(igxi-convert PRIVATE include)
target_include_directories(igxi-convert PRIVATE third_party)
target_include_directories(igxi-convert PRIVATE igxi/include)

add_subdirectory(igxi)

get_target_property(IGNIS_SOURCE_DIR ignis SOURCE_DIR)
get_target_property(CORE2_SOURCE_DIR ocore SOURCE_DIR)
target_include_directories(igxi-convert PUBLIC ${IGNIS_SOURCE_DIR}/include)
target_include_directories(igxi-convert PUBLIC ${CORE2_SOURCE_DIR}/include)
target_link_directories(igxi-convert PUBLIC igxi ignis ocore)

source_group("Headers" FILES ${hpp})
source_group("Source" FILES ${cpp})

if(MSVC)
    target_compile_options(igxi-convert PRIVATE /W4 /WX /MD /MP /wd26812 /wd4201 /EHsc /GR)
else()
    target_compile_options(igxi-convert PRIVATE -Wall -Wpedantic -Wextra -Werror)
endif()