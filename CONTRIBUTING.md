# Contributing to CrazyCity Insanity

Thank you for helping. The easiest ways in are play-testing against the original game and
reporting what differs, and small pull requests that fix one thing.

## What we are building

- **MM2's behaviour, exactly,** with the player's own MM2 files and mods: the same physics, AI,
  race rules and file handling, down to the order of operations where it changes the result.
- **Our own visual design** for the menus and the HUD (they are not copies of MM2's screens),
  while the game world looks the way the player's MM2 files make it look.
- **Every option in the game's own options menu.** No setting should need a launcher, a config file
  or an environment variable (those can exist for testing, never as the only way).
- **Enhancements are optional.** Anything that goes beyond MM2 (draw distance, controller extras,
  graphics improvements) is a switch in the options, and the original look and behaviour are
  always reachable. Gameplay, timing and physics never change because of a visual option.

## Reporting a difference from MM2

If something plays, sounds or works differently from the original game, open an issue with the
**"Behaves differently from MM2"** template: what MM2 does, what we do, the city, car and race, and
how to reproduce it. Side-by-side videos or screenshots help most. These reports are how the game
gets closer to MM2, and you do not need to write any code for them.

## Code contributions

Never submit, in any form:

- MM2's files (archives, textures, sounds, `mmlang.dll`, profiles) or anything extracted from them;
- decompiled or disassembled MM2 code, or code translated from MM2's program;
- code taken from other Midtown Madness projects, even rewritten.

A pull request that contains any of these will be closed. Write the code yourself, in the style of
the code around it.

## The behaviour specification

The engine is written from `spec/`, which describes how MM2 behaves, one page per area (file
formats, physics, AI, game rules, audio, the engine). Code comments cite its pages and sections.

- A change that alters behaviour follows the spec. If your change needs behaviour the spec does
  not describe, or the spec is unclear, note the point in `spec/QUESTIONS.md` in your pull request
  (what you need, which page, what you observed) and say which choice the code makes meanwhile.
- If you think the spec is wrong, open an issue that names the page and says what the original game
  does and how you saw it (side by side in MM2, a video, numbers). Please do not change a spec page
  and the engine together to match what you think MM2 does without that evidence.

## Before you open a pull request

The check runs must stay bit-identical unless your change is meant to change behaviour:

```
./build.sh checks --verify     # all 97 check runs against checks/expected.sha256
```

It names every run whose output differs. If some runs are meant to change, say which and why.
Please also run the other suites your change touches (see "Tests and check runs" in the
[README](README.md)): `./build.sh test`, `uitest`, `music`, `nettest`, and `selftest-native` for the
native game. They need your own MM2 files in `orig/`.

Keep a pull request to one change, and say what it does and which checks you ran.

## How pull requests are handled

- Pull requests from outside the project are **read in full before anything in them is built or
  run**, build scripts and tools above all.
- Merging needs the maintainer's approval.
- By contributing you agree that your contribution is released under the [Unlicense](LICENSE),
  like the rest of the project's own code.
