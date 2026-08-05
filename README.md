# DF-UpscalerVideo

Native Windows desktop application for AI video enhancement: upscaling, frame
interpolation and restoration. FFmpeg runs as an external process over raw
frame pipes; neural network inference is linked in-process via ncnn + Vulkan.

## Status

Working end to end: add a video, choose restoration and upscale settings, press
Start, get a finished file with its audio intact.

| Area | State |
|---|---|
| Probe, decode, encode over pipes | Done |
| Job queue, progress, ETA, pause, stop | Done |
| Restoration: deinterlace, deblock, denoise, deband | Done (FFmpeg filters) |
| Enhancement: sharpen, brightness, contrast, saturation | Done (after upscaling) |
| Upscaling | Lanczos/Spline, plus Real-ESRGAN in-process on the GPU |
| Frame interpolation (RIFE) | Done, 2x/3x/4x |
| Chunking and resume | Not yet |
| Preview with before/after slider | Not yet |

### Vulkan without the LunarG installer

`ncnn[vulkan]` needs Vulkan headers and an import library at build time. The
official LunarG SDK installer demands administrator rights and refuses to
elevate from a command line, so instead the repository carries a minimal SDK
in `.vulkan-sdk/`, assembled from the vcpkg `vulkan-headers` and
`vulkan-loader` ports. `build.bat` points `VULKAN_SDK` at it automatically.
CMake's `FindVulkan` accepts it, and nothing needs installing.

To rebuild that directory from scratch:

```bash
vcpkg install vulkan-headers:x64-windows-static-md vulkan-loader:x64-windows-static-md
```

Then copy `include/vulkan` to `.vulkan-sdk/Include/vulkan` and
`lib/vulkan-1.lib` to `.vulkan-sdk/Lib/`.

Running the AI tier still needs a working Vulkan *driver*; when none is
present the settings panel disables the Real-ESRGAN option and says why,
rather than silently falling back to Lanczos.

## Using it

1. **Add Files**, or drag videos onto the window, or drop them on the
   executable.
2. Select a row. The **Settings** dock on the right edits that job; selecting
   several rows edits all of them.
3. Press **Start** (F9). **Pause** is Space, **Stop** is Escape.

Stop discards the partial output rather than leaving a file that looks
finished but is not.

### Filter order, and why

Restoration runs **before** upscaling, enhancement **after**:

```
deblock -> denoise -> deband -> [UPSCALE] -> sharpen -> colour -> [INTERPOLATE] -> encode
```

Deblocking comes first so the denoiser does not read compression block edges as
detail worth keeping. Sharpening comes last because sharpening before the
upscaler would have the network amplify the halos it introduces. Interpolation
runs after upscaling, not before: interpolating first would put twice as many
frames through the upscaler, and the upscaler dominates the runtime.

### Command line

The console binary runs the same pipeline headlessly:

```bash
DF-UpscalerVideo-cli.exe -i input.mp4 -o output.mp4 --method ai --model realesrgan-x4plus --sharpen cas --fps-multiplier 2
```

Options: `--method` (ai/lanczos/spline), `--model`, `--scale`, `--denoise`,
`--denoise-strength`, `--deblock`, `--deband`, `--sharpen` (none/cas/unsharp),
`--sharpen-strength`, `--fps-multiplier`, `--encoder`, `--quality`,
`--deinterlace`.

## Requirements

| | |
|---|---|
| Compiler | MSVC with C++20 (Visual Studio 2022 or 2026) |
| Qt | 6.5 LTS or newer, `msvc*_64` kit |
| CMake | 3.24+ |
| vcpkg | manifest mode |

## Building

The short version — from a plain `cmd` or PowerShell prompt, no developer
shell needed:

```bash
build.bat
```

It locates vcpkg, Qt and Visual Studio on its own, configures, builds Release,
stages a complete application layout in `dist\`, and produces the installer in
`dist-installer\` when Inno Setup 6 is present.

```bash
build.bat debug --clean --run
```

Arguments: `release` (default) or `debug`; `--clean` wipes `build\` and
`dist\` first; `--installer` fails if Inno Setup is missing rather than
skipping the step; `--no-installer` skips it; `--run` launches the app on
success.

### Manual CMake

Two environment variables locate the external toolchains:

```bash
setx VCPKG_ROOT C:/vcpkg
```

```bash
setx QT_PREFIX C:/Qt/6.10.1/msvc2022_64
```

Alternatively, copy `CMakeUserPresets.json.example` to
`CMakeUserPresets.json` and edit the two paths in it. That file is not tracked
in version control, which is what makes the paths machine-local rather than
baked into the repository.

Configure and build:

```bash
cmake --preset default
```

```bash
cmake --build --preset default
```

Available configure presets: `default` (from your user presets), `vs2026`,
`vs2022`, `ninja`. The Ninja preset must be run from a Developer shell so that
`cl.exe` is on `PATH`; the Visual Studio presets work from any shell.

Output is staged in `build/<preset>/stage/<config>/`, with the Qt runtime
deployed alongside so the executables run directly from the build tree.

## Binaries

Windows forces a single subsystem per executable, so the same entry point is
linked twice:

- `DF-UpscalerVideo.exe` — GUI subsystem. No console window when launched from
  Explorer. Accepts `--cli`, in which case it attaches to the parent console.
- `DF-UpscalerVideo-cli.exe` — console subsystem. Blocks the shell and returns
  a real exit code, so it works in scripts and pipelines.

Exit codes: `0` success, `1` runtime failure, `2` usage error,
`3` requested operation not available in this build.

## Installer

`installer/DF-UpscalerVideo.iss` packages the `dist\` tree produced by
`cmake --install`, so the installer can never disagree with the binaries. It
refuses to compile if `dist\` is absent.

The vcpkg dependencies build against the `x64-windows-static-md` triplet:
static libraries, dynamic C runtime. That matches how Qt is built and means
there are no third-party DLLs to deploy beyond Qt's own — the packaged
application cannot fail with a missing `spdlog.dll` or `fmt.dll`.

Uninstall removes the program and its log directory, and deliberately leaves
`HKCU\Software\DF-UpscalerVideo` in place so a reinstall keeps user settings.

## Theme

**View → Theme** offers Follow System, Light and Dark; the choice persists in
`QSettings` and applies at the next launch as well as immediately.

The application uses the Fusion style rather than the native Windows style.
This is not a cosmetic preference: the Windows 10 native style draws through
uxtheme and has no dark variant, so it ignores a dark colour-scheme request
and leaves a light UI behind dark window decorations. Fusion honours
`Qt::ColorScheme` on every Windows version.

## Layout

```
bin/        ffmpeg.exe, ffprobe.exe        (not tracked; see bin/README.md)
cmake/      build system modules
models/     ncnn weights                   (not tracked; see models/README.md)
resources/  icons and the Qt resource file
src/core/   frame buffers, pools, rings, job spec, logging
src/media/  ffprobe/ffmpeg process wrappers, chunking
src/engine/ Vulkan context, ncnn processors, VRAM budget
src/pipeline/ job runner and queue
src/ui/     widgets
tests/      targeted tests
```

Only `src/core` and `src/ui` are populated at M0.

## Logs

`%APPDATA%/DF-UpscalerVideo/DF-UpscalerVideo/logs/dfupscaler.log`, rotating at
5 MB across 3 files. Also mirrored live into the application's log dock, and to
stdout in CLI mode. **File → Open Log Folder** opens the directory.
