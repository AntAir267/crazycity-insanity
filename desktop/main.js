// CrazyCity Insanity desktop app (Electron main process).
//
// The window shows the same page as the browser build (index.html?play: the front end). The page
// comes from a small server in this process on 127.0.0.1: the dev signalling and relay server
// (server/signal.js) serving the bundled web build with byte ranges and the WebSocket /net for
// network games, plus game/ made from the player's own Midtown Madness 2 folder (game_folder.js),
// read from disk as the page asks. No MM2 file is part of the app.
//
//   crazycity-insanity [--mm2 <MM2 folder>] [--selftest <out.png>]
//
// Settings live in ~/.config/crazycity-insanity/config.json (the MM2 folder, the network setting,
// the port, the window size); the page's own storage (drivers, records, options) is Chromium's
// localStorage in the same folder. The port stays the same from run to run, because the page's
// storage belongs to its address (http://127.0.0.1:<port>).
'use strict';

const { app, BrowserWindow, Menu, dialog, ipcMain, screen, session, shell } = require('electron');
const fs = require('fs');
const os = require('os');
const path = require('path');
const folder = require('./game_folder.js');
const { createLocalServer } = require('./local_server.js');

const APP_ID = 'crazycity-insanity';
const TITLE = 'CrazyCity Insanity';
const DEFAULT_PORT = 28620;
const BG = '#0f1115';  // the page's background (web/style.css --bg)

// ------------------------------------------------------------------ arguments and folders

function parseArgs(argv) {
  const out = { selftest: '', mm2: '' };
  for (let i = 1; i < argv.length; i++) {
    const a = argv[i];
    for (const name of ['selftest', 'mm2']) {
      if (a === '--' + name) out[name] = argv[++i] || '';
      else if (a.startsWith('--' + name + '=')) out[name] = a.slice(name.length + 3);
      else continue;
      if (name === 'selftest' && !out.selftest) out.selftest = 'cci-selftest.png';
    }
  }
  return out;
}

const ARGS = parseArgs(process.argv);
const SELFTEST = !!ARGS.selftest;
const CONFIG_DIR = path.join(app.getPath('appData'), APP_ID);
const CONFIG_FILE = path.join(CONFIG_DIR, 'config.json');
let tempProfile = '';
if (SELFTEST) {
  // The self-test reads the remembered MM2 folder but keeps its own throwaway browser profile, so
  // the player's drivers and records are never touched (and a running copy of the game is no bother).
  tempProfile = fs.mkdtempSync(path.join(os.tmpdir(), APP_ID + '-selftest-'));
  app.setPath('userData', tempProfile);
  app.commandLine.appendSwitch('disable-http-cache');  // nothing written there at exit
} else {
  app.setPath('userData', CONFIG_DIR);
}

function webRoot() {
  const packaged = path.join(__dirname, '..', 'web');  // the AppImage: resources/app/web
  if (fs.existsSync(path.join(packaged, 'cci.wasm'))) return packaged;
  return path.join(__dirname, '..', 'dist');  // a checkout after ./build.sh web
}
const WEB_ROOT = webRoot();

function iconFile() {
  const p = path.join(__dirname, 'icon.png');  // made from icon.svg by ./build.sh appimage
  return fs.existsSync(p) ? p : undefined;
}

// ------------------------------------------------------------------ config

function loadConfig() {
  try {
    const c = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8'));
    return c && typeof c === 'object' ? c : {};
  } catch (e) {
    return {};
  }
}
const cfg = loadConfig();

function saveConfig() {
  if (SELFTEST) return;
  try {
    fs.mkdirSync(CONFIG_DIR, { recursive: true });
    const tmp = CONFIG_FILE + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(cfg, null, 2) + '\n');
    fs.renameSync(tmp, CONFIG_FILE);
  } catch (e) {
    console.error('could not save ' + CONFIG_FILE + ': ' + e.message);
  }
}

// ------------------------------------------------------------------ the local server

function netLog(...a) {
  if (!SELFTEST) console.log(new Date().toISOString().slice(11, 19), ...a);
}
const local = createLocalServer({ webRoot: WEB_ROOT, log: netLog });
const S = { portNote: '' };

// The remembered port; another one (with a note) when a different program holds it.
async function startServer(port) {
  const host = cfg.lan && !SELFTEST ? '0.0.0.0' : '127.0.0.1';
  try {
    await local.start(port, host);
  } catch (e) {
    if (!port || (e.code !== 'EADDRINUSE' && e.code !== 'EACCES')) throw e;
    await local.start(0, host);
    S.portNote = 'Port ' + port + ' is in use by another program, so this run uses port ' + local.port() +
      '. Drivers, records and options are kept per port: the ones saved on port ' + port + ' come back once it is free again.';
  }
  if (!SELFTEST && !cfg.port && !S.portNote) { cfg.port = local.port(); saveConfig(); }
}

function pageOrigin() { return 'http://127.0.0.1:' + local.port(); }
function isOurs(u) {
  try { return new URL(u).origin === pageOrigin(); } catch (e) { return false; }
}

function lanAddresses() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces())) {
    for (const a of list || []) if (a.family === 'IPv4' && !a.internal) out.push(a.address);
  }
  return out;
}

// ------------------------------------------------------------------ the window

let win = null;

function createWindow() {
  const wa = screen.getPrimaryDisplay().workAreaSize;
  const w = cfg.window || {};
  win = new BrowserWindow({
    title: TITLE,
    width: Math.min(Number(w.width) || 1280, wa.width),
    height: Math.min(Number(w.height) || 800, wa.height),
    minWidth: 800,
    minHeight: 560,
    show: false,
    backgroundColor: BG,
    icon: iconFile(),
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      spellcheck: false,
      backgroundThrottling: !SELFTEST,
      offscreen: SELFTEST,  // the self-test draws without a window on the screen
    },
  });
  win.removeMenu();
  if (!SELFTEST && w.maximized) win.maximize();
  if (!SELFTEST && w.fullscreen) win.setFullScreen(true);
  win.on('page-title-updated', (e) => { e.preventDefault(); win.setTitle(TITLE); });

  const wc = win.webContents;
  // Only our own page: other addresses open in the system browser.
  wc.on('will-navigate', (e, url) => {
    if (isOurs(url)) return;
    e.preventDefault();
    if (/^https?:\/\//i.test(url)) shell.openExternal(url);
  });
  wc.setWindowOpenHandler(({ url }) => {
    if (/^https?:\/\//i.test(url) && !isOurs(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  wc.on('before-input-event', (e, input) => {
    if (input.type !== 'keyDown' || input.isAutoRepeat) return;
    const plain = !input.control && !input.alt && !input.shift && !input.meta;
    const ctrlShift = input.control && input.shift && !input.alt && !input.meta;
    if (input.code === 'F11' && plain) {
      e.preventDefault();
      win.setFullScreen(!win.isFullScreen());
    } else if (ctrlShift && input.code === 'KeyO') {
      e.preventDefault();
      changeFolder();
    } else if (ctrlShift && input.code === 'KeyR') {
      e.preventDefault();
      wc.reload();
    } else if (ctrlShift && input.code === 'KeyI') {
      e.preventDefault();
      wc.toggleDevTools();
    }
  });
  if (!SELFTEST) {
    wc.on('render-process-gone', async (e, details) => {
      if (details.reason === 'clean-exit') return;
      const r = await dialog.showMessageBox(win, {
        type: 'error', title: TITLE, message: 'The game stopped (' + details.reason + ').',
        buttons: ['Reload', 'Quit'], defaultId: 0,
      });
      if (r.response === 0) loadPage(); else app.quit();
    });
    win.on('close', () => {
      const b = win.getNormalBounds();
      cfg.window = { width: b.width, height: b.height, maximized: win.isMaximized(), fullscreen: win.isFullScreen() };
      saveConfig();
    });
    win.show();
  }
}

function loadPage() {
  win.loadURL(pageOrigin() + '/?play');
}

// ------------------------------------------------------------------ the MM2 folder

let choosing = false;

// Asks for the MM2 folder (reason: 'first', 'missing' or 'change'); true when a new one was taken.
async function chooseFolder(reason) {
  if (choosing) return false;
  choosing = true;
  try {
    if (reason === 'first' || reason === 'missing') {
      const first = reason === 'first';
      const r = await dialog.showMessageBox(win, {
        type: first ? 'info' : 'warning',
        title: TITLE,
        message: first ? 'CrazyCity Insanity plays with your own Midtown Madness 2' : 'Your Midtown Madness 2 folder was not found',
        detail: first
          ? 'Choose the folder Midtown Madness 2 is installed in: the one with mm2core.ar and mm2tex.ar. The game reads its files from there and copies nothing; mod .ar files in that folder are loaded too.'
          : folder.describe(local.scan()) + '\n\nIf it is on a drive that is not connected, connect it and try again, or choose the folder again.',
        buttons: first ? ['Choose folder…', 'Not now'] : ['Choose folder…', 'Try again', 'Not now'],
        defaultId: 0,
        cancelId: first ? 1 : 2,
      });
      if (!first && r.response === 1) {
        const again = folder.scanFolder(local.dir());
        if (again.ok) { local.setFolder(again); return true; }
        choosing = false;
        return chooseFolder('missing');
      }
      if (r.response !== 0) return false;
    }
    for (;;) {
      const pick = await dialog.showOpenDialog(win, {
        title: 'Choose your Midtown Madness 2 folder',
        defaultPath: local.dir() && fs.existsSync(local.dir()) ? local.dir() : os.homedir(),
        buttonLabel: 'Use this folder',
        properties: ['openDirectory', 'dontAddToRecent'],
      });
      if (pick.canceled || !pick.filePaths.length) return false;
      const scan = folder.findGameFolder(pick.filePaths[0]);
      if (scan.ok) {
        local.setFolder(scan);
        cfg.mm2Folder = scan.dir;
        saveConfig();
        return true;
      }
      const m = await dialog.showMessageBox(win, {
        type: 'warning', title: TITLE,
        message: 'That folder has no Midtown Madness 2 archives',
        detail: pick.filePaths[0] + ' has no ' + scan.missing.join(' or ') + '. Pick the folder Midtown Madness 2 is installed in (the one with mm2core.ar and mm2tex.ar).',
        buttons: ['Choose another folder', 'Cancel'], defaultId: 0, cancelId: 1,
      });
      if (m.response !== 0) return false;
    }
  } finally {
    choosing = false;
  }
}

async function changeFolder() {
  if (await chooseFolder('change')) loadPage();
}

// ------------------------------------------------------------------ the page's desktop panel (preload.js)

function info() {
  const lan = !!cfg.lan;
  return {
    version: app.getVersion(),
    folder: local.dir(),
    folderOk: !!(local.scan() && local.scan().ok),
    folderLine: folder.describe(local.scan()),
    lan,
    port: local.port(),
    lanPages: lan ? lanAddresses().map((a) => 'http://' + a + ':' + local.port() + '/?play') : [],
    lanServers: lan ? lanAddresses().map((a) => 'ws://' + a + ':' + local.port() + '/net') : [],
    portNote: S.portNote,
  };
}

function fromPage(e) {
  return !!(e.senderFrame && isOurs(e.senderFrame.url));
}

async function setLan(on) {
  on = !!on;
  if (on === !!cfg.lan) return info();
  if (on) {
    const addrs = lanAddresses();
    const r = await dialog.showMessageBox(win, {
      type: 'question', title: TITLE,
      message: 'Let friends on your network join?',
      detail: 'Your network games are then open to the computers on your network (not the internet). Friends join from their own copy of CrazyCity Insanity with the server ' +
        (addrs.length ? addrs.map((a) => 'ws://' + a + ':' + local.port() + '/net').join(' or ') : '(no network address found)') +
        ', or from a browser at ' + (addrs.length ? addrs.map((a) => 'http://' + a + ':' + local.port() + '/?play').join(' or ') : 'your address') +
        ' with their own MM2 folder. Your MM2 files are never sent to them.\n\nThe network server restarts: a network game in progress ends.',
      buttons: ['Let them join', 'Cancel'], defaultId: 0, cancelId: 1,
    });
    if (r.response !== 0) return info();
  }
  cfg.lan = on;
  saveConfig();
  // The same port again, so the page keeps its address (and its storage).
  const port = local.port();
  await local.stop();
  await startServer(port);
  loadPage();
  return info();
}

ipcMain.handle('cci:info', (e) => (fromPage(e) ? info() : null));
ipcMain.handle('cci:choose-folder', async (e) => {
  if (!fromPage(e)) return null;
  if (await chooseFolder('change')) setTimeout(loadPage, 0);
  return info();
});
ipcMain.handle('cci:set-lan', (e, on) => (fromPage(e) ? setLan(on) : null));
// The page's own "Open my MM2 folder" picked a folder: remember it when it holds MM2.
ipcMain.on('cci:folder-picked', (e, dir) => {
  if (!fromPage(e) || typeof dir !== 'string' || !path.isAbsolute(dir)) return;
  const scan = folder.scanFolder(dir);
  if (!scan.ok) return;
  local.setFolder(scan);
  if (cfg.mm2Folder !== scan.dir) { cfg.mm2Folder = scan.dir; saveConfig(); }
});

// ------------------------------------------------------------------ session rules

const PERMISSIONS = new Set(['pointerLock', 'fullscreen', 'clipboard-sanitized-write']);
const CSP = "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; " +
  "img-src 'self' data: blob:; media-src 'self' data: blob:; font-src 'self' data:; connect-src 'self' ws: wss:; " +
  "worker-src 'self' blob:; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

function setupSession() {
  const ses = session.defaultSession;
  ses.setPermissionRequestHandler((wc, permission, cb, details) => cb(PERMISSIONS.has(permission) && isOurs(details.requestingUrl || wc.getURL())));
  ses.setPermissionCheckHandler((wc, permission, origin) => PERMISSIONS.has(permission) && isOurs(origin));
  ses.webRequest.onHeadersReceived((details, cb) => {
    if (details.resourceType === 'mainFrame' && isOurs(details.url)) {
      cb({ responseHeaders: Object.assign({}, details.responseHeaders, { 'Content-Security-Policy': [CSP] }) });
    } else {
      cb({});
    }
  });
}

// ------------------------------------------------------------------ start

function removeTempProfile() {
  if (tempProfile) { try { fs.rmSync(tempProfile, { recursive: true, force: true }); } catch (e) { /* in use */ } }
}

// The self-test's end. Chromium still writes a few files into its profile as it exits, so a
// small detached shell removes the throwaway profile a moment later.
function finishSelftest(code) {
  try { win.destroy(); } catch (e) { /* gone */ }
  setTimeout(() => {
    removeTempProfile();
    try {
      require('child_process').spawn('/bin/sh', ['-c', 'sleep 3; rm -rf -- "$0"', tempProfile], { detached: true, stdio: 'ignore' }).unref();
    } catch (e) { /* left in the temp folder */ }
    app.exit(code);
  }, 300);
}

if (!SELFTEST && !app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (!win) return;
    if (win.isMinimized()) win.restore();
    win.focus();
  });
  app.on('window-all-closed', () => { if (!SELFTEST) app.quit(); });
  app.on('will-quit', removeTempProfile);
  app.whenReady().then(main).catch((e) => {
    console.error(e && e.stack ? e.stack : e);
    if (!SELFTEST) dialog.showErrorBox(TITLE, 'The game could not start: ' + (e && e.message ? e.message : e));
    app.exit(1);
  });
}

async function main() {
  Menu.setApplicationMenu(null);
  setupSession();
  if (ARGS.mm2) local.setFolder(folder.findGameFolder(path.resolve(ARGS.mm2)));
  else local.setFolder(folder.scanFolder(cfg.mm2Folder || ''));
  await startServer(SELFTEST ? 0 : Number(cfg.port) || DEFAULT_PORT);
  createWindow();
  if (SELFTEST) {
    require('./selftest.js').run({ win, args: ARGS, scan: local.scan(), folder, pageUrl: pageOrigin() + '/?play', version: app.getVersion() })
      .then(finishSelftest, (e) => { console.error('self-test: ' + (e && e.stack ? e.stack : e)); finishSelftest(1); });
    return;
  }
  if (!local.scan().ok) await chooseFolder(local.dir() ? 'missing' : 'first');
  loadPage();
  if (S.portNote) dialog.showMessageBox(win, { type: 'warning', title: TITLE, message: 'Another port this time', detail: S.portNote });
}
