cmake_minimum_required(VERSION 3.16)
project(GoldSaucer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Qt6 path
set(CMAKE_PREFIX_PATH "C:\\Qt\\6.10.1\\msvc2022_64" ${CMAKE_PREFIX_PATH})
set(Qt6_DIR "C:\\Qt\\6.10.1\\msvc2022_64\\lib\\cmake\\Qt6")

# Find Qt6 (need Xml, Svg, Core5Compat, and Network for WebSocket)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Xml Svg Core5Compat Network)

# Set up FF7TK
set(FF7TK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ff7tk-1.3.1.3")
set(FF7TK_INCLUDE_DIR "${FF7TK_ROOT}/include")
set(FF7TK_LIB_DIR "${FF7TK_ROOT}/lib")
set(FF7TK_CMAKE_DIR "${FF7TK_ROOT}/lib/cmake")

# Add FF7TK CMake to search path
list(APPEND CMAKE_PREFIX_PATH "${FF7TK_CMAKE_DIR}")

# Set up ZLIB (use our built version)
set(ZLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../zlib-1.3.1")
set(ZLIB_INCLUDE_DIR "${ZLIB_ROOT}")
set(ZLIB_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../zlib-1.3.1/build/Release/zlibstatic.lib")

# Create ZLIB target
add_library(ZLIB::ZLIB STATIC IMPORTED)
set_target_properties(ZLIB::ZLIB PROPERTIES
    IMPORTED_LOCATION "${ZLIB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}"
)

# Find FF7TK
find_package(ff7tk REQUIRED)

# Include directories
include_directories(${FF7TK_INCLUDE_DIR})
include_directories(${ZLIB_INCLUDE_DIR})
include_directories("${ZLIB_ROOT}/build")

# Enable Qt MOC/UIC/RCC before defining targets
set(CMAKE_AUTOMOC ON)

# Add executable (simple GUI version)
add_executable(GoldSaucer_GUI WIN32
    src/main_gui.cpp
    src/Randomizer.cpp
    src/EnemyRandomizer.cpp
    src/EnemyDatabase.cpp
    src/ShopRandomizer.cpp
    src/KernelBinParser.cpp
    src/FieldPickupRandomizer_ff7tk.cpp
    src/ulgp_lgp_writer.cpp
    src/FF7String.cpp
    src/ScriptManager.cpp
    src/FieldTextManager.cpp
    src/MakouLgpManager.cpp
    src/StartingEquipmentRandomizer.cpp
    src/Config.cpp
    src/TextEncoder.cpp
    src/TextReplacementConfig.cpp
    src/TextReplacementManager.cpp
    src/NameGenerator.cpp
    src/KernelBinValidator.cpp
    src/UserFeedback.cpp
    src/GUI/SimpleMainWindow.cpp
    src/GUI/SimpleMainWindow.h
)

# Link libraries
target_link_libraries(GoldSaucer_GUI 
    Qt6::Core 
    Qt6::Widgets
    Qt6::Xml
    Qt6::Svg
    Qt6::Core5Compat
    Qt6::Network
    ff7tk::ff7tk
    ff7tk::ff7tkData
    ff7tk::ff7tkFormats
    ff7tk::ff7tkUtils
    ZLIB::ZLIB
)

# Add ff7tk libraries (if available)
if(EXISTS "${FF7TK_LIB_DIR}")
    target_link_directories(GoldSaucer_GUI PRIVATE ${FF7TK_LIB_DIR})
endif()
