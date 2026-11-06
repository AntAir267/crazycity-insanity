#!/usr/bin/env node
// CrazyCity Insanity: the dev signalling and relay server for network games (our own transport;
// spec/game/multiplayer.md specifies only the game's records and events).
//
//   node server/signal.js [--port 8821] [--host 127.0.0.1] [--static dist] [--quiet]
//                         [--tls-cert cert.pem --tls-key key.pem] [--no-udp] [--test-ids]
//
// The desktop app (desktop/main.js) runs the same server in its main process: require() this file
// and call createServer().
//
// One WebSocket endpoint, /net. A machine hosts a session (it gets a four-letter code) or joins
// one by its code; the server keeps each session's players in join order (the session order), gives
// every player a unique id above 999, makes the next player host when the host goes, refuses joins
// to a sealed or full session or with the wrong password or game version, forwards the WebRTC
// signalling between peers, and relays game messages between peers whose data channels are not
// open. With --static it also serves that folder (the page and the game data, with byte ranges),
// so one port does everything. No accounts, nothing stored, no external services.
//
// Control frames are JSON text; relayed game messages are binary: [u32 peer][u8 flags][payload],
// the peer being the recipient on the way in and the sender on the way out. Flags bit 0 marks a
// guaranteed message; any other (a car state) may be dropped: while the receiver's socket is
// backed up only the newest one from each sender waits (latest wins).
//
// Protocol 2 (the engine's own server, engine/platform/host_server.cpp, speaks the same): a
// "welcome" {proto, udp} on connecting; "host" and "join" carry the machine's capabilities (caps:
// "webrtc" for a browser, "native" and "udp" for the native game; none given = a browser), which
// "opened" (caps by player id) and "joined" (caps) pass on, so a browser uses the relay for a
// native player and native players can look for a direct UDP path; "ping" {n} is answered with
// "pong" {n}. On the same port number over UDP the server answers a native client's question
// where its datagrams come from ("CCIW" 1 + padding → "CCIW" 2, family, port, address), the only
// help the direct path needs (no STUN service). --test-ids gives ids 1001, 1002, … and codes AAAA,
// AAAB, … (repeatable test logs); --port 0 takes any free port (the first line says which).
'use strict';

const http = require('http');
const https = require('https');
const dgram = require('dgram');
const net = require('net');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

// The log; the desktop app (desktop/main.js) sets its own through createServer's options.
let log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);
const PROTO = 2;
const DROP_ABOVE = 64 * 1024;  // bytes waiting on a socket above which car states wait (latest wins)
let testIds = false;           // createServer({testIds}): ids 1001, 1002, … and codes AAAA, AAAB, …
let udpPort = 0;               // the UDP address answer's port (0: none)

// ------------------------------------------------------------------ minimal WebSocket (RFC 6455)

class Socket {
  constructor(sock) {
    this.sock = sock;
    this.buf = Buffer.alloc(0);
    this.parts = [];
    this.open = true;
    this.onmessage = null;
    this.onclose = null;
    this.waiting = new Map();  // sender → its newest droppable frame while the socket is backed up
    sock.setNoDelay(true);
    sock.on('drain', () => this.drain());
    sock.on('data', (d) => this.data(d));
    // The HTTP server's sockets stay half-open after the peer's FIN: a machine that went away
    // without a close frame (a crash, a lost host) is gone now, not at the next write to it.
    sock.on('end', () => this.close());
    sock.on('close', () => this.closed());
    sock.on('error', () => this.closed());
  }
  data(d) {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 2) return;
      const b0 = this.buf[0], b1 = this.buf[1];
      const fin = (b0 & 0x80) !== 0, op = b0 & 15, masked = (b1 & 0x80) !== 0;
      let len = b1 & 127, at = 2;
      if (len === 126) {
        if (this.buf.length < 4) return;
        len = this.buf.readUInt16BE(2);
        at = 4;
      } else if (len === 127) {
        if (this.buf.length < 10) return;
        len = Number(this.buf.readBigUInt64BE(2));
        at = 10;
      }
      if (len > 16 * 1024 * 1024) return this.close();
      const need = at + (masked ? 4 : 0) + len;
      if (this.buf.length < need) return;
      let payload = this.buf.subarray(at + (masked ? 4 : 0), need);
      if (masked) {
        const mask = this.buf.subarray(at, at + 4);
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      }
      this.buf = this.buf.subarray(need);
      if (op === 8) return this.close();
      if (op === 9) { this.frame(10, payload); continue; }  // ping → pong
      if (op === 10) continue;
      if (op === 1 || op === 2) this.partOp = op;
      this.parts.push(payload);
      if (fin) {
        const all = Buffer.concat(this.parts);
        this.parts = [];
        if (this.onmessage) this.onmessage(all, this.partOp === 2);
      }
    }
  }
  frame(op, payload) {
    if (!this.open) return;
    const n = payload.length;
    let head;
    if (n < 126) head = Buffer.from([0x80 | op, n]);
    else if (n < 65536) { head = Buffer.alloc(4); head[0] = 0x80 | op; head[1] = 126; head.writeUInt16BE(n, 2); }
    else { head = Buffer.alloc(10); head[0] = 0x80 | op; head[1] = 127; head.writeBigUInt64BE(BigInt(n), 2); }
    this.sock.write(Buffer.concat([head, payload]));
  }
  sendJson(o) { this.frame(1, Buffer.from(JSON.stringify(o))); }
  sendBinary(b) { this.frame(2, b); }
  // A droppable frame: sent at once unless the socket is backed up; then it waits, replacing the
  // one from the same sender that waits already.
  sendDroppable(b, from) {
    if (this.sock.writableLength <= DROP_ABOVE && !this.waiting.size) { this.frame(2, b); return; }
    this.waiting.set(from, b);
  }
  drain() {
    if (!this.waiting.size || !this.open) return;
    const w = [...this.waiting.values()];
    this.waiting.clear();
    for (const b of w) this.frame(2, b);
  }
  close() {
    if (!this.open) return;
    try { this.frame(8, Buffer.alloc(0)); } catch (e) { /* closed */ }
    this.sock.end();
    this.closed();
  }
  closed() {
    if (!this.open) return;
    this.open = false;
    if (this.onclose) this.onclose();
  }
}

// ------------------------------------------------------------------ sessions

const sessions = new Map();  // code → session
const usedIds = new Set();
let testId = 1001, testCode = 0;

function newId() {
  for (;;) {
    const id = testIds ? testId++ : 1000 + crypto.randomInt(0, 0x7fffffff - 1000);
    if (!usedIds.has(id)) { usedIds.add(id); return id; }
  }
}
function newCode() {
  const A = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
  for (;;) {
    let c = '';
    if (testIds) {
      let n = testCode++;
      for (let i = 0; i < 4; i++) { c = A[n % A.length] + c; n = Math.floor(n / A.length); }
    } else {
      for (let i = 0; i < 4; i++) c += A[crypto.randomInt(0, A.length)];
    }
    if (!sessions.has(c)) return c;
  }
}

// What a machine can do ("webrtc", "native", "udp"); a page that does not say is a browser.
function cleanCaps(c) {
  if (!Array.isArray(c)) return ['webrtc'];
  return c.filter((x) => typeof x === 'string' && x).slice(0, 8).map((x) => x.slice(0, 16));
}
function rosterCaps(s) {
  const o = {};
  for (const id of s.order) o[id] = s.players.get(id).caps;
  return o;
}

function publicDesc(s) {
  return {
    code: s.code, name: s.desc.name || 'Game', players: s.order.length, max: s.desc.maxPlayers,
    sealed: !!s.desc.sealed, city: s.desc.city || '', mode: s.desc.mode || 0, password: !!s.desc.password,
    version: s.desc.version || '',  // a list can show games of another version as not joinable
  };
}

function cleanDesc(d, old) {
  d = d || {};
  return {
    name: String(d.name || (old && old.name) || 'Game').slice(0, 40),
    password: String(d.password !== undefined ? d.password : (old ? old.password : '')).slice(0, 40),
    maxPlayers: Math.max(1, Math.min(8, Number(d.maxPlayers || (old && old.maxPlayers) || 8))),
    sealed: !!d.sealed,
    city: String(d.city || '').slice(0, 40),
    mode: Number(d.mode || 0),
    version: String(d.version !== undefined ? d.version : (old ? old.version : '')).slice(0, 40),
  };
}

function removePlayer(s, id, reason, notifySelf) {
  const p = s.players.get(id);
  if (!p) return;
  s.players.delete(id);
  s.order = s.order.filter((x) => x !== id);
  usedIds.delete(id);
  p.session = null;
  if (notifySelf && p.ws.open) p.ws.sendJson({ t: 'closed', reason });
  for (const q of s.players.values()) q.ws.sendJson({ t: 'left', peer: id });
  if (s.host === id) {
    s.host = s.order.length ? s.order[0] : 0;
    if (s.host) for (const q of s.players.values()) q.ws.sendJson({ t: 'host', host: s.host });
  }
  log(`session ${s.code}: player ${id} out (${reason}); ${s.order.length} left`);
  if (!s.order.length) {
    sessions.delete(s.code);
    log(`session ${s.code} closed`);
  }
}

function onConnection(ws, addr) {
  const me = { ws, id: 0, session: null, addr, caps: ['webrtc'] };
  ws.sendJson({ t: 'welcome', proto: PROTO, udp: udpPort });
  ws.onmessage = (data, binary) => {
    if (binary) {
      // Relay: [u32 to][u8 flags][payload] → [u32 from][u8 flags][payload].
      const s = me.session;
      if (!s || data.length < 5) return;
      const to = data.readUInt32LE(0);
      const dst = s.players.get(to);
      if (!dst) return;
      const out = Buffer.from(data);
      out.writeUInt32LE(me.id, 0);
      if (data[4] & 1) dst.ws.sendBinary(out);
      else dst.ws.sendDroppable(out, me.id);
      return;
    }
    let m;
    try { m = JSON.parse(data.toString('utf8')); } catch (e) { return; }
    switch (m.t) {
      case 'list':
        ws.sendJson({ t: 'sessions', list: [...sessions.values()].map(publicDesc) });
        return;
      case 'ping':
        ws.sendJson({ t: 'pong', n: m.n });
        return;
      case 'host': {
        if (me.session) return;
        const s = { code: newCode(), desc: cleanDesc(m.desc), players: new Map(), order: [], host: 0 };
        me.id = newId();
        me.caps = cleanCaps(m.caps);
        s.players.set(me.id, me);
        s.order.push(me.id);
        s.host = me.id;
        me.session = s;
        sessions.set(s.code, s);
        ws.sendJson({ t: 'opened', self: me.id, host: me.id, roster: s.order, code: s.code, caps: rosterCaps(s) });
        log(`session ${s.code} hosted by ${me.id} (${addr})`);
        return;
      }
      case 'join': {
        if (me.session) return;
        const s = sessions.get(String(m.code || '').toUpperCase());
        let why = '';
        if (!s) why = 'no such game';
        else if (s.desc.sealed) why = 'sealed';
        else if (s.order.length >= s.desc.maxPlayers) why = 'full';
        else if (s.desc.password && String(m.password || '') !== s.desc.password) why = 'password';
        else if (s.desc.version && m.version && String(m.version) !== s.desc.version) why = 'version';
        if (why) {
          ws.sendJson({ t: 'closed', reason: 'refused: ' + why });
          return;
        }
        me.id = newId();
        me.caps = cleanCaps(m.caps);
        s.players.set(me.id, me);
        s.order.push(me.id);
        me.session = s;
        ws.sendJson({ t: 'opened', self: me.id, host: s.host, roster: s.order, code: s.code, caps: rosterCaps(s) });
        for (const q of s.players.values()) if (q !== me) q.ws.sendJson({ t: 'joined', peer: me.id, caps: me.caps });
        log(`session ${s.code}: ${me.id} joined (${addr}); ${s.order.length} players`);
        return;
      }
      case 'describe': {
        const s = me.session;
        if (!s || s.host !== me.id) return;
        s.desc = cleanDesc(m.desc, s.desc);
        return;
      }
      case 'kick': {
        const s = me.session;
        if (!s || s.host !== me.id || m.id === me.id) return;
        const p = s.players.get(Number(m.id));
        if (p) removePlayer(s, p.id, 'kicked', true);
        return;
      }
      case 'leave':
        if (me.session) removePlayer(me.session, me.id, 'left', false);
        return;
      case 'signal': {
        const s = me.session;
        if (!s) return;
        const dst = s.players.get(Number(m.to));
        if (dst) dst.ws.sendJson({ t: 'signal', from: me.id, data: m.data });
        return;
      }
      default:
        return;
    }
  };
  ws.onclose = () => {
    if (me.session) removePlayer(me.session, me.id, 'lost', false);
  };
}

// ------------------------------------------------------------------ HTTP (static files, upgrade)

const TYPES = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
};

// One file, with byte ranges (the page reads the audio archives lazily by range).
function sendFile(req, res, file) {
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) { res.writeHead(404); res.end('not found'); return; }
    const type = TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
    const head = { 'Content-Type': type, 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-cache' };
    const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
    if (range) {
      let start, end;
      if (range[1] === '') { start = Math.max(0, st.size - Number(range[2] || 0)); end = st.size - 1; }
      else { start = Number(range[1]); end = range[2] ? Number(range[2]) : st.size - 1; }
      end = Math.min(end, st.size - 1);
      if (start > end) { res.writeHead(416, { 'Content-Range': 'bytes */' + st.size }); res.end(); return; }
      res.writeHead(206, Object.assign(head, { 'Content-Range': `bytes ${start}-${end}/${st.size}`, 'Content-Length': end - start + 1 }));
      if (req.method === 'HEAD') { res.end(); return; }
      fs.createReadStream(file, { start, end }).pipe(res);
      return;
    }
    res.writeHead(200, Object.assign(head, { 'Content-Length': st.size }));
    if (req.method === 'HEAD') { res.end(); return; }
    fs.createReadStream(file).pipe(res);
  });
}

function serveStatic(req, res, staticDir) {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/net/info') {
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
    res.end(JSON.stringify({ cci: true, sessions: sessions.size, proto: PROTO, udp: udpPort }));
    return;
  }
  if (!staticDir) { res.writeHead(404); res.end('signalling server: WebSocket at /net'); return; }
  let rel;
  try { rel = decodeURIComponent(url.pathname); } catch (e) { res.writeHead(400); res.end(); return; }
  if (rel.endsWith('/')) rel += 'index.html';
  const root = path.resolve(staticDir);
  const file = path.resolve(root, '.' + rel);
  if (!file.startsWith(root)) { res.writeHead(403); res.end(); return; }
  sendFile(req, res, file);
}

// ------------------------------------------------------------------ UDP: the address answer

// On the server's own address and port once it listens. `done` when bound (or given up).
function startUdp(server, done) {
  const a = server.address();
  const host = a.address;
  const u = dgram.createSocket(net.isIPv6(host) ? 'udp6' : 'udp4');
  u.on('message', (msg, rinfo) => {
    // "CCIW" 1 + padding (28 bytes) → "CCIW" 2, family, port (big-endian), address: where the
    // datagram came from. Never more bytes back than came in.
    if (msg.length < 28 || msg.readUInt32BE(0) !== 0x43434957 || msg[4] !== 1) return;
    const v4 = net.isIPv4(rinfo.address) ? rinfo.address : (/^::ffff:(\d+\.\d+\.\d+\.\d+)$/.exec(rinfo.address) || [])[1];
    if (!v4) return;  // the direct path is IPv4 only
    const out = Buffer.alloc(12);
    out.write('CCIW', 0, 'latin1');
    out[4] = 2; out[5] = 4; out.writeUInt16BE(rinfo.port, 6);
    v4.split('.').forEach((b, i) => { out[8 + i] = Number(b); });
    u.send(out, rinfo.port, rinfo.address);
  });
  let started = false;
  u.on('error', (e) => {
    log(`no UDP on port ${a.port} (${e.message}): native players stay on the relay`);
    if (server.udp === u) { server.udp = null; udpPort = 0; }
    try { u.close(); } catch (x) { /* closed */ }
    if (!started) { started = true; done(); }
  });
  u.bind(a.port, host, () => {
    server.udp = u;
    udpPort = u.address().port;
    started = true;
    done();
  });
}

// The server: {port, host, static, handle(req, res, url) → true when it answered, log, tls:
// {cert, key} (https and wss), udp: false (no UDP address answer), testIds}. The desktop app adds
// its own routes (the player's MM2 folder) through `handle`. It emits "udp" once the UDP address
// answer is up (or was given up) after listening.
function createServer(opts) {
  opts = opts || {};
  if (opts.log) log = opts.log;
  if (opts.testIds) testIds = true;
  const sockets = new Set();
  const onRequest = (req, res) => {
    if (opts.handle && opts.handle(req, res, new URL(req.url, 'http://x'))) return;
    serveStatic(req, res, opts.static || '');
  };
  const server = opts.tls ? https.createServer({ cert: opts.tls.cert, key: opts.tls.key }, onRequest) : http.createServer(onRequest);
  server.udp = null;
  server.on('listening', () => {
    if (opts.udp === false) { server.emit('udp'); return; }
    startUdp(server, () => server.emit('udp'));
  });
  server.on('connection', (sock) => {
    sockets.add(sock);
    sock.on('close', () => sockets.delete(sock));
  });
  server.on('upgrade', (req, sock) => {
    const url = new URL(req.url, 'http://x');
    const key = req.headers['sec-websocket-key'];
    if (url.pathname !== '/net' || !key) { sock.destroy(); return; }
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
               'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n');
    onConnection(new Socket(sock), sock.remoteAddress);
  });
  // Stops listening and drops every connection (the network games on it end).
  server.closeAll = (done) => {
    if (server.udp) {
      try { server.udp.close(); } catch (e) { /* closed */ }
      server.udp = null;
      udpPort = 0;
    }
    server.close(() => { if (done) done(); });
    for (const s of sockets) s.destroy();
  };
  return server;
}

module.exports = { createServer, sendFile, TYPES };

if (require.main === module) {
  const args = process.argv.slice(2);
  const arg = (name, def) => {
    const i = args.indexOf('--' + name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
  };
  const PORT = Number(arg('port', process.env.PORT || 8821));
  const HOST = arg('host', '127.0.0.1');
  const STATIC = arg('static', '');
  const QUIET = args.includes('--quiet');
  const TLS_CERT = arg('tls-cert', ''), TLS_KEY = arg('tls-key', '');
  const tls = TLS_CERT && TLS_KEY ? { cert: fs.readFileSync(TLS_CERT), key: fs.readFileSync(TLS_KEY) } : null;
  const server = createServer({
    static: STATIC,
    log: (...a) => { if (!QUIET) console.log(new Date().toISOString().slice(11, 19), ...a); },
    tls,
    udp: !args.includes('--no-udp'),
    testIds: args.includes('--test-ids'),
  });
  // The line comes once the UDP answer is up too (the tests read the port from it).
  server.once('udp', () => {
    log(`CrazyCity Insanity network server on ${tls ? 'https' : 'http'}://${HOST}:${server.address().port}/ (WebSocket /net)` +
        (STATIC ? `, serving ${STATIC}` : ''));
  });
  server.listen(PORT, HOST);
}
