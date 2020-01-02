cmake_minimum_required(VERSION 3.12.4)
project(igxi-tool)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(GLOB_RECURSE igxiToolHpp "include/*.hpp")
file(GLOB_RECURSE igxiToolCpp "src/*.cpp")

# Link library

add_library(
	igxi-tool STATIC
	${igxiToolHpp}
	${igxiToolCpp}
	CMakeLists.txt
)

target_include_directories(igxi-tool PRIVATE include)
target_include_directories(igxi-tool PRIVATE third_party)
target_include_directories(igxi-tool PRIVATE igxi/include)

add_subdirectory(igxi)

target_link_directories(igxi-tool PRIVATE igxi)

source_group("Headers" FILES ${igxiHpp})
source_group("Source" FILES ${igxiToolCpp})

if(MSVC)
    target_compile_options(igxi-tool PRIVATE /W4 /WX /MD /MP /wd26812 /wd4201 /EHsc /GR)
else()
    target_compile_options(igxi-tool PRIVATE -Wall -Wpedantic -Wextra -Werror)
endif()