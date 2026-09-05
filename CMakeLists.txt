cmake_minimum_required(VERSION 3.19)

# --------------------------------------------------------------------------
# Ten projekt zaklada budowanie z oficjalnej binarnej dystrybucji CEF
# ("cef_binary_...") pobranej z https://cef-builds.spotifycdn.com/index.html
# Ustaw CEF_ROOT tak, zeby wskazywal na rozpakowany folder tej dystrybucji,
# np.:
#   cmake -B build -DCEF_ROOT=C:/cef_binary_<wersja>_windows64
# --------------------------------------------------------------------------

project(glass_browser)

# CEF 151 (Chromium 151) korzysta w naglowkach z C++20 (std::wstring_view,
# concepts, itd.) - bez tego kompilator MSVC domyslnie buduje w C++14
# i sypie bledami w cef_logging.h / cef_scoped_refptr.h.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcef_dll_wrapper (budowany przez wlasne makra CEF przy find_package
# ponizej) uzywa statycznego CRT (/MT, /MTd). Domyslnie CMake buduje nasze
# targety z dynamicznym CRT (/MD) - te dwa sie wykluczaja przy linkowaniu
# (LNK2038/LNK2005). Wymuszamy wiec /MT/MTd globalnie, zanim powstanie
# jakikolwiek target. Wymaga CMake >= 3.15 (mamy 3.19) - polityka CMP0091
# jest wtedy domyslnie NEW.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT DEFINED CEF_ROOT)
  message(FATAL_ERROR "Ustaw -DCEF_ROOT=<sciezka do cef_binary_...> przy konfiguracji CMake")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# Standardowe makra dostarczane przez sama dystrybucje CEF - ustawiaja
# poprawne flagi kompilatora/linkera zgodne z tym, jak zbudowano CEF.
find_package(CEF REQUIRED)

# Naglowki CEF sa dolaczane w kodzie jako "include/cef_app.h" itd.,
# czyli wzgledem CEF_ROOT - bez tego kompilator ich nie znajdzie.
include_directories(${CEF_ROOT})

set(GLASS_SRCS
  src/main.cpp
)

# libcef_dll_wrapper to statyczna biblioteka pomocnicza dostarczana przez
# CEF (zawiera m.in. CefMessageRouterBrowserSide z include/wrapper/*).
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

add_executable(glass_browser WIN32 ${GLASS_SRCS})

# find_package(CEF) powyzej samo narzuca swoj wlasny standard C++ (np. 17)
# poprzez CMAKE_CXX_FLAGS, co nadpisuje nasze ustawienie CMAKE_CXX_STANDARD
# sprzed find_package. Dopisujemy /std:c++20 tutaj, na samym targecie, zeby
# byla to ostatnia flaga /std: na linii kompilacji (MSVC: ostatnia wygrywa).
if(MSVC)
  target_compile_options(glass_browser PRIVATE /std:c++20)
endif()

target_link_libraries(glass_browser
  "${CEF_ROOT}/$<CONFIG>/libcef.lib"
  libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
)

set_target_properties(glass_browser PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# Kopiowanie plikow runtime CEF (libcef.dll, *.pak, icudtl.dat, locales/, itd.)
# obok naszego .exe - bez tego aplikacja nie odpali sie w ogole.
COPY_FILES("glass_browser" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CMAKE_BINARY_DIR}/$<CONFIG>")
COPY_FILES("glass_browser" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CMAKE_BINARY_DIR}/$<CONFIG>")

# Nasze wlasne zasoby (toolbar.html) - tez musza wyladowac obok .exe,
# bo main.cpp buduje do nich sciezke wzgledem katalogu programu.
add_custom_command(TARGET glass_browser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_BINARY_DIR}/$<CONFIG>/resources"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/toolbar.html"
          "${CMAKE_BINARY_DIR}/$<CONFIG>/resources/toolbar.html"
)
