cmake_minimum_required(VERSION 2.6)

set(CMAKE_CXX_STANDARD 11)

project(chibi)

# --- chibi ---

add_executable(
	chibi
	base64.cpp
	base64.h
	chibi.cpp
	chibi.h
	chibi-internal.h
	filesystem.cpp
	filesystem.h
	plistgenerator.cpp
	plistgenerator.h
	stringbuilder.cpp
	stringbuilder.h
	stringhelpers.h
	write-cmake.cpp
	write-gradle.cpp
	main.cpp)

if (WIN32)
	target_compile_definitions(chibi PUBLIC WINDOWS)
endif (WIN32)

if (APPLE)
	target_compile_definitions(chibi PUBLIC MACOS)
endif (APPLE)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_compile_definitions(chibi PUBLIC LINUX)
endif ()

if (${ANDROID_ABI})
	target_compile_definitions(chibi PUBLIC ANDROID)
endif ()
