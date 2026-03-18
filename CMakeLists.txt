cmake_minimum_required(VERSION 3.16)
project(SoldierFederate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- User should set MAK_RTI_HOME to their MAK RTI install location ---
if(NOT DEFINED ENV{MAK_RTI_HOME})
  message(FATAL_ERROR "Please set MAK_RTI_HOME to your MAK RTI install directory (e.g. C:/Program Files/MAK/RTI 4.0)")
endif()

set(MAK_RTI_HOME $ENV{MAK_RTI_HOME})
message(STATUS "Using MAK_RTI_HOME=${MAK_RTI_HOME}")

# --- Include and library paths ---
set(MAK_RTI_INCLUDE "${MAK_RTI_HOME}/include/HLA1516E")
set(MAK_RTI_LIB_DIR "${MAK_RTI_HOME}/lib")

find_path(MAK_RTI_INCLUDE_DIR
  NAMES RTI/RTIambassador.h
  HINTS "${MAK_RTI_INCLUDE}"
)

find_library(MAK_RTI_LIB
  NAMES librti1516e64
  HINTS "${MAK_RTI_LIB_DIR}"
)

find_library(MAK_RTI_LIB_DEBUG
  NAMES librti1516e64d
  HINTS "${MAK_RTI_LIB_DIR}"
)

if(NOT MAK_RTI_INCLUDE_DIR)
  message(FATAL_ERROR "Could not locate MAK RTI headers. Check MAK_RTI_HOME and the installed RTI package.")
endif()

if(NOT MAK_RTI_LIB)
  message(FATAL_ERROR "Could not locate MAK RTI library (rti1516e). Check MAK_RTI_HOME/lib and architecture (x64/x86).")
endif()

# --- Target ---
add_executable(SoldierFederate
  src/main.cpp
  src/SoldierFederate.cpp
  src/SoldierFederate.h
)

target_include_directories(SoldierFederate PRIVATE ${MAK_RTI_INCLUDE_DIR})

target_link_libraries(SoldierFederate PRIVATE
  $<$<CONFIG:Debug>:${MAK_RTI_LIB_DEBUG}>
  $<$<NOT:$<CONFIG:Debug>>:${MAK_RTI_LIB}>
  ws2_32
)
