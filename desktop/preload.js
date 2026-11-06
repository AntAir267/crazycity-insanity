// CrazyCity Insanity desktop app: the page's preload (isolated from the page, sandboxed).
//
// The page itself gets only window.cciDesktop = {desktop: true, chooseMm2Folder()}. This script
// adds the "Desktop app" card under the viewer's Game data card (the MM2 folder, changing it, the
// network setting) and notes the folder the page's own "Open my MM2 folder" picks, so the app
// remembers it.
'use strict';

const { contextBridge, ipcRenderer, webUtils } = require('electron');

contextBridge.exposeInMainWorld('cciDesktop', Object.freeze({
  desktop: true,
  chooseMm2Folder: () => ipcRenderer.invoke('cci:choose-folder').then(() => undefined),
}));

// The page's folder picker: its own handler reads the files (and empties the input), so this one
// runs first, in the capture phase on the window.
window.addEventListener('change', (ev) => {
  const input = ev.target;
  if (!input || input.id !== 'pick-folder' || !input.files) return;
  const ars = Array.from(input.files).filter((f) => /\.ar$/i.test(f.name));
  if (!ars.length) return;
  const depth = (f) => (f.webkitRelativePath || f.name).split('/').length;
  const min = Math.min(...ars.map(depth));
  const top = ars.find((f) => depth(f) === min);
  let file = '';
  try { file = webUtils.getPathForFile(top); } catch (e) { file = ''; }
  const cut = file.lastIndexOf('/');
  if (cut > 0) ipcRenderer.send('cci:folder-picked', file.slice(0, cut));
}, true);

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

async function buildCard() {
  const data = document.getElementById('data');
  if (!data || document.getElementById('desktop-card')) return;
  const card = el('section', 'card');
  card.id = 'desktop-card';
  const head = el('div', 'card-head');
  head.append(el('h2', '', 'Desktop app'));
  const badge = el('span', 'badge');
  head.append(badge);
  card.append(head);

  const where = el('div', 'field');
  where.append(el('div', 'label', 'Midtown Madness 2 folder'));
  const path = el('div', 'status');
  path.style.wordBreak = 'break-all';
  where.append(path);
  card.append(where);

  const row = el('div', 'row');
  const change = el('button', 'btn secondary', 'Change MM2 folder…');
  change.title = 'Choose the folder the app reads the game data from (Ctrl+Shift+O)';
  row.append(change);
  card.append(row);

  const lanField = el('div', 'field checks');
  const lanLabel = el('label');
  const lan = document.createElement('input');
  lan.type = 'checkbox';
  lanLabel.append(lan, document.createTextNode(' Let friends on my network join'));
  lanField.append(lanLabel);
  card.append(lanField);
  const lanNote = el('p', 'hint');
  card.append(lanNote);
  const portNote = el('p', 'hint');
  card.append(portNote);
  card.append(el('p', 'hint', 'F11 full screen · Ctrl+Shift+O change the MM2 folder · Ctrl+Shift+R reload'));
  data.after(card);

  const show = (i) => {
    if (!i) return;
    badge.textContent = 'v' + i.version;
    path.textContent = i.folder || 'none chosen';
    path.title = i.folderLine;
    lan.checked = !!i.lan;
    lanNote.textContent = i.lan
      ? (i.lanServers.length
        ? 'Friends join with the server ' + i.lanServers.join(' or ') + ' in their own copy, or open ' + i.lanPages.join(' or ') + ' in a browser with their own MM2 folder. Your MM2 files stay here.'
        : 'No network address found.')
      : 'Off: network games are for this computer only.';
    portNote.textContent = i.portNote || '';
    portNote.hidden = !i.portNote;
  };
  change.addEventListener('click', async () => show(await ipcRenderer.invoke('cci:choose-folder')));
  lan.addEventListener('change', async () => {
    lan.disabled = true;
    try { show(await ipcRenderer.invoke('cci:set-lan', lan.checked)); } finally { lan.disabled = false; }
  });
  show(await ipcRenderer.invoke('cci:info'));
}

window.addEventListener('DOMContentLoaded', () => { buildCard(); });
