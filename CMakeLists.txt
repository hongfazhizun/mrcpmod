cmake_minimum_required (VERSION 3.11)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE "")
project (mrcpmod)

include_directories(src)
include_directories(src/libs)
include_directories(src/recog)
include_directories(src/synth)
include_directories(src/tencent/asr/include)
include_directories(src/tencent/tts/include)
include_directories(src/tencent/common/include)

find_package(PkgConfig REQUIRED)

pkg_search_module(unimrcp REQUIRED unimrcpserver)
include_directories(${unimrcp_INCLUDE_DIRS})
LINK_DIRECTORIES(${unimrcp_LIBRARY_DIRS})

set(MODULE_NAME recog)
file(GLOB_RECURSE SRC_LIST src/recog/*.h src/recog/*.cpp)
ADD_LIBRARY(${MODULE_NAME} SHARED ${SRC_LIST})
TARGET_LINK_LIBRARIES(${MODULE_NAME} ${unimrcp_LIBRARIES} tencent common)
set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")

set(MODULE_NAME synth)
file(GLOB_RECURSE SRC_LIST src/synth/*.h src/synth/*.cpp)
ADD_LIBRARY(${MODULE_NAME} SHARED ${SRC_LIST} ${TTS_SRC_FILES})
TARGET_LINK_LIBRARIES(${MODULE_NAME} ${unimrcp_LIBRARIES} tencent common)
set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")

set(MODULE_NAME tencent)
file(GLOB_RECURSE SRC_LIST src/tencent/*.h src/tencent/*.cpp src/tencent/*.c)
add_library(${MODULE_NAME} ${SRC_LIST})
TARGET_LINK_LIBRARIES(${MODULE_NAME} boost_chrono)

set(MODULE_NAME common)
file(GLOB_RECURSE SRC_LIST src/libs/*.h src/libs/*.cpp src/libs/*.c)
add_library(${MODULE_NAME} ${SRC_LIST})
TARGET_LINK_LIBRARIES(${MODULE_NAME} boost_random boost_program_options boost_system boost_filesystem boost_thread pthread crypto ssl)
