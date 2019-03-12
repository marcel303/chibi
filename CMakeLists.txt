cmake_minimum_required(VERSION 2.6)

set(CMAKE_CXX_STANDARD 11)

project(chibi)

# --- chibi ---

add_executable(chibi filesystem.cpp filesystem.h chibi.cpp)

if (WIN32)
	target_compile_definitions(chibi PUBLIC WINDOWS)
endif (WIN32)

if (APPLE)
	target_compile_definitions(chibi PUBLIC MACOS)
endif (APPLE)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_compile_definitions(chibi PUBLIC LINUX)
endif ()