# Localised text: `mmlang.dll`, fonts and the intro movie

All of MM2's on-screen text (menu labels, option values, HUD messages, results lines, error
boxes) comes from one file next to the game, `mmlang.dll`, by number. Each language version of
MM2 ships its own copy; the program itself is the same. Our engine shows its own interface, but
it should take the words from the player's `mmlang.dll` by the same numbers, so a player with a
French, German or Japanese MM2 sees their own language. This page says how to read the file,
how MM2 treats the text, which number means what (by purpose, not by wording: the strings are not
copied into the spec), and notes on fonts and the intro movie.

## The file

`mmlang.dll` is a Windows DLL used purely as a container of resources: MM2 never runs code from
it and our engine must **never load or run it**, only read its bytes. The shipped (US English)
file is 53,248 bytes. Its only resource type is the standard string table (resource type 6):
42 blocks of 16 strings, one language each (0x0409 in the shipped file), strings 1 – 668 present
except 447 (and 0). Each string is UTF-16 little-endian, stored with a 16-bit length in front and
no terminating zero.

### Reading string *id*

1. At file offset 0x3C read u32 *p*; the file must start with `MZ` and have `PE\0\0` at *p*.
2. The file header follows at *p* + 4: number of sections = u16 at +2, size of the optional
   header = u16 at +16.
3. The optional header starts at *p* + 24. Its first u16 is 0x10B (32-bit) or 0x20B (64-bit);
   the data-directory table starts at +96 (or +112), 8 bytes per entry (u32 address, u32 size).
   Entry 2 is the resource directory; an address of 0 means "no strings".
4. The section table follows the optional header, 40 bytes per section: virtual size at +8,
   virtual address at +12, raw size at +16, raw file offset at +20. To turn an address *a* into a
   file offset, find the section with VA ≤ *a* < VA + max(virtual size, raw size): offset = *a* −
   VA + raw offset (reject *a* − VA ≥ raw size).
5. *root* = the file offset of the resource directory. A directory has a 16-byte header with the
   number of named entries (u16 at +12) and of numbered entries (u16 at +14), followed by that many
   8-byte entries: u32 name (top bit set = named; else the low 16 bits are the number), u32 target
   (top bit set = a subdirectory at *root* + low 31 bits; clear = a data entry at *root* + target).
6. Walk three levels: type **6**; then block **(*id* >> 4) + 1**; then the language (take the
   wanted language if present, else the first entry).
7. The data entry (16 bytes) holds u32 address (a normal address, not relative to *root*) and u32
   size of the block.
8. In the block, skip (*id* AND 15) strings (each: u16 *n*, then 2*n* bytes), then read u16 *n* and
   2*n* bytes. *n* = 0, or a missing block, means the string does not exist.

### How MM2 treats the text

- A missing string is shown as **empty**, with no error (the shipped file lacks 447, the label of
  the in-race chat box, so that label is blank).
- MM2 converts each string to the Windows ANSI code page of the machine and keeps at most 511
  bytes; a double-byte character cut in half at the end is dropped. On-screen text lines take at
  most 255 bytes; a longer HUD message is ignored (the previous text stays). Our engine keeps the
  UTF-16 text, so it shows any language without a matching Windows code page; it should still
  treat strings as one line of at most 255 characters where MM2 does.
- Several strings are `printf` formats: 4 (one integer: an opponent number), 232 (one number
  printed with no decimals: a target speed in mph), 29 (followed by the car position in brackets
  with one decimal). Strings 238 and 652 contain a line break.
- MM2 builds value lists for its menus by joining several strings with `|` and splitting them
  again later, so a localised string never contains `|`.
- Some strings hold numbers that are settings, read as integers (below).
- Long names in tables are cut to a number of bytes given by strings 664 – 668 (15, 10, 10, 15,
  15) when wider than a fifth (or 0.15) of the screen, followed by three dots.

The English shipped file is pure 7-bit text.

## Text numbers by purpose

The numbers the game uses (602 of them asked for by the program, 11 of those only from dead code; 66
strings are never asked for). "List" means the strings are joined into one `|` value list in this
order.

**Start-up and errors.** 246 no usable display adapter; 247 the game is already running;
248 little virtual memory; 249 little disk space; 250 DirectX 7 needed; 611 broken install;
658 desktop colour depth too low; 663 video driver too old for hardware mode; 74 the web address
of the online-play site (opened by a menu button, after the game quits).

**Settings stored as text** (integers): 275 source of key names in the binding screen (0 = the
operating system's key names); 531 in-race menu font choice (0 = normal); 605 text-entry style
(0 = direct typing); 612, 613 smallest display mode offered, width and height (320 × 200);
664 – 668 cut lengths (above). Font descriptions: 48, 58 – 61, 251 – 253, 256 – 258, 262, 263,
558 – 573 ("Fonts").

**Drivers and records** (front end). 65 and 66 the name and network name of the default driver
created at the first start; 77 the network name given to a new driver (and 437, the name used when
one is left empty); driver-panel words: 78 crash course, 79 Cops and Robbers, 80 Cruise, 81
Professional, 82 Amateur; 635 – 638, 640 driver record labels (ranking, last race, last car,
controller, score); 428 – 430 player panel labels (driver, car, colour). Tables: race statistics
screen title 348; its column headings 351 race, 353 driver, then 350 (time) while an Amateur or
Professional time table is shown or 349 (score) for the score table, and 355 car (354 is that third
heading as first built and is replaced at once; 352 is stored with the table but never shown);
driver record table headings 345 race, 346 time, 347 car, and 344 points only for a Professional
driver.

**Race and car choices.** Mode names as results titles (followed by the race's name): 5 Circuit, 6
Checkpoint, 7 Blitz, 12 Crash Course, 11 Cops and Robbers followed by the sub-mode's name, 8
free-for-all, 9 cops and robbers, 10 robber teams (sub-modes 0, 1, 2); Cruise has none (80 is the
driver panel's Cruise word). 585 – 589 list of game modes in the race menu's order: 585 Blitz, 586
Circuit, 587 Checkpoint, 588 Cruise, 589 Cops and Robbers (the multiplayer session panel names a
session's mode from it; a Cruise session shows a blank there). Race menu choices: time of day 0 – 3
= 629, 630, 631, 632 (morning, noon, evening, night), weather 0 – 3 = 625, 626, 627, 628 (clear,
cloudy, fog, rain); 357 – 360 and 361 – 364 are the same time-of-day and weather choices in that
order, used only by a screen that never opens. 578, 579 and 633, 634 automatic / manual; 590 – 599
the numbers 1 – 10; 532 – 544 London crash-course lesson names, 545 – 557 San Francisco ones (each
city's 13 joined into one list).

**Options.** 574 – 577 detail levels list (with 389 the "recommended" mark and 390 – 393 detail
presets); the cloud-shadow list 660 none, 661 low, 576 high (values 0, 1, 2); 600 – 603 a four-step
list (none, vehicles, props, skid marks); 394 – 396 renderer (software, hardware, hardware with
transform and lighting); 644, 645, 647, 648, 659, 460, 461 graphics options labels and title; 326 –
330 audio channel modes (mono, stereo, two surround variants, stereo effects); 325 unknown
controller, 331 controllers heading; 580 – 584 controller types list; 276 – 309 the names of the 34
control actions on the binding screen, **not** in action order: each action's text number is in
`../engine/controls.md`, "Default binding table" (the screen lists the actions 0 – 33 in order, each
with its own text); 606, 607 off / on; 604 the navigation bar. **A binding's name** on that screen:
a keyboard key → the key's name (string 275, above); mouse → 310 left button, 311 right button, 312
X axis, 313 Y axis; a game-controller button *n* → 273, a space, 272, a space, the number *n*; a
controller axis → 273, a space, and 314 X, 317 Y, 320 Z, 321 U, 322 R, 323 V, 324 hat, 315 / 316 X
left / right half, 318 / 319 Y up / down half; anything else (an empty entry) → 271. 274 is loaded
but never shown.

**In-race menus.** 463 – 469 pause menu; 470 previous; 471, 472 cancel, OK; 473 resume;
474 – 477 options menu; 442 – 445 audio options; 448 – 453 control options; 462 customise
controls; 456 – 459 quit question; 491 – 498 results menu; 499 "did not finish"; 500 results list
title; 653 – 656 crash-course versions of restart / next / back; 446, 447 chat box; 483 – 490
replay controls; 502 – 505 save-replay question; 454, 455 debug menu.

**HUD and race messages.** 3 the local player's default name; 4 default opponent name (format); 13 –
20 list of opponent names; 21 – 28 list of place suffixes (1st, 2nd, …); 29 fell out of the world;
30 in the water (other cities), 642 London, 643 San Francisco (`../game/hud.md`); 62 final lap; 63
lap time label; HUD panels: 254 place and 255 checkpoints (the Checkpoint, Blitz and Crash Course
panel), 259 place, 260 checkpoint, 261 lap (the Circuit panel), 251 – 253 and 256 – 258 their fonts;
Cops and Robbers panel: 264 / 265 the team names (team 0 / 1) in the cops-and-robbers sub-mode, 266
/ 267 in the robber-teams sub-mode, 268 the money sign, 262, 263 its fonts; 269, 270 two hit
counters of a crash-course panel the program never creates; 158 – 164 single-player blitz messages,
165 – 178 circuit, 179 – 191 checkpoint race (countdown steps, go, penalties, game over, finishing
lines); crash-course lessons: 192 – 198 evade, 199 – 204 accelerate and stop, 205 – 209 and 614, 615
jump, 210 – 216 clean driving, 217 – 223 follow the car, 224 – 230 no contact, 231 – 240 minimum
speed (232 the format), 241 – 245 and 609 checkpoints, 616 – 622 destroy the car, 651 pass mark, 652
evade intro. (`../game/hud.md` gives which message appears when.)

**Multiplayer** (which text shows when: `../game/multiplayer.md`). 31 – 47 lobby and in-game player
notices (waiting for players, joined, left, host quit, ejected, cheating); 89 – 99 blitz, 100 – 111
circuit, 112 – 142 Cops and Robbers (gold, teams, limits, minutes left 138 – 142), 143 – 153
checkpoint race and 154 – 157 Cruise messages. **Network screens** (front end): MM2's buttons there
(host, join, start, ready, eject, back) are pictures with their words drawn in, so they have no text
number; the texts it does use are: session list 438 the prompt, 657 "searching", 83 shown in place
of a session from another network version (it cannot be joined), 437 the name used when the network
name is empty, and 67 (a host-or-join question that is fetched but never shown); serial link 365 –
368 ports, 369 – 376 baud rates, 377 – 379 stop bits, 380 – 384 flow control, 385 – 388 parity;
lobby message lines 68 joined and 69 left (after the player's name), 70 you are now the host, 71 the
game started in a city you do not have, 72 you have been disconnected, 73 the session returns to the
lobby, 84, 85 the session's city is unknown / you cannot join, 86 the city is missing so you are
disconnected; 398 the chat entry hint; player panel labels 428 driver, 429 car, 430 colour; the
session panel: 407 and the city's name (city not installed: 406, 403, and 404 + 405 on two lines),
the mode name (585 – 589), the event (399 Cruise, the race's name, or 400 – 402 the Cops and Robbers
sub-modes 0 – 2), weather 408 clear, 624 cloudy, 409 fog, 410 rain, time of day 412 – 415, Circuit
418 laps with the number, Cops and Robbers 423 gold weight with 420 / 421 / 422 (mass 0 / 100 / 200)
and 427 limit with the number and 424 points or 425 minutes, or 426 none; 87 and 88 label the city
and the event in the session text the host sends; host options for Cops and Robbers 509 the
gold-mass label with the list 506 – 508 (mass 0, 100, 200), time limits 510 – 513 (5, 10, 20, 30
minutes), point limits 514 – 517 (100, 250, 500, 1,000; the sub-mode and limit-type choices are
pictures). **In-race**: quit menu 478 title, 479 quit to the lobby, 641 quit the game (481 instead
in Cops and Robbers), 482 cancel; roster 500 title, 501 the host's mark; results 494 show the
roster; chat 446 title, 447 (missing) the field label, 398 the hint. The lobby texts 431 – 436
(players in session, waiting for the host to start, start race, select provider) and 480 (migrate
host) exist but MM2 never shows them; an engine may use them for those labels in the player's
language.

Never used: 1, 2, 39, 75, 76, 108, 163, 178, 192, 205, 217, 224, 231, 332 – 343, 356, 397, 411,
416, 417, 419, 431 – 436, 439 – 441, 465, 467, 480, 495, 518 – 530, 608, 610, 616, 623, 639, 646,
649, 650, 662; and 49 – 57 (multiplayer finishing places) and 269, 270 (crash-course hit
counters), which only pieces of code that are never called ask for.

## Fonts

MM2 draws its text with Windows fonts named in the string table. A font description is the text
`face, small height, large height, character set, weight` (comma-separated; the character set is
the Windows number, 0 for Western text, which is how a Japanese or Korean table switches to a
double-byte font). The large height is used when the screen is at least 640 pixels wide, else
the small one; heights are pixels. A description with a field missing is invalid.

| Strings | Used for | Shipped |
|---|---|---|
| 558 – 565 | the front end's eight text sizes (keys 12, 14, 16, 20, 24, 32, 48, 64; any other key uses 16) | Arial Bold, 12/12, 14/14, 16/16, 18/18, 18/24, 12/16, 24/48, 32/64, weight 400 |
| 566 – 573 | the same eight for the in-race menus | 12/12, 12/14, 14/16, 16/20, 16/24, 20/32, 20/40, 32/64 |
| 48 | names over opponent cars | Gill Sans MT 10/16 |
| 59, 60, 61 | HUD text lines (position, two-line messages, chat lines) | Gill Sans MT 12/24, 20/36, 8/16 |
| 58 | face only: the HUD number font (height = screen height × 24 / 480, at least 12) | Broadway |
| 251 – 253, 256 – 258 | checkpoint / crash-course and circuit HUD panels (numbers and labels) | Gill Sans MT 16/22 and 20/40 bold |
| 262, 263 | Cops and Robbers panel | Gill Sans MT 12/20 bold, 10/16 |

The HUD's big digits (timer, speed, gear) are textures, not fonts (`../game/hud.md`). Our engine
uses its own fonts; it only needs to follow the character set for double-byte languages (or
simply render the UTF-16 text). Typing double-byte names needs an input-method editor; MM2 turns
that on for Japanese and Traditional Chinese systems and off for Korean.

## Intro movie

At start-up MM2 plays `logos.avi` from its folder (the publisher and developer logos: 320 × 75
pixels, Cinepak, 144 frames at 29.97 frames per second = 4.8 s, mono 16-bit 44.1 kHz sound),
centred on a black screen. Escape, Space or the left mouse button ends it early. It is skipped
with the `-nomovie` command-line switch (the shipped "No Movie", "No Sound" and "Safe Mode"
shortcuts pass it) or in a window. A missing file is ignored. Our engine has no need to play it.

## Checked against

- The reading steps above, written as a stand-alone reader, give all 667 strings of the shipped
  `mmlang.dll` identical to a reference PE parser; one language (0x0409), blocks 1 – 42, string
  447 absent; longest string 358 characters; all plain ASCII.
- String 4 contains one integer format, 232 one number format; 612/613 read 320 and 200; 664 – 668
  read 15, 10, 10, 15, 15; 275, 531, 605 read 0.
- `logos.avi`: 1,367,882 bytes, 2 streams, 144 video frames (rate 2997/100), 211,856 audio samples.
