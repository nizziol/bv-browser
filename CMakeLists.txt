cmake_minimum_required(VERSION 3.21)

project(bv)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# CEF
# ============================================================

set(CEF_ROOT
    "C:/Projects/cef_binary_151.3.24+g2384915+chromium-151.0.7922.174_windows64"
)

set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    "${CEF_ROOT}/cmake"
)

find_package(CEF REQUIRED)

# ============================================================
# CEF wrapper
# ============================================================

add_subdirectory(
    ${CEF_LIBCEF_DLL_WRAPPER_PATH}
    libcef_dll_wrapper
)

# ============================================================
# Nizz Browser
# ============================================================

add_executable(bv WIN32
    src/main.cpp
)

target_include_directories(bv PRIVATE
    "${CEF_INCLUDE_PATH}"
)

target_link_libraries(bv PRIVATE
    libcef_dll_wrapper
    "${CEF_LIB_RELEASE}"
)

# Ustawienia wymagane przez CEF
SET_EXECUTABLE_TARGET_PROPERTIES(bv)

# ============================================================
# Output
# ============================================================

set_target_properties(bv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# ============================================================
# Kopiowanie plikow CEF
# ============================================================

COPY_FILES(
    bv
    "${CEF_BINARY_FILES}"
    "${CEF_BINARY_DIR}"
    "${CMAKE_BINARY_DIR}/Release"
)

COPY_FILES(
    bv
    "${CEF_RESOURCE_FILES}"
    "${CEF_RESOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/Release"
)