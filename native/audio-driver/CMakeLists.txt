cmake_minimum_required(VERSION 3.20)
project(PulseAudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

# HAL plugin is a bundle (loadable module)
add_library(PulseAudio MODULE
    src/plugin.cpp
    src/device.cpp
    src/ring-buffer.cpp
)

target_include_directories(PulseAudio PRIVATE src)

# macOS frameworks
target_link_libraries(PulseAudio PRIVATE
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

# Bundle configuration
set_target_properties(PulseAudio PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/PulseAudio.driver/Contents/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "PulseAudio"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.pulse.audio.driver"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    PREFIX ""
    SUFFIX ""
)

# CLI helper for aggregate device management (used by Electron via execSync)
add_executable(pulse-audio-helper src/helper.cpp)
target_link_libraries(pulse-audio-helper PRIVATE
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework AudioToolbox"
)
set_target_properties(pulse-audio-helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Install to HAL plugins directory (for development only)
install(TARGETS PulseAudio
    LIBRARY DESTINATION "/Library/Audio/Plug-Ins/HAL"
)
