cmake_minimum_required(VERSION 3.24)

# The application name lives in exactly two places: here, and kAppName in
# src/core/AppInfo.h. Renaming the product means editing both and nothing else.
project(DF-UpscalerVideo
    VERSION 0.1.0
    DESCRIPTION "GPU video upscaling, frame interpolation and restoration"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Deploy)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything lands next to the executable so the app can resolve bin/ and
# models/ relative to applicationDirPath() in the build tree exactly as it
# will after installation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage")

# 6.8 rather than the specification's 6.5 floor: the light/dark switch uses
# QStyleHints::setColorScheme/unsetColorScheme, which landed in 6.8. That is
# still "6.5 LTS or newer" per the technology stack.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ncnn CONFIG REQUIRED)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# dfu_core -- every line of application code.
#
# Both executables and (from M2) the test binaries link this, so the sources
# are compiled once and the entry points stay thin.
# ---------------------------------------------------------------------------
qt_add_library(dfu_core STATIC
    src/core/AppInfo.h
    src/core/Logging.h
    src/core/Logging.cpp
    src/core/FrameBuffer.h
    src/core/FrameBuffer.cpp
    src/core/FramePool.h
    src/core/FramePool.cpp
    src/core/RingBuffer.h
    src/core/JobSpec.h
    src/core/JobSpec.cpp

    src/media/FfmpegPaths.h
    src/media/FfmpegPaths.cpp
    src/media/MediaProbe.h
    src/media/MediaProbe.cpp
    src/media/FfmpegDecoder.h
    src/media/FfmpegDecoder.cpp
    src/media/FfmpegEncoder.h
    src/media/FfmpegEncoder.cpp

    src/engine/IFrameProcessor.h
    src/engine/PassthroughProcessor.h
    src/engine/PassthroughProcessor.cpp
    src/engine/VulkanContext.h
    src/engine/VulkanContext.cpp
    src/engine/RealEsrganNcnn.h
    src/engine/RealEsrganNcnn.cpp
    src/engine/RifeNcnn.h
    src/engine/RifeNcnn.cpp
    src/engine/RifeWarpLayer.h
    src/engine/RifeWarpLayer.cpp

    src/pipeline/JobRunner.h
    src/pipeline/JobRunner.cpp
    src/pipeline/JobQueue.h
    src/pipeline/JobQueue.cpp

    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/LogDock.h
    src/ui/LogDock.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/JobQueueWidget.h
    src/ui/JobQueueWidget.cpp
    src/ui/SettingsPanel.h
    src/ui/SettingsPanel.cpp
)

target_include_directories(dfu_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(dfu_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ncnn
)

target_compile_definitions(dfu_core PUBLIC
    DFU_VERSION_STRING="${PROJECT_VERSION}"
    QT_DISABLE_DEPRECATED_BEFORE=0x060500
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

function(dfu_apply_compile_options target)
    if(MSVC)
        # /arch:AVX and no higher: the target CPU (i7-3770, Ivy Bridge) has AVX
        # but not AVX2. See PROMPT section 4.
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
            /EHsc
            /MP
            /arch:AVX
        )
    endif()
endfunction()

dfu_apply_compile_options(dfu_core)

# ---------------------------------------------------------------------------
# Executables.
#
# Windows makes an application choose one subsystem at link time. A GUI
# subsystem binary launched from a shell returns immediately and reports no
# exit code; a console subsystem binary flashes a console window when launched
# from Explorer. Neither is acceptable on its own, so the same entry point is
# linked twice -- the pattern devenv.exe / devenv.com uses.
# ---------------------------------------------------------------------------
# Windows resource script: application icon plus the version block Explorer
# shows in the file properties dialog. Generated per executable because
# OriginalFilename differs between the two.
set(DFU_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
string(REPLACE "/" "\\\\" DFU_ICON_PATH "${DFU_ICON_PATH}")

set(DFU_ORIGINAL_FILENAME "${PROJECT_NAME}.exe")
configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app_gui.rc" @ONLY)

set(DFU_ORIGINAL_FILENAME "${PROJECT_NAME}-cli.exe")
configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app_cli.rc" @ONLY)

qt_add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    resources/app.qrc
    "${CMAKE_CURRENT_BINARY_DIR}/app_gui.rc"
)
target_link_libraries(${PROJECT_NAME} PRIVATE dfu_core)
dfu_apply_compile_options(${PROJECT_NAME})

qt_add_executable(${PROJECT_NAME}-cli
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/app_cli.rc"
)
set_target_properties(${PROJECT_NAME}-cli PROPERTIES WIN32_EXECUTABLE OFF)
target_compile_definitions(${PROJECT_NAME}-cli PRIVATE DFU_CONSOLE_BUILD)
target_link_libraries(${PROJECT_NAME}-cli PRIVATE dfu_core)
dfu_apply_compile_options(${PROJECT_NAME}-cli)

# Both executables share one staging directory, so the Qt runtime only needs
# to be deployed once.
# The application resolves models relative to its own directory, so the build
# tree needs them too -- otherwise the AI tier only works after installing.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
    COMMENT "Staging model weights"
    VERBATIM)

dfu_deploy_qt(${PROJECT_NAME})
dfu_install_app(${PROJECT_NAME})
install(TARGETS ${PROJECT_NAME}-cli RUNTIME DESTINATION .)

# Qt's deployment script only resolves Qt's own libraries. Anything else the
# executables link against dynamically -- vcpkg dependencies, for instance --
# has to be installed explicitly, or the packaged application dies at startup
# with a missing-DLL dialog.
#
# The presets use a static vcpkg triplet, so in the normal case these lists
# contain only Qt libraries that the deploy script installs anyway. The rules
# exist so that a dynamic triplet still produces a working install tree.
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION .)
    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}-cli> DESTINATION .)
endif()

# The install tree is the exact layout the installer packages, so ffmpeg and
# the model weights come along. Both directories are allowed to be empty at
# this stage; the installer script reports what is missing.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/"
    DESTINATION bin
    FILES_MATCHING
        PATTERN "*.exe"
        PATTERN "FFMPEG-BUILD.txt")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models/"
    DESTINATION models
    PATTERN "README.md" EXCLUDE)

message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  Qt        : ${Qt6_VERSION} (${Qt6_DIR})")
message(STATUS "  Compiler  : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Stage dir : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
