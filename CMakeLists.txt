cmake_minimum_required(VERSION 3.22)

if(APPLE)
  project(reaper_reabeat VERSION 2.0.0 LANGUAGES C CXX OBJCXX)
else()
  project(reaper_reabeat VERSION 2.0.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Dependencies ---

# REAPER SDK
if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/reaper-sdk/sdk/reaper_plugin.h")
  set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/vendor/reaper-sdk/sdk")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/vendor/reaper-sdk/reaper_plugin.h")
  set(REAPER_SDK_DIR "${CMAKE_SOURCE_DIR}/vendor/reaper-sdk")
else()
  message(FATAL_ERROR "REAPER SDK not found. Run: git submodule update --init")
endif()

# WDL
set(WDL_DIR "${CMAKE_SOURCE_DIR}/vendor/WDL/WDL")
if(NOT EXISTS "${WDL_DIR}/wdltypes.h")
  message(FATAL_ERROR "WDL not found. Run: git submodule update --init")
endif()

# JUCE
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/vendor/JUCE/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found. Run: git submodule update --init")
endif()
add_subdirectory(vendor/JUCE EXCLUDE_FROM_ALL)

# ONNX Runtime
set(ORT_DIR "${CMAKE_SOURCE_DIR}/vendor/onnxruntime")
if(NOT EXISTS "${ORT_DIR}/include/onnxruntime_cxx_api.h")
  message(WARNING "ONNX Runtime not found at ${ORT_DIR}. Inference will not be available.")
  set(ORT_FOUND FALSE)
else()
  set(ORT_FOUND TRUE)
  add_library(onnxruntime SHARED IMPORTED)
  if(APPLE)
    set_target_properties(onnxruntime PROPERTIES
      IMPORTED_LOCATION "${ORT_DIR}/lib/libonnxruntime.dylib"
    )
  elseif(WIN32)
    set_target_properties(onnxruntime PROPERTIES
      IMPORTED_LOCATION "${ORT_DIR}/lib/onnxruntime.dll"
      IMPORTED_IMPLIB "${ORT_DIR}/lib/onnxruntime.lib"
    )
  else()
    set_target_properties(onnxruntime PROPERTIES
      IMPORTED_LOCATION "${ORT_DIR}/lib/libonnxruntime.so"
    )
  endif()
  set_target_properties(onnxruntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_DIR}/include"
  )
endif()

# PocketFFT (header-only)
set(POCKETFFT_DIR "${CMAKE_SOURCE_DIR}/vendor")

# --- Binary resources ---

juce_add_binary_data(ReaBeatResources SOURCES
  resources/icon.png
)

# --- Main target ---

file(GLOB REABEAT_SOURCES "src/*.cpp")
add_library(reaper_reabeat SHARED ${REABEAT_SOURCES})

# JUCE modules + resources
target_link_libraries(reaper_reabeat PRIVATE
  juce::juce_gui_basics
  juce::juce_gui_extra
  juce::juce_audio_formats
  ReaBeatResources
)

if(ORT_FOUND)
  target_link_libraries(reaper_reabeat PRIVATE onnxruntime)
  target_compile_definitions(reaper_reabeat PRIVATE REABEAT_HAS_ONNX=1)
endif()

target_include_directories(reaper_reabeat PRIVATE
  ${REAPER_SDK_DIR}
  ${WDL_DIR}
  ${WDL_DIR}/swell
  ${POCKETFFT_DIR}
  src
)

target_compile_definitions(reaper_reabeat PRIVATE
  JUCE_STANDALONE_APPLICATION=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  WDL_NO_DEFINE_MINMAX
  REABEAT_VERSION="${PROJECT_VERSION}"
  $<$<NOT:$<PLATFORM_ID:Windows>>:SWELL_PROVIDED_BY_APP>
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
  $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
  $<$<CONFIG:Debug>:REABEAT_DEBUG>
)

# Compiler warnings
if(MSVC)
  target_compile_options(reaper_reabeat PRIVATE /W3)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(reaper_reabeat PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  )
endif()

# --- Platform-specific ---

if(APPLE)
  # Determine architecture for output name
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(ARCH_NAME "arm64")
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(ARCH_NAME "x86_64")
  else()
    set(ARCH_NAME "${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  set_target_properties(reaper_reabeat PROPERTIES
    SUFFIX ".dylib"
    PREFIX ""
    OUTPUT_NAME "reaper_reabeat-${ARCH_NAME}"
  )

  target_link_libraries(reaper_reabeat PRIVATE
    "-framework AppKit"
    "-framework CoreFoundation"
  )

  # SWELL mod stub (REAPER provides SWELL implementation)
  target_sources(reaper_reabeat PRIVATE
    ${WDL_DIR}/swell/swell-modstub.mm
  )

elseif(WIN32)
  set_target_properties(reaper_reabeat PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
    OUTPUT_NAME "reaper_reabeat-x64"
  )

elseif(UNIX)
  set_target_properties(reaper_reabeat PROPERTIES
    SUFFIX ".so"
    PREFIX ""
    OUTPUT_NAME "reaper_reabeat-x86_64"
  )
  target_link_libraries(reaper_reabeat PRIVATE dl pthread)

  # SWELL mod stub for Linux (REAPER provides SWELL implementation)
  target_sources(reaper_reabeat PRIVATE
    ${WDL_DIR}/swell/swell-modstub-generic.cpp
  )
endif()

# --- Install ---

if(APPLE)
  set(REAPER_PLUGINS_DIR "$ENV{HOME}/Library/Application Support/REAPER/UserPlugins")
elseif(WIN32)
  set(REAPER_PLUGINS_DIR "$ENV{APPDATA}/REAPER/UserPlugins")
else()
  set(REAPER_PLUGINS_DIR "$ENV{HOME}/.config/REAPER/UserPlugins")
endif()

install(TARGETS reaper_reabeat DESTINATION "${REAPER_PLUGINS_DIR}")
