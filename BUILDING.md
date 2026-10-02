# Building From Source

## Windows

Install:

- Visual Studio 2022 Build Tools with Desktop development with C++
- CMake 3.21 or later
- vcpkg with the `sdl2:x64-windows` package

The release package uses the official SDL 2.32.10 x64 runtime and needs no
separate Visual C++ runtime installation. A source build using vcpkg's SDL2 DLL
may require Microsoft's [Visual C++ v14 x64 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
The game executables link their own C runtime statically.

Set `VCPKG_ROOT`, then run from the package directory:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
powershell -ExecutionPolicy Bypass -File .\build_windows.ps1
```

Equivalent commands:

```powershell
cmake -S reimpl -B build-windows -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build-windows --config Release
ctest --test-dir build-windows -C Release --output-on-failure
```

The faithful and enhanced executables are written to:

```text
build-windows\Release\niteraid.exe        # faithful
build-windows\Release\niteraid_remake.exe # enhanced
```

Place a supported `GRAPHICS.NTR` or `GRAPHICS.NRD` beside either executable.
The runtime reads the archive directly; no asset conversion or Python
installation is required. `GRAPHICS.NRD` runs the four-level shareware edition.

Launch from PowerShell after placing the archive beside the selected executable:

```powershell
.\build-windows\Release\niteraid.exe
.\build-windows\Release\niteraid_remake.exe
```

## Other Platforms

For non-Windows builds, install SDL2 development files. CMake uses an SDL2
package config when available and otherwise falls back to the `sdl2`
pkg-config module. SDL3 is not supported by this release.

```sh
cmake -S reimpl -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On single-configuration generators, the executables are normally written to
`build/`. Place `GRAPHICS.NTR` or `GRAPHICS.NRD` beside the resulting
executable. Platform packaging outside Windows has not been validated in this
release. `GRAPHICS.NRD` runs the four-level shareware edition.
