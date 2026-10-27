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

project(DUST_LOOPS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

add_subdirectory(JUCE)

juce_add_binary_data(DustLoopsAssets SOURCES assets/dpr_logo_horizontal.png)

set(DL_SOURCES
    Source/Core/Params.cpp
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp)

set(DL_COMMON_DEFS
    DUST_PRODUCT_NAME="${DL_PRODUCT_NAME}"
    DUST_COMPANY_NAME="${DL_COMPANY_NAME}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

set(DL_COMMON_LIBS
    DustLoopsAssets
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
target_sources(DustLoopsTests PRIVATE ${DL_SOURCES} tests/TestMain.cpp)
target_include_directories(DustLoopsTests PRIVATE Source)
target_compile_definitions(DustLoopsTests PRIVATE ${DL_COMMON_DEFS}
    JucePlugin_Name="${DL_PRODUCT_NAME}" DUST_LOOPS_FX=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(DustLoopsTests PRIVATE ${DL_COMMON_LIBS} juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME dsp COMMAND DustLoopsTests)
