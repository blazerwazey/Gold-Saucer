# shophook.dll — injected into FF7_EN.exe by the AP client to enable native-grid
# Tier-3 AP shops (cross-player item names + purchase detection) under 7th Heaven.
#
# FF7_EN.exe is 32-bit, so this MUST build as a 32-bit (x86) DLL:
#   cmake -B build -A Win32
#   cmake --build build --config Release
# Output: build/Release/shophook.dll  -> copy next to FF7_EN.exe.
cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)          # enable MSVC_RUNTIME_LIBRARY
project(shophook CXX)

set(CMAKE_CXX_STANDARD 17)
# Static CRT (/MT) so the injected DLL has NO MSVCRT/VCRUNTIME dependency — the
# old FF7 process won't have those DLLs loaded, and a missing dep makes the
# injected LoadLibrary fail silently. Matches MinHook's static build.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook is bundled with the repo (../MinHook_134_lib). Adjust if your headers
# / .lib live elsewhere. Build the matching x86 MinHook .lib.
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MinHook_134_lib")

add_library(shophook SHARED dllmain.cpp)
target_include_directories(shophook PRIVATE "${MINHOOK_DIR}/include")
target_link_libraries(shophook PRIVATE "${MINHOOK_DIR}/lib/libMinHook.x86.lib")

# No CRT name-mangling surprises for exported helpers; keep it lean.
set_target_properties(shophook PROPERTIES OUTPUT_NAME "shophook")
