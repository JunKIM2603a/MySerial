# Set the minimum version of CMake required.
cmake_minimum_required(VERSION 3.10)

# Define the project name.
project(LogConsumer CXX)

# Add the executable for the consumer application.
# The executable will be named 'consumer' on most systems, or 'consumer.exe' on Windows.
add_executable(consumer main.cpp)

# 콘솔 앱 강제 (WinMain 찾지 않도록)
set_target_properties(consumer PROPERTIES WIN32_EXECUTABLE FALSE)

# Link necessary libraries.
# For Windows API functions, the C++ compiler typically includes the necessary libraries by default.
# However, you can explicitly link 'kernel32.lib' for clarity, though it might not be strictly necessary.
# This line is primarily for documentation and cross-platform consistency.
# On Windows, MSVC handles this automatically.
# target_link_libraries(consumer PRIVATE kernel32)

# Set the C++ standard to C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Unicode support for Windows.
# This ensures that TCHAR, _tmain, and other related macros work correctly.
# In Visual Studio, this corresponds to the 'Use Unicode Character Set' setting.
if (WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()