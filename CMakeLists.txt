cmake_minimum_required(VERSION 3.22)

# ---- Identity: the ONE place to rename the plugin ----------------------------------------------
# Everything else (C++ constants, preset folder, window title) is derived from these.
set(DL_PRODUCT_NAME   "Dust Loops")
set(DL_COMPANY_NAME   "Dust Planet Records")
set(DL_COMPANY_URL    "https://www.dustplanetrecords.co.uk")
set(DL_MANUFACTURER   Dplp)          # 4-char AU manufacturer code (Dust Synth uses Dplr)
set(DL_CODE_INSTR     Dlp1)          # AU subtype, instrument
set(DL_CODE_FX        Dlx1)          # AU subtype, effect
set(DL_BUNDLE_ID      uk.co.dustplanetrecords.dustloops)
# -------------------------------------------------------------------------------------------------

project(DUST_LOOPS VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

add_subdirectory(JUCE)

juce_add_binary_data(DustLoopsAssets SOURCES assets/dpr_logo_horizontal.png)

# Factory presets: every JSON file under presets/ is built into the plugin.
file(GLOB_RECURSE DL_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json")
juce_add_binary_data(DustLoopsPresets NAMESPACE PresetData HEADER_NAME PresetData.h SOURCES ${DL_PRESET_FILES})

set(DL_SOURCES
    Source/Core/Params.cpp
    Source/Presets/PresetManager.cpp
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Gui/Art.cpp
    Source/Gui/DustLookAndFeel.cpp
    Source/Gui/SegmentDisplay.cpp
    Source/Gui/VuMeter.cpp
    Source/Gui/DeckParts.cpp
    Source/Gui/Panels.cpp
    Source/Gui/PedalBoardView.cpp
    Source/Gui/SynthPanels.cpp
    Source/Gui/PresetBrowser.cpp)

set(DL_COMMON_DEFS
    DUST_PRODUCT_NAME="${DL_PRODUCT_NAME}"
    DUST_COMPANY_NAME="${DL_COMPANY_NAME}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

set(DL_COMMON_LIBS
    DustLoopsAssets
    DustLoopsPresets
    juce::juce_audio_utils
    juce::juce_dsp)

# Both plugins compile the same sources; DUST_LOOPS_FX switches the synth out.
function(dust_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;CODE;BUNDLE_SUFFIX;FX;AU_TYPE;DESCRIPTION" "FORMATS" ${ARGN})
    if(ARG_FX)
        set(is_synth FALSE)
    else()
        set(is_synth TRUE)
    endif()
    juce_add_plugin(${target}
        COMPANY_NAME "${DL_COMPANY_NAME}"
        COMPANY_WEBSITE "${DL_COMPANY_URL}"
        BUNDLE_ID ${DL_BUNDLE_ID}${ARG_BUNDLE_SUFFIX}
        PLUGIN_MANUFACTURER_CODE ${DL_MANUFACTURER}
        PLUGIN_CODE ${ARG_CODE}
        FORMATS ${ARG_FORMATS}
        PRODUCT_NAME "${ARG_PRODUCT_NAME}"
        DESCRIPTION "${ARG_DESCRIPTION}"
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE ${ARG_AU_TYPE}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Dust Loops records audio input onto its tape loops."
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)
    target_sources(${target} PRIVATE ${DL_SOURCES})
    target_compile_definitions(${target} PUBLIC ${DL_COMMON_DEFS} DUST_LOOPS_FX=$<BOOL:${ARG_FX}>)
    target_include_directories(${target} PRIVATE Source)
    target_link_libraries(${target}
        PRIVATE ${DL_COMMON_LIBS}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()

dust_add_plugin(DustLoops
    PRODUCT_NAME "${DL_PRODUCT_NAME}"
    CODE ${DL_CODE_INSTR}
    BUNDLE_SUFFIX ""
    FX FALSE
    AU_TYPE kAudioUnitType_MusicDevice
    DESCRIPTION "Tape-loop drone instrument"
    FORMATS AU Standalone)

dust_add_plugin(DustLoopsFX
    PRODUCT_NAME "${DL_PRODUCT_NAME} FX"
    CODE ${DL_CODE_FX}
    BUNDLE_SUFFIX "fx"
    FX TRUE
    AU_TYPE kAudioUnitType_MusicEffect
    DESCRIPTION "Tape-loop drone effect"
    FORMATS AU)

# ---- Unit tests: the real processor, driven offline ---------------------------------------------
juce_add_console_app(DustLoopsTests PRODUCT_NAME "DustLoopsTests")
target_sources(DustLoopsTests PRIVATE ${DL_SOURCES} tests/TestMain.cpp tests/TapeTests.cpp tests/PedalTests.cpp
    tests/SynthTests.cpp tests/PresetTests.cpp)
target_include_directories(DustLoopsTests PRIVATE Source)
target_compile_definitions(DustLoopsTests PRIVATE ${DL_COMMON_DEFS}
    JucePlugin_Name="${DL_PRODUCT_NAME}" DUST_LOOPS_FX=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(DustLoopsTests PRIVATE ${DL_COMMON_LIBS} juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME dsp COMMAND DustLoopsTests)

# ---- UI snapshot tool (not part of the plugin) ---------------------------------------------------
juce_add_console_app(DustSnapshot PRODUCT_NAME "DustSnapshot")
target_sources(DustSnapshot PRIVATE ${DL_SOURCES} tools/Snapshot.cpp)
target_include_directories(DustSnapshot PRIVATE Source)
target_compile_definitions(DustSnapshot PRIVATE ${DL_COMMON_DEFS}
    JucePlugin_Name="${DL_PRODUCT_NAME}" DUST_LOOPS_FX=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(DustSnapshot PRIVATE ${DL_COMMON_LIBS} juce::juce_recommended_config_flags)
set_target_properties(DustSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---- Preset render tool: renders every factory preset to WAV and reports levels -------------------
#   cmake --build build --target DustRender && build/DustRender_artefacts/Release/DustRender [--trims tools/preset_trims.json]
juce_add_console_app(DustRender PRODUCT_NAME "DustRender")
target_sources(DustRender PRIVATE ${DL_SOURCES} tools/Render.cpp)
target_include_directories(DustRender PRIVATE Source)
target_compile_definitions(DustRender PRIVATE ${DL_COMMON_DEFS}
    JucePlugin_Name="${DL_PRODUCT_NAME}" DUST_LOOPS_FX=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(DustRender PRIVATE ${DL_COMMON_LIBS} juce::juce_recommended_config_flags)
set_target_properties(DustRender PROPERTIES EXCLUDE_FROM_ALL TRUE)
