# Set the minimum version of CMake required.
cmake_minimum_required(VERSION 3.10)

# Define the project name.
project(LogConsumer CXX)

# Add the executable for the consumer application.
add_executable(consumer main.cpp)

# 콘솔 앱 강제 (WinMain 찾지 않도록)
set_target_properties(consumer PROPERTIES WIN32_EXECUTABLE FALSE)

# Set the C++ standard to C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
