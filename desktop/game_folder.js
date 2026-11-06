// CrazyCity Insanity desktop app: the player's own Midtown Madness 2 folder.
//
// The page finds its game data the way the dev build gives it (build.sh, write_game_index):
// game/index.json lists the archives (with their sizes), whether mmlang.dll is there, the files of
// the players folder and an optional General MIDI bank, and game/<name> serves each file with byte
// ranges. Here the listing is made from the folder the player chose, and every file is read from
// that folder on disk when the page asks for it: nothing is copied.
'use strict';

const fs = require('fs');
const path = require('path');

const BASE = ['mm2core.ar', 'mm2tex.ar'];
const MAX_PLAYER_FILES = 4000;

function statOrNull(p) {
  try { return fs.statSync(p); } catch (e) { return null; }
}

function listDir(dir) {
  try { return fs.readdirSync(dir); } catch (e) { return []; }
}

// The files below the players folder, as paths relative to it with '/' (sorted).
function playerFiles(root) {
  const out = [];
  const walk = (dir, rel, depth) => {
    if (depth > 6 || out.length >= MAX_PLAYER_FILES) return;
    for (const name of listDir(dir).sort()) {
      const full = path.join(dir, name);
      const st = statOrNull(full);
      if (!st) continue;
      const r = rel ? rel + '/' + name : name;
      if (st.isDirectory()) walk(full, r, depth + 1);
      else if (st.isFile()) out.push(r);
    }
  };
  walk(root, '', 0);
  return out.sort();
}

// What a folder holds: {dir, ok, archives: [{name, size, file}], mmlang, playersDir, players,
// gm: {name, size, file}, missing: [base archives not found]}.
function scanFolder(dir) {
  const res = { dir: dir || '', ok: false, exists: false, archives: [], mmlang: null, playersDir: null, players: [], gm: null, missing: BASE.slice() };
  if (!dir) return res;
  const st = statOrNull(dir);
  if (!st || !st.isDirectory()) return res;
  res.exists = true;
  for (const name of listDir(dir)) {
    const full = path.join(dir, name);
    const lower = name.toLowerCase();
    const s = statOrNull(full);
    if (!s) continue;
    if (s.isFile() && lower.endsWith('.ar')) res.archives.push({ name, size: s.size, file: full });
    else if (s.isFile() && lower === 'mmlang.dll') res.mmlang = full;
    else if (s.isFile() && lower === 'gm.dls') res.gm = { name, size: s.size, file: full };
    else if (s.isDirectory() && lower === 'players') res.playersDir = full;
  }
  // By name without regard to case, as the dev listing (build.sh) has them.
  res.archives.sort((a, b) => (a.name.toLowerCase() < b.name.toLowerCase() ? -1 : a.name.toLowerCase() > b.name.toLowerCase() ? 1 : 0));
  if (res.playersDir) res.players = playerFiles(res.playersDir);
  const have = new Set(res.archives.map((a) => a.name.toLowerCase()));
  res.missing = BASE.filter((b) => !have.has(b));
  res.ok = res.missing.length === 0;
  return res;
}

// The folder itself, or the one folder below it that holds MM2 (a player who picks the folder
// above the game's, such as "Microsoft Games").
function findGameFolder(dir) {
  const top = scanFolder(dir);
  if (top.ok || !top.exists) return top;
  for (const name of listDir(dir).sort()) {
    const sub = path.join(dir, name);
    const st = statOrNull(sub);
    if (!st || !st.isDirectory()) continue;
    const s = scanFolder(sub);
    if (s.ok) return s;
  }
  return top;
}

// game/index.json, in the dev build's shape.
function indexJson(scan) {
  const idx = { archives: scan && scan.ok ? scan.archives.map((a) => ({ name: a.name, size: a.size })) : [] };
  if (scan && scan.ok) {
    if (scan.mmlang) idx.mmlang = true;
    if (scan.players.length) idx.players = scan.players;
    if (scan.gm) idx.gm = { name: scan.gm.name, size: scan.gm.size };
  }
  return JSON.stringify(idx) + '\n';
}

// The file on disk for a path below game/ (already decoded, without the "game/"), or null.
function resolveGameFile(scan, rel) {
  if (!scan || !scan.ok || !rel) return null;
  const lower = rel.toLowerCase();
  if (lower.startsWith('players/')) {
    if (!scan.playersDir) return null;
    const sub = rel.slice('players/'.length);
    if (!scan.players.includes(sub)) return null;  // only the files the listing gave
    const file = path.resolve(scan.playersDir, sub);
    return file.startsWith(path.resolve(scan.playersDir) + path.sep) ? file : null;
  }
  if (rel.includes('/') || rel.includes('\\')) return null;
  if (lower === 'mmlang.dll') return scan.mmlang;
  if (scan.gm && lower === scan.gm.name.toLowerCase()) return scan.gm.file;
  const a = scan.archives.find((x) => x.name.toLowerCase() === lower);
  return a ? a.file : null;
}

// One line about the folder for the self-test and the panel.
function describe(scan) {
  if (!scan || !scan.dir) return 'no folder chosen';
  if (!scan.exists) return scan.dir + ' (not found)';
  if (!scan.ok) return scan.dir + ' (no ' + scan.missing.join(' or ') + ')';
  const parts = [scan.archives.map((a) => a.name).join(', ')];
  parts.push(scan.mmlang ? 'mmlang.dll' : 'no mmlang.dll');
  parts.push(scan.playersDir ? 'players/ (' + scan.players.length + ' files)' : 'no players folder');
  parts.push(scan.gm ? scan.gm.name : 'no gm.dls');
  return scan.dir + ': ' + parts.join('; ');
}

module.exports = { BASE, scanFolder, findGameFolder, indexJson, resolveGameFile, describe };
