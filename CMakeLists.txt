cmake_minimum_required(VERSION 3.16)
project(ClipForge VERSION 0.1 LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-Wall -Wextra -Wpedantic)

# Find dependencies
find_package(SQLite3 REQUIRED)
find_package(Curses REQUIRED)

# Include directories
include_directories(
    ${SQLite3_INCLUDE_DIRS}
    ${CURSES_INCLUDE_DIR}
)

# Source files
add_executable(clipforge
    src/main.cpp
)

# Link libraries
target_link_libraries(clipforge
    ${SQLite3_LIBRARIES}
    ${CURSES_LIBRARIES}
)