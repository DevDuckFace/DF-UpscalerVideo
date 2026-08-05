# Qt runtime deployment.
#
# Build tree : windeployqt runs after every link so the staged executable is
#              directly runnable without Qt on PATH.
# Install    : Qt's own deployment script handles the installed layout.

function(dfu_find_windeployqt out_var)
    set(_tool "")

    if(TARGET Qt6::windeployqt)
        get_target_property(_tool Qt6::windeployqt IMPORTED_LOCATION)
    endif()

    if(NOT _tool)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        if(_qmake)
            get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
            find_program(DFU_WINDEPLOYQT_EXECUTABLE
                NAMES windeployqt windeployqt.exe
                HINTS "${_qt_bin}"
                NO_DEFAULT_PATH)
            set(_tool "${DFU_WINDEPLOYQT_EXECUTABLE}")
        endif()
    endif()

    if(_tool STREQUAL "DFU_WINDEPLOYQT_EXECUTABLE-NOTFOUND")
        set(_tool "")
    endif()

    set(${out_var} "${_tool}" PARENT_SCOPE)
endfunction()

function(dfu_deploy_qt target)
    if(NOT WIN32)
        return()
    endif()

    dfu_find_windeployqt(_windeployqt)
    if(NOT _windeployqt)
        message(WARNING
            "windeployqt was not found. The staged executable will only run "
            "with the Qt bin directory on PATH.")
        return()
    endif()

    message(STATUS "  windeployqt: ${_windeployqt}")

    # --no-compiler-runtime is correct here: the developer machine already has
    # the MSVC runtime. Shipping it is the installer's job (milestone M7).
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${_windeployqt}"
                --no-compiler-runtime
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --verbose 0
                "$<TARGET_FILE:${target}>"
        COMMENT "Staging the Qt runtime next to ${target}"
        VERBATIM)
endfunction()

function(dfu_install_app target)
    install(TARGETS ${target}
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION .)

    qt_generate_deploy_app_script(
        TARGET ${target}
        OUTPUT_SCRIPT _deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)

    # Qt's deploy support defaults QT_DEPLOY_BIN_DIR to "bin" and does not
    # derive it from CMAKE_INSTALL_BINDIR. Left alone, the Qt runtime lands in
    # <prefix>/bin while the executables sit in <prefix>, so the installed
    # application cannot find Qt6Core.dll. bin/ is also already spoken for by
    # the FFmpeg binaries.
    #
    # These run in the same install script scope as the deploy script below,
    # so the assignment is visible to it.
    install(CODE "set(QT_DEPLOY_BIN_DIR \".\")\nset(QT_DEPLOY_LIB_DIR \".\")")
    install(SCRIPT "${_deploy_script}")
endfunction()
