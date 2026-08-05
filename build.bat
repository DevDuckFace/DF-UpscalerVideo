@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ===========================================================================
rem  DF-UpscalerVideo -- full build
rem
rem    build.bat                 configure + build Release + stage dist/
rem    build.bat debug           same, Debug configuration
rem    build.bat --clean         delete build/ and dist/ first
rem    build.bat --installer     also run Inno Setup (implied when ISCC exists)
rem    build.bat --no-installer  skip the installer step
rem    build.bat --run           launch the app when the build succeeds
rem
rem  Flags combine freely: build.bat debug --clean --run
rem ===========================================================================

cd /d "%~dp0"

set "CONFIG=Release"
set "DO_CLEAN=0"
set "DO_RUN=0"
set "WANT_INSTALLER=auto"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="release"        ( set "CONFIG=Release"        & shift & goto parse_args )
if /i "%~1"=="debug"          ( set "CONFIG=Debug"          & shift & goto parse_args )
if /i "%~1"=="--clean"        ( set "DO_CLEAN=1"            & shift & goto parse_args )
if /i "%~1"=="--run"          ( set "DO_RUN=1"              & shift & goto parse_args )
if /i "%~1"=="--installer"    ( set "WANT_INSTALLER=yes"    & shift & goto parse_args )
if /i "%~1"=="--no-installer" ( set "WANT_INSTALLER=no"     & shift & goto parse_args )
if /i "%~1"=="-h"             ( goto usage )
if /i "%~1"=="--help"         ( goto usage )
echo [ERROR] Unknown argument: %~1
goto usage
:args_done

echo.
echo ===============================================================
echo   DF-UpscalerVideo  --  %CONFIG%
echo ===============================================================
echo.

rem --- CMake ---------------------------------------------------------------
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake was not found on PATH.
    echo         Install CMake 3.24+ or add it to PATH.
    exit /b 1
)

rem --- vcpkg ---------------------------------------------------------------
if not defined VCPKG_ROOT (
    for %%D in ("C:\vcpkg" "%USERPROFILE%\vcpkg" "%~dp0vcpkg") do (
        if exist "%%~D\scripts\buildsystems\vcpkg.cmake" (
            set "VCPKG_ROOT=%%~D"
            goto vcpkg_found
        )
    )
)
:vcpkg_found
if not defined VCPKG_ROOT (
    echo [ERROR] vcpkg was not found.
    echo         Set VCPKG_ROOT, or clone vcpkg to C:\vcpkg and bootstrap it.
    exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [ERROR] VCPKG_ROOT is set to "%VCPKG_ROOT%" but the toolchain file is missing.
    exit /b 1
)
echo   vcpkg     : %VCPKG_ROOT%

rem --- Qt ------------------------------------------------------------------
if not defined QT_PREFIX (
    for /f "delims=" %%D in ('dir /b /ad /o-n "C:\Qt\6.*" 2^>nul') do (
        if not defined QT_PREFIX (
            for /f "delims=" %%K in ('dir /b /ad "C:\Qt\%%D\msvc*_64" 2^>nul') do (
                if not defined QT_PREFIX set "QT_PREFIX=C:\Qt\%%D\%%K"
            )
        )
    )
)
if not defined QT_PREFIX (
    echo [ERROR] No Qt installation was found.
    echo         Set QT_PREFIX to a Qt 6.8+ msvc kit, for example:
    echo             set QT_PREFIX=C:\Qt\6.10.1\msvc2022_64
    exit /b 1
)
if not exist "%QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] QT_PREFIX is set to "%QT_PREFIX%" but Qt6Config.cmake is missing.
    exit /b 1
)
echo   Qt        : %QT_PREFIX%

rem --- Vulkan --------------------------------------------------------------
rem ncnn's vulkan feature needs headers and an import library. The repository
rem ships a minimal SDK assembled from the vcpkg vulkan-headers/vulkan-loader
rem ports, which avoids requiring the LunarG installer (it demands admin).
if not defined VULKAN_SDK (
    if exist "%~dp0.vulkan-sdk\Include\vulkan\vulkan.h" set "VULKAN_SDK=%~dp0.vulkan-sdk"
)
if defined VULKAN_SDK (
    echo   Vulkan    : %VULKAN_SDK%
    set "VCPKG_KEEP_ENV_VARS=VULKAN_SDK"
) else (
    echo [WARN] No Vulkan SDK found. ncnn will fail to configure.
)

rem --- Visual Studio generator --------------------------------------------
set "PRESET="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "tokens=1 delims=." %%V in ('"%VSWHERE%" -latest -products * -property installationVersion 2^>nul') do (
        if "%%V"=="18" set "PRESET=vs2026"
        if "%%V"=="17" set "PRESET=vs2022"
    )
)
if not defined PRESET (
    echo [ERROR] No supported Visual Studio installation was detected.
    echo         Visual Studio 2022 or 2026 with the C++ workload is required.
    exit /b 1
)
echo   Generator : %PRESET%
echo.

set "BUILD_DIR=build\%PRESET%"
set "DIST_DIR=%~dp0dist"

rem --- clean ---------------------------------------------------------------
if "%DO_CLEAN%"=="1" (
    echo [1/5] Cleaning...
    if exist "build" rmdir /s /q "build"
    if exist "dist" rmdir /s /q "dist"
    if exist "dist-installer" rmdir /s /q "dist-installer"
) else (
    echo [1/5] Clean skipped ^(pass --clean to force^)
)

rem --- configure -----------------------------------------------------------
echo [2/5] Configuring...
cmake --preset %PRESET%
if errorlevel 1 (
    echo.
    echo [ERROR] Configure failed.
    exit /b 1
)

rem --- build ---------------------------------------------------------------
echo.
echo [3/5] Building %CONFIG%...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

rem --- install -------------------------------------------------------------
echo.
echo [4/5] Staging to dist\ ...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
cmake --install "%BUILD_DIR%" --config %CONFIG% --prefix "%DIST_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Install step failed.
    exit /b 1
)

if not exist "%DIST_DIR%\bin\ffmpeg.exe" (
    echo.
    echo [WARN] bin\ffmpeg.exe is missing. The application cannot decode or
    echo        encode without it. Place ffmpeg.exe and ffprobe.exe in bin\.
) else (
    findstr /c:"DO NOT DISTRIBUTE" "%DIST_DIR%\bin\FFMPEG-BUILD.txt" >nul 2>&1
    if not errorlevel 1 (
        echo.
        echo [WARN] The bundled FFmpeg is marked development-only. See
        echo        bin\FFMPEG-BUILD.txt. Replace it with an LGPL build before
        echo        publishing a release.
    )
)

rem --- installer -----------------------------------------------------------
echo.
set "ISCC="
for %%P in (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
) do (
    if not defined ISCC if exist %%P set "ISCC=%%~P"
)
if not defined ISCC (
    for /f "delims=" %%P in ('where ISCC 2^>nul') do (
        if not defined ISCC set "ISCC=%%P"
    )
)

if /i "%WANT_INSTALLER%"=="no" (
    echo [5/5] Installer skipped ^(--no-installer^)
    goto finished
)

if not defined ISCC (
    if /i "%WANT_INSTALLER%"=="yes" (
        echo [ERROR] Inno Setup 6 was not found but --installer was requested.
        echo         Install it from https://jrsoftware.org/isdl.php
        exit /b 1
    )
    echo [5/5] Installer skipped: Inno Setup 6 is not installed.
    echo       Install it from https://jrsoftware.org/isdl.php to enable this step.
    goto finished
)

echo [5/5] Building the installer...
"%ISCC%" /Qp "installer\DF-UpscalerVideo.iss"
if errorlevel 1 (
    echo.
    echo [ERROR] Inno Setup failed.
    exit /b 1
)

:finished
echo.
echo ===============================================================
echo   Done.
echo     Binaries : %BUILD_DIR%\stage\%CONFIG%\
echo     Staged   : dist\
if exist "dist-installer" echo     Installer: dist-installer\
echo ===============================================================

if "%DO_RUN%"=="1" (
    echo.
    echo Launching...
    start "" "%BUILD_DIR%\stage\%CONFIG%\DF-UpscalerVideo.exe"
)

exit /b 0

:usage
echo.
echo Usage: build.bat [release^|debug] [--clean] [--installer^|--no-installer] [--run]
echo.
exit /b 1
