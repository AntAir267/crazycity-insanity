// CrazyCity Insanity desktop app: --selftest <out.png> [--mm2 <dir>].
//
// Runs the page in a hidden window with the remembered (or given) MM2 folder, waits for the
// engine-drawn front end's main menu with the data loaded, starts a Cruise drive through the front
// end and holds the accelerator for a few seconds, saves a capture of the page (and one of the menu
// beside it, "<out>-menu.png"), prints a short status and gives exit code 1 when something failed.
// The page is the engine's own UI on a canvas (app/front_end.*): the self-test reads it through the
// engine's calls (cc_ui_screen, cc_ui_state, cc_city_list, cc_car_list, cc_fe_play, cc_drive_debug).
// CCI_SELFTEST_DEBUG=1 prints each step.
'use strict';

const fs = require('fs');
const path = require('path');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const debug = (...a) => { if (process.env.CCI_SELFTEST_DEBUG) console.error('[debug]', ...a); };

// Waits until the page's `expr` is truthy; the last value, or null after `ms`.
async function until(wc, expr, ms) {
  const end = Date.now() + ms;
  let v = null;
  while (Date.now() < end) {
    try { v = await wc.executeJavaScript(expr, true); } catch (e) { v = null; }
    if (v && (typeof v !== 'object' || v.done)) return v;
    await sleep(250);
  }
  return null;
}

// How many different colours a grid of samples of the capture has (a blank page has one or two).
function colours(img) {
  const { width, height } = img.getSize();
  if (!width || !height) return 0;
  const bmp = img.toBitmap();
  const seen = new Set();
  for (let y = 0; y < 24; y++) {
    for (let x = 0; x < 32; x++) {
      const px = Math.floor((x + 0.5) * width / 32), py = Math.floor((y + 0.5) * height / 24);
      const i = (py * width + px) * 4;
      seen.add((bmp[i] >> 3) + ',' + (bmp[i + 1] >> 3) + ',' + (bmp[i + 2] >> 3));
    }
  }
  return seen.size;
}

function sandboxState(pid) {
  try {
    const st = fs.readFileSync('/proc/' + pid + '/status', 'utf8');
    const sec = /Seccomp:\s*(\d)/.exec(st);
    let ownNs = false;
    try { ownNs = fs.readlinkSync('/proc/' + pid + '/ns/user') !== fs.readlinkSync('/proc/self/ns/user'); } catch (e) { ownNs = true; }
    const on = sec && sec[1] === '2';
    return on ? 'on (seccomp filter' + (ownNs ? ', own user namespace' : '') + ')' : 'off (--no-sandbox)';
  } catch (e) {
    return 'unknown';
  }
}

async function capture(wc, file) {
  const img = await wc.capturePage();
  const size = img.getSize();
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, img.toPNG());
  return { file, width: size.width, height: size.height, colours: colours(img) };
}

async function run({ win, args, scan, folder, pageUrl, version }) {
  const t0 = Date.now();
  const secs = () => ((Date.now() - t0) / 1000).toFixed(1) + ' s';
  const out = path.resolve(args.selftest);
  const menuOut = out.replace(/(\.png)?$/i, '-menu.png');
  const fails = [];
  const consoleErrors = [];
  const consoleWarnings = [];  // the engine's warnings and errors go to the console (CCI.onLog)
  const wc = win.webContents;
  wc.setAudioMuted(true);
  wc.on('console-message', (e) => {
    if (e.level === 'error') consoleErrors.push(e.message);
    else if (e.level === 'warning') consoleWarnings.push(e.message);
  });
  let gone = '';
  wc.on('render-process-gone', (e, d) => { gone = d.reason; });

  console.log('CrazyCity Insanity ' + version + ' self-test');
  console.log('  MM2 folder:   ' + folder.describe(scan));
  if (!scan || !scan.ok) fails.push('no usable MM2 folder (pass --mm2 <dir> or choose one in the app)');

  await win.loadURL(pageUrl);
  console.log('  renderer sandbox: ' + sandboxState(wc.getOSProcessId()));

  // The engine, then the data, then the front end's main menu.
  const engine = await until(wc, 'window.CCI && CCI.engineReady', 60000);
  if (!engine) fails.push('the engine did not start' + (gone ? ' (renderer ' + gone + ')' : ''));
  const gl = await wc.executeJavaScript(`(() => { try { const g = document.createElement('canvas').getContext('webgl2');
    const d = g && g.getExtension('WEBGL_debug_renderer_info'); return g ? (d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : g.getParameter(g.RENDERER)) : 'no WebGL 2'; } catch (e) { return String(e); } })()`, true).catch(() => '?');
  console.log('  WebGL 2:      ' + gl);
  // The engine's calls (string results as JSON), from the page.
  const ccJson = (name, args) => `(() => { try { return JSON.parse(Module.ccall('${name}', 'string', ${JSON.stringify((args || []).map(() => 'number'))}, ${JSON.stringify(args || [])}) || 'null'); } catch (e) { return null; } })()`;
  const ccScreen = `(() => { try { return Module.ccall('cc_ui_screen', 'string', [], []); } catch (e) { return ''; } })()`;
  let menu = null;
  if (engine && scan && scan.ok) {
    menu = await until(wc, `(() => { const st = ${ccJson('cc_ui_state')}; const screen = ${ccScreen};
      return { done: !!(st && st.started && (CCI.loaded || []).length && screen === 'main'), screen,
        loaded: (CCI.loaded || []).map((a) => a.name), driver: st && st.driver ? st.driver.name : '' }; })()`, 180000);
    if (!menu) {
      const sc = await wc.executeJavaScript(ccScreen, true).catch(() => '');
      const n = await wc.executeJavaScript('(window.CCI && CCI.loaded || []).length', true).catch(() => 0);
      fails.push('the main menu did not come up (screen "' + sc + '", ' + n + ' archives loaded)');
    } else {
      const cities = (await wc.executeJavaScript(ccJson('cc_city_list'), true).catch(() => null)) || [];
      const cars = ((await wc.executeJavaScript(ccJson('cc_car_list'), true).catch(() => null)) || []).filter((c) => c.group === 0);
      menu.cities = cities;
      menu.cars = cars;
      console.log('  archives:     ' + menu.loaded.join(', '));
      console.log('  main menu:    up after ' + secs() + ' (' + cars.length + ' menu cars, cities ' + cities.filter((c) => !c.test).map((c) => c.name).join(', ') +
        ', driver ' + (menu.driver || 'none') + ')');
      await sleep(1500);  // the car behind the menus
      const m = await capture(wc, menuOut);
      console.log('  menu capture: ' + m.file + ' (' + m.width + 'x' + m.height + ', ' + m.colours + ' colours sampled)');
    }
  }

  // The page's desktop API (the preload's window.cciDesktop), which the page reports to the engine
  // so its Drivers screen imports "from my MM2 folder" (the engine UI has no HTML card).
  if (engine) {
    const api = await wc.executeJavaScript('!!(window.cciDesktop && cciDesktop.desktop)', true).catch(() => false);
    const players = await wc.executeJavaScript('(window.CCI && CCI.devPlayers || []).length', true).catch(() => 0);
    console.log('  desktop API:  ' + (api ? 'window.cciDesktop present' : 'missing') + '; players folder in the MM2 folder: ' +
      (players ? players + ' file' + (players === 1 ? '' : 's') + ' (the Drivers screen offers "Import from my MM2 folder")' : 'none'));
    if (!api) fails.push('window.cciDesktop is missing');
  }

  // A short Cruise drive with the accelerator held, started as the menus would (the driver's city
  // and car, else the first city and car).
  let drove = null;
  if (menu) {
    debug('starting cruise');
    const st = (await wc.executeJavaScript(ccJson('cc_ui_state'), true).catch(() => null)) || {};
    const d = st.driver || {};
    let city = menu.cities.findIndex((c) => c.raceDir === d.city && !c.test);
    if (city < 0) city = menu.cities.findIndex((c) => !c.test);
    const car = menu.cars.find((c) => c.name === d.car) || menu.cars.find((c) => c.name === 'vpbug') || menu.cars[0];
    if (city < 0 || !car) {
      fails.push('no city or car to drive');
    } else {
      await wc.executeJavaScript(`Module.ccall('cc_fe_play', null, ['number', 'number', 'number', 'number', 'number'], [${city}, 0, 0, ${car.index}, 0])`, true).catch(() => null);
      const started = await until(wc, `(() => { const g = ${ccJson('cc_drive_debug')}; return g && typeof g.speed === 'number' ? { done: true } : null; })()`, 120000);
      if (!started) {
        fails.push('Cruise did not start (screen "' + (await wc.executeJavaScript(ccScreen, true).catch(() => '')) + '")');
      } else {
        debug('in game');
        await sleep(2000);
        wc.focus();
        wc.sendInputEvent({ type: 'keyDown', keyCode: 'Up' });
        let top = 0, throttle = 0;
        for (let i = 0; i < 20; i++) {
          await sleep(250);
          wc.sendInputEvent({ type: 'keyDown', keyCode: 'Up', modifiers: ['isAutoRepeat'] });
          const g = await wc.executeJavaScript(ccJson('cc_drive_debug'), true).catch(() => null);
          const mph = g ? Math.abs(g.speed) * 2.23694 : 0;  // m/s to mph
          top = Math.max(top, mph);
          throttle = Math.max(throttle, g ? g.throttle : 0);
          debug('mph', mph.toFixed(1));
        }
        wc.sendInputEvent({ type: 'keyUp', keyCode: 'Up' });
        await sleep(500);
        drove = { city: menu.cities[city].name, car: car.description || car.name, top };
        console.log('  cruise:       ' + drove.city + ', ' + drove.car + ', ' + (top > 0.5 ? top.toFixed(1) + ' mph after 5 s on the accelerator' :
          'started (the car did not move: ' + (throttle > 0 ? 'throttle ' + throttle.toFixed(2) + ' but no speed' : 'no key input reached the window') + ')'));
      }
    }
  }

  // The capture.
  let shot = null;
  debug('capturing');
  try {
    shot = await capture(wc, out);
    console.log('  capture:      ' + shot.file + ' (' + shot.width + 'x' + shot.height + ', ' + shot.colours + ' colours sampled)');
    if (!shot.width || shot.colours < 8) fails.push('the capture looks blank');
  } catch (e) {
    fails.push('no capture (' + e.message + ')');
  }

  // The engine's warnings (CCI.onLog sends them to the console) and the console's errors.
  console.log('  engine log:   ' + consoleWarnings.length + ' warning line' + (consoleWarnings.length === 1 ? '' : 's'));
  for (const p of consoleWarnings.slice(0, 8)) console.log('    ' + p);
  console.log('  console errors: ' + consoleErrors.length);
  for (const m of consoleErrors.slice(0, 8)) console.log('    ' + m);
  if (gone) fails.push('the renderer stopped (' + gone + ')');
  if (consoleErrors.length) fails.push(consoleErrors.length + ' console error' + (consoleErrors.length === 1 ? '' : 's'));
  if (!drove && menu) fails.push('no drive');

  console.log('  result:       ' + (fails.length ? 'FAILED: ' + fails.join('; ') : 'OK') + ' (' + secs() + ')');
  return fails.length ? 1 : 0;
}

module.exports = { run };
