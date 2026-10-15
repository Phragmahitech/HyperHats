cmake_minimum_required(VERSION 3.22)

# --- Réglages ----------------------------------------------------------------
set(HYPERHATS_PRODUCT_NAME "HyperHats")          # nom du plugin dans le DAW, et nom du fichier .vst3
set(HYPERHATS_COMPANY_NAME "Phragma")            # nom affiché comme éditeur dans le DAW
set(HYPERHATS_BUNDLE_ID    "com.phragma.hyperhats")
# -----------------------------------------------------------------------------
# Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE : ce sont eux (et pas le nom)
# qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(HyperHats VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(HYPERHATS_BUILD_TESTS "Compiler les tests" OFF)
set(HYPERHATS_FONT_DIR "" CACHE PATH "Dossier contenant déjà les polices (sinon elles sont téléchargées)")

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Polices (licence OFL, voir Assets/) -------------------------------------
# Michroma et IBM Plex Mono, prises dans le dépôt google/fonts à un commit fixe et vérifiées par SHA-256.
set(HYPERHATS_FONTS_COMMIT 66928797f271fed5ee428544e7229685e2d90127)
set(HYPERHATS_FONTS
    "ofl/michroma/Michroma-Regular.ttf|b62301163788bc5b7f8fcac0b74b184e34e1827e577b499ecb724da065098f87"
    "ofl/ibmplexmono/IBMPlexMono-Regular.ttf|6a3412f058c7d8dfd9170c41e85ade48e5156ecb89356110ca57a0a27734af46"
    "ofl/ibmplexmono/IBMPlexMono-Medium.ttf|a9b4c49bb299e05b5f6c481e7fb5e78943d2793249a0c8874ab574a2d1ea6755")

set(HYPERHATS_FONT_FILES "")
foreach(entry IN LISTS HYPERHATS_FONTS)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 font_path)
    list(GET parts 1 font_hash)
    get_filename_component(font_name "${font_path}" NAME)

    if(HYPERHATS_FONT_DIR)
        set(font_file "${HYPERHATS_FONT_DIR}/${font_name}")
        file(SHA256 "${font_file}" actual_hash)
        if(NOT actual_hash STREQUAL font_hash)
            message(FATAL_ERROR "Police inattendue : ${font_file}")
        endif()
    else()
        set(font_file "${CMAKE_BINARY_DIR}/fonts/${font_name}")
        if(NOT EXISTS "${font_file}")
            file(DOWNLOAD
                "https://raw.githubusercontent.com/google/fonts/${HYPERHATS_FONTS_COMMIT}/${font_path}"
                "${font_file}"
                EXPECTED_HASH SHA256=${font_hash}
                TLS_VERIFY ON
                STATUS download_status)
            list(GET download_status 0 download_code)
            if(NOT download_code EQUAL 0)
                message(FATAL_ERROR "Téléchargement impossible : ${font_path} (${download_status})")
            endif()
        endif()
    endif()

    list(APPEND HYPERHATS_FONT_FILES "${font_file}")
endforeach()

# --- Plugin ------------------------------------------------------------------
juce_add_plugin(HyperHats
    PRODUCT_NAME                "${HYPERHATS_PRODUCT_NAME}"
    COMPANY_NAME                "${HYPERHATS_COMPANY_NAME}"
    BUNDLE_ID                   "${HYPERHATS_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Hyht
    FORMATS                     VST3
    VST3_CATEGORIES             Instrument Drum
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(HyperHats PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_binary_data(HyperHatsAssets SOURCES ${HYPERHATS_FONT_FILES})
set_target_properties(HyperHatsAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(HyperHats PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    HYPERHATS_VERSION="${PROJECT_VERSION}")

target_link_libraries(HyperHats
    PRIVATE
        juce::juce_audio_processors
        HyperHatsAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(HYPERHATS_BUILD_TESTS)
    enable_testing()

    # Moteur seul, sans JUCE
    add_executable(HyperHatsEngineTests Tests/EngineTests.cpp)
    target_include_directories(HyperHatsEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND HyperHatsEngineTests)

    # Plugin complet (paramètres, MIDI, état, interface)
    juce_add_console_app(HyperHatsPluginTests PRODUCT_NAME "HyperHatsPluginTests")
    target_sources(HyperHatsPluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(HyperHatsPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        HYPERHATS_VERSION="${PROJECT_VERSION}")
    target_link_libraries(HyperHatsPluginTests PRIVATE
        juce::juce_audio_processors
        HyperHatsAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND HyperHatsPluginTests)

    # Rendu d'extraits audio et d'une capture de l'interface (Tools/Render.cpp)
    juce_add_console_app(HyperHatsRender PRODUCT_NAME "HyperHatsRender")
    target_sources(HyperHatsRender PRIVATE
        Tools/Render.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(HyperHatsRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        HYPERHATS_VERSION="${PROJECT_VERSION}")
    target_link_libraries(HyperHatsRender PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        HyperHatsAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
