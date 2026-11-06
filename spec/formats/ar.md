# .ar archives ("DAVE")

The game's data lives in a few archives (`mm2core.ar`, `mm2tex.ar`, `mm2aud.ar`, `mm2audex.ar`).
Mods add more `.ar` files next to them. Each archive is a flat list of files with `/`-separated
paths such as `geometry/vpbug.pkg` or `city/london.psdl`.

All numbers are little-endian unsigned 32-bit.

## Layout

| Offset | Size | Contents |
|---|---|---|
| 0 | 4 | ASCII `DAVE` |
| 4 | 4 | entry count |
| 8 | 4 | directory size in bytes, rounded up to a multiple of 2048 |
| 12 | 4 | name table size in bytes, rounded up to a multiple of 2048 |
| 16 | — | zero padding up to 0x800 |
| 0x800 | count × 16 | directory |
| 0x800 + directory size | name table size | name table |
| after that | — | file data, each file starting on a 2048-byte boundary |

Each directory entry is four numbers:

| Field | Meaning |
|---|---|
| name offset | where the entry's name starts, counted from the start of the name table |
| data offset | where the entry's data starts, counted from the start of the archive |
| size | size of the file once unpacked |
| packed size | number of bytes stored in the archive |

Names are NUL-terminated, Latin-1, and use `/` between folders. Entries are sorted by name with
the letters A–Z compared as a–z (otherwise byte order; MM2 also treats `\` as `/` when comparing).
MM2 finds a name by binary search over the entries as stored, so an archive that is not sorted
this way has entries the game cannot find. Entries whose name ends in `/` are folders and hold no
data.

When `packed size == size` the bytes are stored as-is. Otherwise they are raw deflate data
(RFC 1951, no zlib or gzip header) that unpacks to exactly `size` bytes.

## Several archives (mods)

MM2 lists every file matching `*.ar` in the game folder (at most 256), converts each name to upper
case (ASCII letters only) and sorts the names by byte value. Every archive is opened, and a path is
taken from the **first archive in that order** that contains it. The base archives get no special
treatment; their own order is `MM2AUD`, `MM2AUDEX`, `MM2CORE`, `MM2TEX`. So a mod named
`AAA.ar` (or starting with a digit or `!`) replaces base files, while `zzz.ar` or `mm2zz.ar` loses
to them wherever paths collide; `_` sorts after the letters. Most mods only add new paths, where
the order does not matter.

Once any archive has been found, loose files in the game folder are never read (a command-line
switch can name a single archive instead of the scan). Listing a folder (e.g. `tune/` for the car
list) walks the archives in priority order and each archive's entries in stored order; a path in two
archives is listed twice.

MM2 also accepts a zip file renamed to `.ar` (stored or deflated entries only, single part, no
comment). None ship; support is optional.

## Notes

- The shipped archives contain some leftovers (CVS folders, `.bat` and `.lnk` files). Ignore them.
- Paths are matched case-insensitively by the game's data (for example both `.jpg` and `.JPG`
  occur). A loader should look names up without regard to case.
- A lowercase `Dave` magic exists in other Angel Studios games (names stored differently). It does
  not occur in MM2's own archives; treat it as unsupported until a mod needs it.

## Checked against

`mm2core.ar` (5,680 entries), `mm2tex.ar` (4,365), `mm2aud.ar`, `mm2audex.ar`: every entry unpacks to
its stated size. All four directories are in order when A–Z are compared as a–z, and not in plain
byte order (mm2core: 102 neighbouring pairs such as `bound/vpsemi_trailer_BOUND.bnd`,
`bound/Vpvw_cup_BOUND.bnd`).
