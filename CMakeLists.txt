cmake_minimum_required(VERSION 3.22)

project(VcabLevel VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual C++ のランタイムを EXE と VST3 の中に組み込む（利用者の PC に再頒布可能パッケージがなくても動くように）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ソースは UTF-8（日本語の文字列を含むため）
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE のサブモジュールがありません。git submodule update --init を実行してください。")
endif()
add_subdirectory(external/JUCE)

# ---- 処理の部品（JUCE に依存しない） ----
add_library(VcabLevelCore STATIC
    Source/Core/Params.cpp
    Source/Core/Engine.cpp
    Source/Core/Dsp/Gate.cpp
    Source/Core/Dsp/Compressor.cpp
    Source/Core/Dsp/Limiter.cpp
)
target_include_directories(VcabLevelCore PUBLIC Source/Core)
set_target_properties(VcabLevelCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(VcabLevelCore PRIVATE /W4)
endif()

# 使わない形式（FLAC・Ogg Vorbis・Opus、WebP）を JUCE から組み込まない（WAV と PNG だけ使う。
# 組み込むと、そのライブラリのライセンス文も配布物に添える必要がある）
set(VCABLV_UNUSED_FORMATS JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_OPUS=0 JUCE_USE_WEBP=0)

# ---- 開発用の WAV の処理ツール ----
juce_add_console_app(vcablv_render PRODUCT_NAME "vcablv_render")
target_sources(vcablv_render PRIVATE Tools/Render/Main.cpp)
target_compile_definitions(vcablv_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    ${VCABLV_UNUSED_FORMATS}
)
target_link_libraries(vcablv_render PRIVATE
    VcabLevelCore
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ---- プラグイン（VST3）と単体アプリ（EXE） ----
juce_add_plugin(VcabLevel
    COMPANY_NAME "VcabLevel"
    PRODUCT_NAME "VcabLevel"
    PLUGIN_MANUFACTURER_CODE Vcha
    PLUGIN_CODE Vclv
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    ICON_BIG Resources/Icon.png
    ICON_SMALL Resources/IconSmall.png
)

target_sources(VcabLevel PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/DisplayView.cpp
    Source/Plugin/LookAndFeel.cpp
)
# EXE 版の外枠（JUCE の外枠を写して日本語にしたもの）
target_sources(VcabLevel_Standalone PRIVATE Source/Standalone/VCStandaloneApp.cpp)

target_compile_definitions(VcabLevel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_ASIO=1
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1  # EXE 版の外枠は Source/Standalone のもの
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${VCABLV_UNUSED_FORMATS}
)

target_link_libraries(VcabLevel
    PRIVATE
        VcabLevelCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- 開発用の画面の撮影ツール ----
juce_add_console_app(vcablv_shot PRODUCT_NAME "vcablv_shot")
target_sources(vcablv_shot PRIVATE
    Tools/Shot/Main.cpp
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/DisplayView.cpp
    Source/Plugin/LookAndFeel.cpp
)
target_include_directories(vcablv_shot PRIVATE Source/Plugin)
target_compile_definitions(vcablv_shot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1  # 画面のタイマーを回すため（runDispatchLoopUntil）
    JucePlugin_Name="VcabLevel"
    ${VCABLV_UNUSED_FORMATS}
)
target_link_libraries(vcablv_shot PRIVATE
    VcabLevelCore
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
