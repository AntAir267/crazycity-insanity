// CrazyCity Insanity: the page around the engine. Everything visible is drawn by the engine
// (its own UI, engine/ui and engine/app/front_end.*); this file only does what needs the browser:
// loading the archives (from the dev server, or the visitor's own MM2 folder) and handing them to
// the engine (the bytes stay in JavaScript ArrayBuffers; the engine copies the ranges it reads),
// the file pickers and dropped files, the clipboard events, the network session server's address,
// and keeping a network game going while the page is hidden.
'use strict';

(function () {
  const $ = (sel) => document.querySelector(sel);
  const canvas = $('#canvas');

  const CCI = (window.CCI = {
    archives: [],      // jsId -> ArrayBuffer or LazyArchive (read by engine/platform/web_bridge.cpp)
    loaded: [],        // [{name, id, size}]
    engineReady: false,
  });

  // The audio archives (sounds, speech, music) are read lazily (LazyArchive below).
  const AUDIO = new Set(['mm2aud.ar', 'mm2audex.ar']);

  // ------------------------------------------------------------------ engine glue

  function call(name, ret, types, args) {
    try {
      return Module.ccall(name, ret, types || [], args || []);
    } catch (e) {
      console.warn('engine error in ' + name, e);
      return ret === 'string' ? '' : null;
    }
  }
  function callJson(name, types, args) {
    const s = call(name, 'string', types, args);
    try { return s ? JSON.parse(s) : null; } catch (e) { return null; }
  }
  // Copies bytes into wasm memory for the duration of one call.
  function withBytes(bytes, fn) {
    const n = bytes.length;
    const p = Module._malloc(Math.max(1, n));
    try {
      Module.HEAPU8.set(bytes, p);
      return fn(p, n);
    } finally {
      Module._free(p);
    }
  }

  function boot(title, text) {
    $('#boot-title').textContent = title;
    $('#boot-text').textContent = text || '';
    $('#boot').classList.toggle('hidden', !title);
  }
  const status = (text, progress) => { if (CCI.engineReady) call('cc_fe_status', null, ['string', 'number'], [text, progress === undefined ? -1 : progress]); };
  const overlay = (title, text) => { if (CCI.engineReady) call('cc_fe_overlay', null, ['string', 'string'], [title || '', text || '']); };
  const toast = (text) => { if (CCI.engineReady) call('cc_fe_toast', null, ['string'], [text]); };

  window.Module = {
    canvas,
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
    onAbort: (what) => boot('The engine stopped', String(what || 'unknown error') + ' — reload the page to try again.'),
  };

  CCI.onLog = (level, kind, msg) => { if (level >= 2) console.warn(msg); };

  CCI.onEngineReady = (ok) => {
    if (!ok) {
      boot('WebGL 2 is not available', 'This game needs a browser with WebGL 2 (current Chrome, Firefox, Safari or Edge).');
      return;
    }
    CCI.engineReady = true;
    boot('');
    canvas.focus();
    // Testing in a pane that gets no animation frames: the main loop on timers (?timer).
    if (new URLSearchParams(location.search).has('timer')) call('cc_debug_timer_loop', null, ['number'], [60]);
    defaultNetServer().then((u) => call('cc_netlink_set_default', null, ['string'], [u]));
    loadBundledGm();
    startDataLoad();
  };

  // ------------------------------------------------------------------ loading data

  const mb = (n) => (n / 1048576).toFixed(1) + ' MB';

  // The audio archives (about 260 MB) are read lazily: the engine reads their directory and small
  // data files at once and each sound file when a sample first needs it. From the dev server they
  // come by HTTP range requests (a server without them sends the whole file, which is kept as it
  // streams in); from the visitor's folder by slicing the File.
  class LazyArchive {
    constructor(name, size, src) {
      this.lazy = true;
      this.name = name;
      this.byteLength = size;
      this.src = src;        // {url} or {file}
      this.chunks = [];      // {off, end, bytes}
      this.inflight = new Map();
      this.fullMode = false; // the whole file, as far as it has arrived
      this.full = null;
      this.received = 0;
      this.failed = false;
    }
    find(off, len) {
      const end = off + len;
      if (this.fullMode) return end <= this.received ? this.full.subarray(off, end) : null;
      for (const c of this.chunks) if (off >= c.off && end <= c.end) return c.bytes.subarray(off - c.off, end - c.off);
      return null;
    }
    has(off, len) { return this.find(off, len) !== null; }
    read(off, len) { return this.find(off, len); }
    forget(off, len) {
      const i = this.chunks.findIndex((c) => c.off === off && c.end === off + len);
      if (i >= 0) this.chunks.splice(i, 1);
    }
    fetch(off, len) {
      if (this.fullMode || this.has(off, len)) return;
      const key = off + ':' + len;
      if (this.inflight.has(key)) return;
      const p = this.range(off, len)
        .then((b) => { if (b) this.chunks.push({ off, end: off + b.byteLength, bytes: new Uint8Array(b) }); })
        .catch((e) => console.warn(this.name + ': could not read bytes ' + off + '–' + (off + len) + ' (' + e.message + ')'))
        .finally(() => this.inflight.delete(key));
      this.inflight.set(key, p);
    }
    async range(off, len) {
      if (this.src.file) return await this.src.file.slice(off, off + len).arrayBuffer();
      const r = await fetch(this.src.url, { headers: { Range: 'bytes=' + off + '-' + (off + len - 1) } });
      if (r.status === 206) return await r.arrayBuffer();
      if (r.ok) {
        this.startFull(r);
        return null;
      }
      throw new Error('HTTP ' + r.status);
    }
    startFull(r) {
      if (this.fullMode) {
        try { r.body.cancel(); } catch (e) { /* ignore */ }
        return;
      }
      this.fullMode = true;
      this.full = new Uint8Array(this.byteLength);
      const reader = r.body.getReader();
      (async () => {
        try {
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            this.full.set(value.subarray(0, Math.max(0, Math.min(value.length, this.byteLength - this.received))), this.received);
            this.received += value.length;
          }
        } catch (e) {
          this.failed = true;
          console.warn(this.name + ': download failed (' + e.message + ')');
        }
      })();
    }
    until(n) {
      return new Promise((resolve, reject) => {
        const check = () => {
          if (this.received >= n) resolve();
          else if (this.failed) reject(new Error('download failed'));
          else setTimeout(check, 20);
        };
        check();
      });
    }
    async bytes(off, len) {
      if (!this.fullMode) {
        const b = await this.range(off, len);
        if (b) return new Uint8Array(b);
      }
      await this.until(off + len);
      return this.full.subarray(off, off + len);
    }
    // The header and the directory (the engine reads them when mounting).
    async init() {
      const head = await this.bytes(0, 16);
      const dv = new DataView(head.buffer, head.byteOffset, 16);
      const need = Math.min(this.byteLength, 0x800 + dv.getUint32(8, true) + dv.getUint32(12, true));
      const all = await this.bytes(0, need);
      if (!this.fullMode) this.chunks.push({ off: 0, end: need, bytes: all.slice() });
      return this;
    }
  }

  async function lazyArchive(name, size, src) {
    try {
      const a = await new LazyArchive(name, size, src).init();
      return { name, lazy: a };
    } catch (e) {
      console.warn(name + ': could not be read (' + e.message + ')');
      return null;
    }
  }

  CCI.devPlayers = null;
  async function startDataLoad() {
    const params = new URLSearchParams(location.search);
    if (!params.has('public')) {
      try {
        const r = await fetch('game/index.json', { cache: 'no-store' });
        if (r.ok) {
          const index = await r.json();
          const list = index.archives || [];
          if (index.gm) loadGmFromServer(index.gm);
          // The dev server's players folder (for testing the import) and its mmlang.dll (unless
          // this browser keeps the player's own).
          CCI.devPlayers = index.players && index.players.length ? index.players : null;
          // (2: the desktop app, whose local server serves the player's own MM2 folder: desktop/)
          call('cc_fe_dev_players', null, ['number'], [CCI.devPlayers ? (window.cciDesktop ? 2 : 1) : 0]);
          let saved = null;
          try { saved = localStorage.getItem('cci.mmlang'); } catch (e) { saved = null; }
          if (index.mmlang && !saved) {
            const m = await fetch('game/mmlang.dll');
            if (m.ok) {
              const bytes = new Uint8Array(await m.arrayBuffer());
              withBytes(bytes, (p, n) => call('cc_fe_load_strings', null, ['number', 'number', 'number'], [p, n, 0]));
            }
          }
          if (list.some((a) => !AUDIO.has(a.name.toLowerCase()))) return loadFromServer(list);
        }
      } catch (e) { /* no dev data: fall through to the folder picker */ }
    }
    status('Choose your Midtown Madness 2 folder to load its data.');
    overlay('Open your MM2 folder', 'Use “Open my MM2 folder” in the panel and pick the folder Midtown Madness 2 is installed in (the one with mm2core.ar and mm2tex.ar). Mod .ar files in it are loaded too.');
  }

  async function fetchWithProgress(url, expected, onBytes) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(url + ': HTTP ' + r.status);
    const total = Number(r.headers.get('content-length')) || expected || 0;
    if (!r.body || !total) {
      const buf = await r.arrayBuffer();
      onBytes(buf.byteLength);
      return buf;
    }
    const out = new Uint8Array(total);
    const reader = r.body.getReader();
    let got = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (got + value.length > out.length) throw new Error(url + ': larger than expected');
      out.set(value, got);
      got += value.length;
      onBytes(value.length);
    }
    return got === out.length ? out.buffer : out.slice(0, got).buffer;
  }

  async function loadFromServer(all) {
    const list = all.filter((a) => !AUDIO.has(a.name.toLowerCase()));
    const audio = all.filter((a) => AUDIO.has(a.name.toLowerCase()));
    const total = list.reduce((s, a) => s + (a.size || 0), 0);
    let done = 0;
    overlay('Loading game data…', list.map((a) => a.name).join(', '));
    status('Downloading ' + list.length + ' archives (' + mb(total) + ')…', 0);
    const lazy = audio.map((a) => lazyArchive(a.name, a.size, { url: 'game/' + encodeURIComponent(a.name) }));
    const results = await Promise.all(list.map(async (a) => {
      try {
        const buf = await fetchWithProgress('game/' + encodeURIComponent(a.name), a.size, (n) => {
          done += n;
          status('Downloading… ' + mb(done) + ' of ' + mb(total), total ? done / total : 0);
        });
        return { name: a.name, buf };
      } catch (e) {
        console.warn(a.name + ': could not be downloaded (' + e.message + ')');
        return null;
      }
    }));
    const lazyResults = await Promise.all(lazy);
    replaceArchives(results.concat(lazyResults).filter(Boolean), window.cciDesktop ? 'your MM2 folder' : 'dev server');  // desktop app: desktop/
  }

  async function readFiles(all) {
    // The audio archives stay in the File and are read lazily.
    const files = all.filter((f) => !AUDIO.has(f.name.toLowerCase()));
    const total = files.reduce((s, f) => s + f.size, 0);
    let done = 0;
    overlay('Reading archives…', files.map((f) => f.name).join(', '));
    const out = [];
    for (const f of all) {
      if (!AUDIO.has(f.name.toLowerCase())) continue;
      const a = await lazyArchive(f.name, f.size, { file: f });
      if (a) out.push(a);
    }
    for (const f of files) {
      status('Reading ' + f.name + ' (' + mb(f.size) + ')…', total ? done / total : 0);
      try {
        out.push({ name: f.name, buf: await f.arrayBuffer() });
      } catch (e) {
        console.warn(f.name + ': could not be read (' + e.message + ')');
      }
      done += f.size;
    }
    return out;
  }

  function replaceArchives(list, source) {
    CCI.archives = [];
    CCI.loaded = [];
    addArchives(list, source);
  }

  function addArchives(list, source) {
    for (const a of list) {
      const existing = CCI.loaded.findIndex((l) => l.name.toLowerCase() === a.name.toLowerCase());
      const id = CCI.archives.length;
      const src = a.buf || a.lazy;
      CCI.archives.push(src);
      const entry = { name: a.name, id, size: src.byteLength };
      if (existing >= 0) {
        CCI.archives[CCI.loaded[existing].id] = null;
        CCI.loaded[existing] = entry;
      } else {
        CCI.loaded.push(entry);
      }
    }
    mountAll(source);
  }

  // The engine mounts them; its front end refreshes the viewer's lists and the profiles.
  function mountAll(source) {
    if (!CCI.engineReady) return;
    if (source) call('cc_fe_source', null, ['string'], [source]);
    status('Reading archive directories…', -1);
    call('cc_clear_archives', null);
    for (const a of CCI.loaded) call('cc_add_archive', null, ['string', 'number', 'number'], [a.name, a.id, a.size]);
    callJson('cc_mount');
  }

  // ------------------------------------------------------------------ the file pickers

  const archiveDepth = (f) => (f.webkitRelativePath || f.name).split('/').length;
  let heldPlayers = null;  // the players folder of the chosen game folder, until the front end imports it

  $('#pick-folder').addEventListener('change', async (ev) => {
    const files = Array.from(ev.target.files || []);
    ev.target.value = '';
    // mmlang.dll and the players folder.
    const dll = files.find((f) => /(^|\/)mmlang\.dll$/i.test(f.webkitRelativePath || f.name));
    if (dll) {
      try {
        const bytes = new Uint8Array(await dll.arrayBuffer());
        withBytes(bytes, (p, n) => call('cc_fe_load_strings', null, ['number', 'number', 'number'], [p, n, 1]));
      } catch (e) { /* unreadable */ }
    }
    const dir = files.filter((f) => /(^|\/)players\//i.test(f.webkitRelativePath || ''));
    if (dir.length) {
      heldPlayers = dir;
      call('cc_fe_pending_players', null, ['number'], [1]);
    }
    const ars = files.filter((f) => /\.ar$/i.test(f.name));
    if (!ars.length) {
      status('No .ar archives in that folder. Pick the folder that holds mm2core.ar.');
      console.warn('the chosen folder has no .ar archives');
      return;
    }
    // MM2 reads the archives in its own folder: keep the shallowest ones.
    const minDepth = Math.min(...ars.map(archiveDepth));
    const top = ars.filter((f) => archiveDepth(f) === minDepth);
    if (!top.some((f) => f.name.toLowerCase() === 'mm2core.ar')) console.warn('mm2core.ar is not in the chosen folder; cars and cities need it');
    replaceArchives(await readFiles(top), 'your folder');
    // The player's own General MIDI bank, if they put gm.dls in the folder (music.md, "General MIDI").
    const gm = files.find((f) => f.name.toLowerCase() === 'gm.dls');
    if (gm) setGmBank(gm.name, await gm.arrayBuffer());
  });

  $('#pick-mods').addEventListener('change', async (ev) => {
    const files = Array.from(ev.target.files || []).filter((f) => /\.ar$/i.test(f.name));
    ev.target.value = '';
    if (files.length) addArchives(await readFiles(files));
  });

  $('#pick-players').addEventListener('change', async (ev) => {
    const files = Array.from(ev.target.files || []);
    ev.target.value = '';
    importFiles(files);
  });

  $('#pick-gm').addEventListener('change', async (ev) => {
    const f = (ev.target.files || [])[0];
    ev.target.value = '';
    if (f) setGmBank(f.name, await f.arrayBuffer());
  });

  // A players folder into the engine (profiles.md, "Importing an MM2 profile"): every file below
  // players.dir's folder with its path relative to it, and mmlang.dll next to that folder.
  async function importFiles(files) {
    const rel = (f) => (f.webkitRelativePath || f.name).replace(/\\/g, '/');
    const dirFile = files.find((f) => /(^|\/)players\.dir$/i.test(rel(f)));
    if (!dirFile) {
      toast('No players.dir in that folder: pick your MM2 folder or its players folder.');
      return;
    }
    const base = rel(dirFile).slice(0, rel(dirFile).length - 'players.dir'.length);
    const mine = files.filter((f) => rel(f).toLowerCase().startsWith(base.toLowerCase()) && f.size < 4 * 1048576);
    call('cc_ui_import_begin', null);
    for (const f of mine) {
      const bytes = new Uint8Array(await f.arrayBuffer());
      withBytes(bytes, (p, n) => call('cc_ui_import_file', null, ['string', 'number', 'number'], [rel(f).slice(base.length), p, n]));
    }
    const parent = base.replace(/players\/$/i, '');
    const dll = files.find((f) => rel(f).toLowerCase() === (parent + 'mmlang.dll').toLowerCase());
    if (dll) {
      const bytes = new Uint8Array(await dll.arrayBuffer());
      withBytes(bytes, (p, n) => call('cc_fe_load_strings', null, ['number', 'number', 'number'], [p, n, 1]));
    }
    call('cc_fe_import_done', null);
  }
  async function importDev() {
    if (!CCI.devPlayers) return;
    call('cc_ui_import_begin', null);
    for (const p of CCI.devPlayers) {
      try {
        const r = await fetch('game/players/' + p.split('/').map(encodeURIComponent).join('/'));
        if (!r.ok) continue;
        const bytes = new Uint8Array(await r.arrayBuffer());
        withBytes(bytes, (ptr, n) => call('cc_ui_import_file', null, ['string', 'number', 'number'], [p, ptr, n]));
      } catch (e) { /* skip */ }
    }
    call('cc_fe_import_done', null);
  }

  // Drag and drop .ar files onto the view.
  window.addEventListener('dragover', (e) => { e.preventDefault(); call('cc_fe_drop', null, ['number'], [1]); });
  window.addEventListener('dragleave', () => call('cc_fe_drop', null, ['number'], [0]));
  window.addEventListener('drop', async (e) => {
    e.preventDefault();
    call('cc_fe_drop', null, ['number'], [0]);
    const files = Array.from(e.dataTransfer.files || []).filter((f) => /\.ar$/i.test(f.name));
    if (files.length) addArchives(await readFiles(files));
  });

  // ------------------------------------------------------------------ General MIDI bank

  function setGmBank(name, buf, bundled) {
    if (!CCI.engineReady) return;
    let id = -1, size = 0;
    if (buf) {
      id = CCI.archives.length;
      CCI.archives.push(buf);
      size = buf.byteLength;
    }
    callJson('cc_music_set_gm', ['string', 'number', 'number', 'number'], [name, id, size, bundled ? 1 : 0]);
    if (id >= 0) CCI.archives[id] = null;  // the engine copied what it needs
  }
  // The bundled fallback (FluidR3 GM's drum kit and piano, MIT; gm/COPYING-FluidR3.txt), used
  // when the player has not added their own gm.dls.
  async function loadBundledGm() {
    try {
      const res = await fetch('gm/FluidR3_GM_drums_piano.sf2');
      if (res.ok) setGmBank('FluidR3_GM_drums_piano.sf2', await res.arrayBuffer(), true);
    } catch (e) { /* optional */ }
  }
  async function loadGmFromServer(gm) {
    try {
      const res = await fetch('game/' + encodeURIComponent(gm.name));
      if (res.ok) setGmBank(gm.name, await res.arrayBuffer());
    } catch (e) { /* optional */ }
  }

  // ------------------------------------------------------------------ the engine's hooks (engine/app/web_host.cpp)

  let textActive = false;
  window.CCIShell = {
    // 0 game folder, 1 mod archives, 2 a players folder, 3 the dev server's players folder,
    // 4 the players folder of the game folder chosen before, 5 a General MIDI bank.
    pick(kind) {
      if (kind === 0) $('#pick-folder').click();
      else if (kind === 1) $('#pick-mods').click();
      else if (kind === 2) $('#pick-players').click();
      else if (kind === 3) importDev();
      else if (kind === 4) { if (heldPlayers) importFiles(heldPlayers); heldPlayers = null; }
      else if (kind === 5) $('#pick-gm').click();
      else return false;
      return true;
    },
    canPick(kind) { return kind === 3 ? !!CCI.devPlayers : kind === 4 ? !!heldPlayers : kind >= 0 && kind <= 5; },
    setClipboard(text) {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).catch(() => fallbackCopy(text));
      } else {
        fallbackCopy(text);
      }
    },
    textInput(on) { textActive = !!on; },
  };
  function fallbackCopy(text) {
    const t = document.createElement('textarea');
    t.value = text;
    t.style.position = 'fixed';
    t.style.opacity = '0';
    document.body.appendChild(t);
    t.select();
    try { document.execCommand('copy'); } catch (e) { /* no clipboard */ }
    t.remove();
    canvas.focus();
  }

  // Clipboard shortcuts in a text field: the browser's own paste, copy and cut events carry the
  // text (the engine's key handling would swallow the shortcut otherwise).
  window.addEventListener('keydown', (e) => {
    if (!textActive || !(e.ctrlKey || e.metaKey)) return;
    const k = e.key.toLowerCase();
    if (k === 'v' || k === 'c' || k === 'x') e.stopPropagation();
  }, true);
  document.addEventListener('paste', (e) => {
    if (!textActive) return;
    const t = (e.clipboardData || window.clipboardData).getData('text');
    e.preventDefault();
    if (t) call('cc_ui_clipboard', null, ['number', 'string'], [0, t]);
  });
  document.addEventListener('copy', (e) => { if (textActive) { e.preventDefault(); call('cc_ui_clipboard', null, ['number', 'string'], [1, '']); } });
  document.addEventListener('cut', (e) => { if (textActive) { e.preventDefault(); call('cc_ui_clipboard', null, ['number', 'string'], [2, '']); } });

  // The keyboard goes to the canvas (SDL 3 reads the pointer events, each with its position).
  canvas.addEventListener('pointerdown', () => canvas.focus());
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());

  // ------------------------------------------------------------------ network games

  // The session server: ?net=…, else this page's own server when it is the network server
  // (./build.sh netserver), else port 8821 of this host.
  async function defaultNetServer() {
    const p = new URLSearchParams(location.search);
    if (p.get('net')) return p.get('net');
    const wsProto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    try {
      const r = await fetch('/net/info', { cache: 'no-store' });
      if (r.ok) {
        const j = await r.json();
        if (j && j.cci) return wsProto + '//' + location.host + '/net';
      }
    } catch (e) { /* not served by the network server */ }
    return wsProto + '//' + (location.hostname || '127.0.0.1') + ':8821/net';
  }
  if (window.CCINet) {
    CCINet.onSessions = (l) => call('cc_netlink_sessions', null, ['string'], [JSON.stringify(l || [])]);
  }

  // A network game does not pause (multiplayer.md 1.5): while the page is hidden the browser runs
  // its frames rarely, so the game makes up the time its own loop did not cover in fixed 1/60 s
  // frames.
  let lastTick = performance.now(), lastGame = null, deficit = 0;
  setInterval(() => {
    const now = performance.now();
    const elapsed = (now - lastTick) / 1000;
    lastTick = now;
    const g = CCI.engineReady && window.Module && Module._cc_net_game_time ? Module._cc_net_game_time() : -1;
    if (g < 0) {
      lastGame = null;
      deficit = 0;
      return;
    }
    if (lastGame === null) { lastGame = g; return; }
    deficit = Math.min(2, Math.max(0, deficit + elapsed - (g - lastGame)));
    lastGame = g;
    if (!document.hidden) { deficit = 0; return; }
    const n = Math.min(120, Math.floor(deficit * 60));
    if (n > 0) {
      call('cc_step_frames', null, ['number', 'number'], [n, 1 / 60]);
      deficit -= n / 60;
      lastGame = Module._cc_net_game_time();
    }
  }, 200);

  // ------------------------------------------------------------------ scripted checks

  // The UI's interactive nodes (labels and places in CSS px) and real input on the canvas.
  window.CCITest = {
    tree: () => callJson('cc_ui_tree'),
    find(label) {
      const t = callJson('cc_ui_tree');
      return t ? t.nodes.filter((n) => n.label === label) : [];
    },
    at(label, index) {
      const n = this.find(label)[index || 0];
      if (!n || n.vw < 2 || n.vh < 2) return null;
      const r = canvas.getBoundingClientRect();
      return { x: r.left + n.vx + n.vw / 2, y: r.top + n.vy + n.vh / 2 };
    },
    // Scrolls the node's list (wheel over it) until the node shows.
    reveal(label, index) {
      for (let k = 0; k < 30; k++) {
        const n = this.find(label)[index || 0];
        if (!n) return false;
        if (n.vh >= Math.min(n.h, 8) - 0.5 && n.vw > 1) return true;
        const r = canvas.getBoundingClientRect();
        const down = n.vh < 1 ? n.y > 200 : n.vy > n.y;
        // Wheel over the node's own column, just inside its scrolling box.
        const x = r.left + n.x + Math.min(20, n.w / 2);
        const y = r.top + (n.y > 0 && n.y < innerHeight * 0.6 ? n.y + 4 : innerHeight * 0.45);
        canvas.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientX: x, clientY: y, pointerId: 1, pointerType: 'mouse', isPrimary: true }));
        canvas.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: x, clientY: y, deltaY: down ? 120 : -120, deltaMode: 0 }));
        this.frames(2);
      }
      return false;
    },
    click(label, index) {
      this.reveal(label, index);
      const p = this.at(label, index);
      if (!p) return false;
      // Pointer events, as SDL 3 reads them; a frame between them, as a hand would leave.
      const o = { bubbles: true, cancelable: true, clientX: p.x, clientY: p.y, button: 0, buttons: 1, view: window, pointerId: 1, pointerType: 'mouse', isPrimary: true };
      canvas.dispatchEvent(new PointerEvent('pointermove', Object.assign({}, o, { button: -1 })));
      this.frames(1);
      canvas.dispatchEvent(new PointerEvent('pointerdown', o));
      this.frames(1);
      canvas.dispatchEvent(new PointerEvent('pointerup', Object.assign({}, o, { buttons: 0 })));
      this.frames(2);
      return true;
    },
    // A key press; {ctrlKey: true} holds Control around it (SDL keeps its own modifier state from
    // the modifier keys' own events).
    key(key, code, opts) {
      const o = Object.assign({ key, code: code || key, bubbles: true, cancelable: true }, opts || {});
      const ctrl = { key: 'Control', code: 'ControlLeft', bubbles: true, cancelable: true, ctrlKey: true };
      canvas.focus();
      if (o.ctrlKey) { canvas.dispatchEvent(new KeyboardEvent('keydown', ctrl)); this.frames(1); }
      canvas.dispatchEvent(new KeyboardEvent('keydown', o));
      canvas.dispatchEvent(new KeyboardEvent('keyup', o));
      if (o.ctrlKey) { this.frames(1); canvas.dispatchEvent(new KeyboardEvent('keyup', Object.assign({}, ctrl, { ctrlKey: false }))); }
    },
    type(text) { call('cc_ui_text', null, ['string'], [text]); },
    screen: () => call('cc_ui_screen', 'string'),
    // n frames now: the first one reads the waiting input, the rest are 1/60 s steps.
    frames(n) {
      call('cc_frame_now', null);
      if (n > 1) call('cc_step_frames', null, ['number', 'number'], [n - 1, 1 / 60]);
    },
  };
})();
