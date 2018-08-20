cmake_minimum_required(VERSION 2.6)

set(CMAKE_CXX_STANDARD 14)

# --- dependencies ---

if (NOT HAS_LIBGG)
	message("including libgg")
	add_subdirectory(../framework/libgg libgg)
endif (NOT HAS_LIBGG)
if (NOT HAS_FRAMEWORK)
	message("including framework")
	add_subdirectory(../framework/framework framework)
endif (NOT HAS_FRAMEWORK)

#

project(chibi)

# --- chibi ---

add_executable(chibi filesystem.cpp filesystem.h main.cpp)

target_link_libraries(chibi PRIVATE ${FRAMEWORK_LIBRARIES})
target_link_libraries(chibi PRIVATE framework)
target_link_libraries(chibi PRIVATE libgg)
