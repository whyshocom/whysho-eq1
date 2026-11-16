cmake_minimum_required(VERSION 3.22)

# macOS: universal binary for macOS 11+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()

# Windows: link the C++ runtime statically, so the plugin loads on machines
# without the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The one place the version is written: plugin binaries, installers and CI all
# read it from here (scripts/version.sh).
project(WhyshoEQ1 VERSION 0.2.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Whysho EQ1 — a modern, feature-complete equalizer plugin (VST3 / AU / SA)
#  Part of the Whysho plugin suite. Independent, self-contained build.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---- JUCE -----------------------------------------------------------------
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# ---- Monocypher (Ed25519: verifies whysho.com's signed license answers) ----
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        0d85f98c9d9b0227e42cf795cb527dff372b40a4   # 4.0.2
)
FetchContent_GetProperties(monocypher)
if(NOT monocypher_POPULATED)
    FetchContent_Populate(monocypher)   # sources only; it has no CMake build of its own
endif()
add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional)

# ---------------------------------------------------------------------------
juce_add_plugin(WhyshoEQ1
    COMPANY_NAME                "whysho"
    COMPANY_WEBSITE             "https://whysho.com"
    BUNDLE_ID                   "com.whysho.eq1"
    PLUGIN_MANUFACTURER_CODE    Wsho
    PLUGIN_CODE                 Weq1
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "whysho EQ1"
    DESCRIPTION                 "whysho EQ1 — modern dynamic / linear-phase equalizer"
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE)

juce_generate_juce_header(WhyshoEQ1)

set(WHYSHO_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/PresetManager.cpp
    src/dsp/EqEngine.cpp
    src/dsp/SpectrumAnalyzer.cpp
    src/licensing/AccountClient.cpp
    src/licensing/LicenseToken.cpp
    src/licensing/LicenseStore.cpp
    src/licensing/LicenseService.cpp
    src/licensing/MachineId.cpp
    src/gui/AnalyzerComponent.cpp
    src/gui/BandEditor.cpp
    src/gui/LicenseDialog.cpp
    src/gui/WhyshoLookAndFeel.cpp)

target_sources(WhyshoEQ1 PRIVATE ${WHYSHO_SOURCES})

target_compile_definitions(WhyshoEQ1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_include_directories(WhyshoEQ1 PRIVATE src)

target_link_libraries(WhyshoEQ1 PRIVATE
    monocypher
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Optional: offscreen GUI snapshot tool (local preview only, never shipped).
#  Build with -DWHYSHO_BUILD_SNAPSHOT=ON, run under xvfb-run.
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_SNAPSHOT "Build offscreen GUI snapshot tool" OFF)
if(WHYSHO_BUILD_SNAPSHOT)
    juce_add_console_app(WhyshoEQ1_Snapshot PRODUCT_NAME "WhyshoEQ1Snapshot")
    juce_generate_juce_header(WhyshoEQ1_Snapshot)
    target_sources(WhyshoEQ1_Snapshot PRIVATE tools/Snapshot.cpp ${WHYSHO_SOURCES})
    target_include_directories(WhyshoEQ1_Snapshot PRIVATE src)
    target_compile_definitions(WhyshoEQ1_Snapshot PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_CHECK_MEMORY_LEAKS=0 NDEBUG=1)
    target_link_libraries(WhyshoEQ1_Snapshot PRIVATE
        monocypher
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_gui_basics juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
#  Tests (licensing rules, DSP stress, undo / presets / A-B). `ctest --test-dir build` after building.
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_TESTS "Build unit tests" ON)
if(WHYSHO_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(WhyshoEQ1_Tests PRODUCT_NAME "WhyshoEQ1Tests")
    target_sources(WhyshoEQ1_Tests PRIVATE
        tests/LicenseTests.cpp
        tests/DspTests.cpp
        tests/WorkflowTests.cpp
        src/Parameters.cpp
        src/PresetManager.cpp
        src/dsp/EqEngine.cpp
        src/licensing/AccountClient.cpp
        src/licensing/LicenseToken.cpp
        src/licensing/LicenseStore.cpp
        src/licensing/MachineId.cpp)
    target_include_directories(WhyshoEQ1_Tests PRIVATE src)
    target_compile_definitions(WhyshoEQ1_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1)
    target_link_libraries(WhyshoEQ1_Tests PRIVATE
        monocypher
        juce::juce_core juce::juce_events juce::juce_cryptography
        juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME unit COMMAND WhyshoEQ1_Tests)
endif()

# ---------------------------------------------------------------------------
#  Linux needs libcurl for JUCE's HTTPS (Windows/macOS use native networking).
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(WhyshoEQ1 PRIVATE CURL::libcurl)
    foreach(extra WhyshoEQ1_Snapshot WhyshoEQ1_Tests)
        if(TARGET ${extra})
            target_link_libraries(${extra} PRIVATE CURL::libcurl)
        endif()
    endforeach()
endif()
