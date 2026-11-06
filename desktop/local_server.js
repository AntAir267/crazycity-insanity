// CrazyCity Insanity desktop app: the local server (no Electron here, so it runs under plain Node
// too).
//
// The dev signalling and relay server (server/signal.js) serves the bundled web build with byte
// ranges and the WebSocket /net; this adds game/ from the player's MM2 folder (game_folder.js),
// for this computer only: with "Let friends on my network join" the page and /net are open to the
// network, the game files never are.
'use strict';

const path = require('path');
const { createServer, sendFile } = require(path.join(__dirname, '..', 'server', 'signal.js'));
const folder = require('./game_folder.js');

function isLoopback(addr) {
  addr = String(addr || '');
  return addr === '::1' || addr.startsWith('127.') || addr.startsWith('::ffff:127.');
}

// {webRoot, log} → {start(port, host), stop(), setFolder(scan), scan(), dir(), port(), host()}.
function createLocalServer(opts) {
  const st = { server: null, port: 0, host: '', dir: '', scan: null };

  function setFolder(scan) {
    st.scan = scan;
    st.dir = scan ? scan.dir : '';
  }

  function handle(req, res, url) {
    if (!url.pathname.startsWith('/game/')) return false;
    if (!isLoopback(req.socket.remoteAddress)) {
      res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('The game data stays on the computer it is on: open your own MM2 folder in the page.');
      return true;
    }
    let rel;
    try { rel = decodeURIComponent(url.pathname.slice('/game/'.length)); } catch (e) { res.writeHead(400); res.end(); return true; }
    if (rel === 'index.json') {
      setFolder(folder.scanFolder(st.dir));  // read again: the player may have added a mod
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
      res.end(folder.indexJson(st.scan));
      return true;
    }
    const file = folder.resolveGameFile(st.scan, rel);
    if (!file) { res.writeHead(404); res.end('not found'); return true; }
    sendFile(req, res, file);
    return true;
  }

  function start(port, host) {
    return new Promise((resolve, reject) => {
      const srv = createServer({ static: opts.webRoot, handle, log: opts.log });
      srv.once('error', reject);
      srv.listen(port, host, () => {
        srv.removeListener('error', reject);
        srv.on('error', (e) => console.error('server: ' + e.message));
        st.server = srv;
        st.port = srv.address().port;
        st.host = host;
        resolve(st.port);
      });
    });
  }

  // Stops listening and drops every connection (a network game on it ends).
  function stop() {
    return new Promise((resolve) => {
      if (!st.server) { resolve(); return; }
      const srv = st.server;
      st.server = null;
      srv.closeAll(resolve);
    });
  }

  return {
    start, stop, setFolder,
    scan: () => st.scan, dir: () => st.dir, port: () => st.port, host: () => st.host,
  };
}

module.exports = { createLocalServer, isLoopback };
