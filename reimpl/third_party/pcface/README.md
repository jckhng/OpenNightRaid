# VGA Text Font

`font8x16.hpp` contains the Oldschool PC Font Pack IBM VGA 8x16 CP437
bitmap by VileR, converted to bitmap arrays by Susam Pal's
[pcface project](https://github.com/susam/pcface).

Pinned upstream revision: `8629ba46b73f58ac88cacbe8e77c6e9975dd221c`.
Source: `out/oldschool-vga-8x16/fontlist.js`.
Source SHA-256:
`f8c59bfc39e52dae72d04406654230d5d4d1a343554bdddfbe11a137d0154cc7`.

Local adaptation: converted the JavaScript array to a C++ constexpr array;
removed character annotations and added attribution. All 4,096 glyph bytes
are unchanged. They match the live canonical DOSBox-X BIOS font byte-for-byte.
No Night Raid archive, executable, or captured ROM bytes are distributed here.

The font array is **CC BY-SA 4.0**, not MIT. See the complete license in
`LICENSE` and the [upstream licensing scope](https://github.com/susam/pcface#license).
Retain attribution and the license when redistributing the font, including in
binaries. Adaptations of the font remain under CC BY-SA 4.0.
