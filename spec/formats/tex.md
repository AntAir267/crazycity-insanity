# Textures: lookup, .tex, .tga, .bmp

Materials in a model name their texture without a folder or extension (for example
`vpbugyellow_sd`, `carbottom`, `s_ocean`). This file says how such a name becomes an image, and
the layout of the image files. Model-side rules (which material uses which texture, the `_dmg`
slots on cars) are in `pkg.md` and `car-model.md`.

All numbers are little-endian.

## Finding the file

1. Images live in the `texture/` folder of the archives (`mm2tex.ar`, plus mod archives). All
   name matching is case-insensitive.
2. Try, in order, `texture/<name>.tex`, `texture/<name>.tga`, `texture/<name>.bmp`. The first
   that exists **and decodes** wins: a `.tex` that MM2 refuses (size not a power of two,
   unknown type) makes it go on to `.tga`, then `.bmp`.
   - When the name already ends in the extension being tried (`foo.tga`), that extension is not
     added a second time.
   - A name containing `/`, `\` or a drive letter is taken as a path from the game folder,
     without `texture/`. No shipped material does this.
   - MM2 also knows a raw 8-bit format (`<name>.raw` + optional `<name>.act` palette) but only uses
     it on graphics cards with palettised textures, which no current system has. Ignore it.
3. If nothing was found, look for an **animated texture**: frames `<name>-0001`, `<name>-0002`, …
   (four digits, each looked up as in step 2), stopping at the first missing number, at most 128
   frames. (MM2 has a debug option to step frame numbers by more than 1; default 1.)
   The frame rate comes from the text file `tune/<name>.movie`, which contains `rate <number>`
   (frames per second); without that file the rate is 30. Frames loop forever; the frame shown
   advances every 1/rate seconds.
4. If still nothing: the material is drawn untextured (its colour only).

During a driving session the name first goes through the time-of-day and weather rules of
`night.md`: in rain `<name>_fa` is tried first, at night `<name>_ni`, and at night every texture
that is not a `_ni` one is loaded at half brightness.

The `jpg/` folder in `mm2tex.ar` holds menu backgrounds; models never use it.

## .tex

| Offset | Size | Contents |
|---|---|---|
| 0 | 2 | width *W* (power of two) |
| 2 | 2 | height *H* (power of two) |
| 4 | 2 | pixel type (table below) |
| 6 | 2 | number of mip levels stored (0 = as many as possible; not used in shipped files) |
| 8 | 2 | always 1; ignored |
| 10 | 4 | flags (below) |
| 14 | — | palette (palette types only), then the mip levels, largest first |

Width and height must be powers of two (non-square is fine); MM2 rejects anything else.

### Pixel types

| Type | Palette | Bits per pixel | Pixel data | Alpha |
|---|---|---|---|---|
| 1 | 256 entries | 8 | palette index | none (palette alpha ignored) |
| 14 | 256 entries | 8 | palette index | from the palette entry |
| 2 | 256 entries | 16 | byte 0 = palette index, byte 1 = alpha | from the pixel |
| 15 | 16 entries | 4 | palette index; two pixels per byte, **low nibble first** (left pixel) | none |
| 16 | 16 entries | 4 | as type 15 | from the palette entry |
| 17 | — | 24 | bytes R, G, B | none |
| 18 | — | 32 | bytes R, G, B, A | from the pixel |
| 6 | — | 16 | u16: bit 15 = alpha (0 or 1), bits 10–14 red, 5–9 green, 0–4 blue | 1-bit |

Any other type is refused. Shipped files use only 1, 14, 17 and 18.

Palette entries are 4 bytes each, stored **B, G, R, A** (so 1,024 bytes for 256 entries, 64 bytes
for 16). Note the byte order differs from types 17/18, which store R first.

Alpha values are 0–255 (255 = opaque).

### Mip levels

Level 0 is *W* × *H*. Each following level halves both sides. A level exists only while the
previous level was more than 1 pixel in **both** directions, so a 256 × 64 texture has at most
7 levels (256×64 … 4×1). The file stores `min(count, possible)` levels back to back, each with
rows packed tightly (no padding): a level takes `w × h × bits / 8` bytes. All levels share the
one palette. Bytes after the last level are junk; ignore them.

Expected file size = 14 + palette bytes + Σ levels. A renderer that wants no mip-mapping reads
level 0 only. A texture with a single level is drawn without mip-mapping.

### Row order and texture coordinates

Rows are stored in the order the renderer samples them: **row 0 is v = 0**, row *H*−1 is v ≈ 1;
each row runs from u = 0 to u ≈ 1. Upload rows in file order and use the model's (u, v) as
stored. (Many car textures look upside down if you write row 0 at the top of a picture; that is
just how they were authored.)

### Flags

| Bit | Meaning |
|---|---|
| 0x00001 | clamp u: do not repeat the texture horizontally (u outside 0..1 takes the edge texel) |
| 0x10000 | clamp v: do not repeat vertically |
| 0x00002 | surface receives the ground overlay (cloud shadows by day, light pools at night; `night.md`) at the **high** detail setting |
| 0x00004 | surface receives the ground overlay already at the **medium** setting (shipped files set 0x2 too) |
| 0x08000 | set by the exporter on about 200 textures; the game never reads it — ignore |

Without the clamp bits a texture repeats (wraps). Textures with an alpha channel never get the
ground overlay. Flag combinations seen: 0x0, 0x2, 0x6, 0x7, 0x8000, 0x8006, 0x10000, 0x10001,
0x10002, 0x10003, 0x10006, 0x18001, 0x18006.

### Alpha channel

A texture **has alpha** — and batches using it are drawn blended (`pkg.md`) — when its type is
2, 6, 14, 16 or 18, or it is a 32-bit `.tga`. Types 1, 15, 17, 24-bit `.tga` and `.bmp` are
opaque even if their palette holds alpha values.

## .tga

MM2 reads a narrow subset of Truevision TGA:

| Offset | Size | Field | Supported values |
|---|---|---|---|
| 0 | 1 | ID length | 0 in all shipped files (MM2 does not skip an ID field; a reader should) |
| 1 | 1 | colour-map type | 0 |
| 2 | 1 | image type | 2 (uncompressed true colour) |
| 12 | 2 | width | any (menu art is not power-of-two) |
| 14 | 2 | height | any |
| 16 | 1 | bits per pixel | 24 or 32 |
| 17 | 1 | descriptor | bit 0x20 set = first stored row is the top of the picture |
| 18 | — | pixels: B, G, R (and A for 32-bit) per pixel |

MM2 flips bottom-up files (descriptor bit 0x20 clear) so that the top row of the picture is
row 0, i.e. **v = 0 is the top of the picture** for a TGA. Trailing TGA 2.0 footers are
ignored. Square TGAs get a full mip chain generated at load time; non-square ones get none.

## .bmp

None ship, but MM2 accepts a narrow kind of Windows bitmap:

- `BM` file header (14 bytes, with the pixel-data offset `bfOffBits` at byte 10), then an info
  header: 40 bytes, or the old 12-byte core header (recognised by its size field).
- 8 or 24 bits per pixel, compression 0; anything else is refused.
- 8-bit palette: always 256 entries read from file offset 54 (right after a 40-byte header),
  4 bytes each (B, G, R, unused) — 3 bytes each (B, G, R) when the header is the 12-byte kind, but
  still read from offset 54. The header's palette size fields are ignored.
- Pixels start at `bfOffBits`, always (even when it is 0).
- Row length in the file: the width rounded up to even, times the bytes per pixel
  (8-bit: roundup2(*w*) bytes, 24-bit: roundup2(*w*) × 3 bytes); rows follow each other with no
  other padding. So only bitmaps whose real 4-byte-padded rows happen to have that length load
  correctly.
- Rows are stored bottom-up and loaded so the top row of the picture is row 0. 24-bit pixels are
  B, G, R.
- Always opaque, no mip levels stored.

## Size limits

MM2 shrinks textures larger than the graphics card allows by dropping top mip levels (or halving
when no smaller level exists). A modern engine can ignore this.

## Checked against

`tools/formats/tex.py` over `mm2tex.ar`'s `texture/` (3,665 `.tex`, 307 `.tga`):

- 3,663 `.tex` decode; every file is at least the expected size and 3,641 match it exactly
  (24 carry 1–7 junk bytes after the last level). The 2 refused are `nl01_coit_top.tex`
  (64 × 73) and `sf_wall_brick03_blkfence_2_l.tex` (650 × 597) — not powers of two, as MM2 also
  refuses.
- Types: 1 = 2,930, 14 = 139, 17 = 299, 18 = 295. Header word at offset 8 is 1 in every file.
  Stored mip counts 1–10; 1,789 files store the full chain.
- Flags: 0x2 (1,532), 0x10001 (940), 0x0 (638), 0x18001 (278), 0x8006 (95), 0x18006 (58),
  0x8000 (44), 0x10000 (35), 0x10002 (21), 0x6 (10), 0x10003 (6), 0x7 (5), 0x10006 (1).
- All 307 `.tga`: image type 2, no ID, no colour map, all bottom-up (descriptor bit 0x20
  clear); 214 are 24-bit, 93 are 32-bit (91 of them with descriptor 8 = 8 alpha bits); 250 end
  with a 26-byte TGA 2.0 footer. 260 are not power-of-two (menu and HUD art).
- Colour order: `vpbugyellow_sd` (type 1) decodes yellow, `sky_ca_f` (type 17) blue sky with
  orange clouds, `audi_dashl` (type 18) a readable dashboard; textured car renders show logos the
  right way round (`work/check/`).
- Name resolution for every texture name in every `.pkg` material: 2,234 distinct names;
  2,206 found as `.tex`, 9 as `.tga` (the HUD map dots such as `red_dot`), 3 as animated
  frames (`s_ocean`, `s_pond`, `s_thames`, 30 frames each; `tune/s_ocean.movie` says `rate 20`),
  16 not found: 10 city texture names (`s_veggrass`, `gw_stc_redbrick_garage_f`, …) and six
  `vppanozgt…_dmg` slot names whose clean textures exist (shown clean, per `car-model.md`).
