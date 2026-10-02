# Third-Party Notices

## SDL 2.32.10

The Windows binary dynamically loads `SDL2.dll`. Release packages use the
unmodified x64 runtime from the official SDL 2.32.10 release:
[upstream downloads](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10).
Upstream runtime ZIP SHA-256:
`6cf9706eefd0a4a06dc764007934d428afaf029fabdd408a9e646048c91e18fb`.
Its embedded `C:\temp\SDL2-2.32.10` paths belong to the upstream build,
not the developer's workstation. No Microsoft runtime installer is
redistributed in this package.

Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

SDL is provided under the zlib license. The complete notice is included at
`licenses/SDL2.txt`, with the upstream runtime README at
`licenses/SDL2-README.txt`. The packaged DLL imports Windows system libraries
only; it does not require a separate Visual C++ runtime installation.

SDL's Windows build also incorporates HIDAPI (Alan Ott, Signal 11 Software).
This distribution selects HIDAPI's BSD-style license from its three offered
alternatives. The complete notice is at `licenses/SDL2-HIDAPI.txt`; see the
[upstream license choices](https://github.com/libsdl-org/SDL/blob/release-2.32.10/src/hidapi/LICENSE.txt).
HIDAPI's alternative GPLv3 license is not selected for this distribution.

## Nuked-OPL3

Copyright (C) 2013-2020 Nuke.YKT

Nuked-OPL3 is vendored in source form under
`reimpl/third_party/nuked-opl3` and used for OPL3 music rendering.

License: LGPL-2.1-or-later. See
`reimpl/third_party/nuked-opl3/LICENSE`.

The Windows executables compile this library into the application. The ZIP
includes the complete library and application source plus `BUILDING.md` and
the CMake build files, so users can modify Nuked-OPL3 and rebuild/relink either
executable. This distribution uses the source-accompaniment approach in
[LGPL-2.1 section 6(a)](https://www.gnu.org/licenses/old-licenses/lgpl-2.1).
Keep that source and all component notices with redistributed binary packages.
No additional restriction on modification for personal use or reverse
engineering to debug those modifications is imposed by this project.

## VGA Text Font

Startup and exit cards use the Oldschool PC Font Pack IBM VGA 8x16 CP437 font
by VileR, converted to bitmap arrays by Susam Pal's `pcface` project.
The pinned source and unchanged glyph-byte conversion are documented in
`reimpl/third_party/pcface/README.md`.

The font array is licensed under Creative Commons Attribution-ShareAlike 4.0
International, separately from the MIT application code. The complete license
is included at `reimpl/third_party/pcface/LICENSE`. Retain this attribution and
license when redistributing source or binaries; font adaptations retain the
same license.

