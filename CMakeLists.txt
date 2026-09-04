cmake_minimum_required(VERSION 3.21)

project(bv)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CEF
set(CEF_ROOT "C:/Projects/cef_binary_151.3.24+g2384915+chromium-151.0.7922.174_windows64")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# Za³aduj konfiguracjê CEF
find_package(CEF REQUIRED)

# Wrapper CEF
add_subdirectory(
    ${CEF_LIBCEF_DLL_WRAPPER_PATH}
    libcef_dll_wrapper
)

# Nasza przegl¹darka
add_executable(bv
    src/main.cpp
)

target_include_directories(bv PRIVATE
    "${CEF_INCLUDE_PATH}"
)

target_link_libraries(bv PRIVATE
    libcef_dll_wrapper
)

# Ustawienia CEF dla aplikacji
SET_EXECUTABLE_TARGET_PROPERTIES(bv)

set_target_properties(bv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)