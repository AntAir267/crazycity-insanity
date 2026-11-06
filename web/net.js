// CrazyCity Insanity: the network layer of a network game in the browser (our own transport;
// spec/game/multiplayer.md gives only the game's records and events). A WebSocket to the session
// server (server/signal.js) for hosting, joining and the WebRTC signalling; between peers WebRTC
// data channels, one reliable and ordered for the game's events, one unordered without
// retransmits for the car states. Until a peer's data channel is open (or when WebRTC cannot
// connect at all) messages go through the server's relay instead. Guaranteed messages carry a
// per-peer sequence number, so they reach the engine in order and exactly once whichever path
// they took.
//
// The server (protocol 2) says what each machine can do: this page says "webrtc"; a native player
// ("native", "udp", engine/platform/native_transport.*) cannot open a data channel, so it is
// always reached through the relay and no WebRTC offer goes to it.
//
// The engine side is engine/platform/web_transport.*: this file pushes the transport events into
// it (cc_net_push) and it calls CCINet.send.
'use strict';

(function () {
  const REL = 1, UNREL = 0;
  const PROTO = 2;
  const CAPS = ['webrtc'];
  const ICE = [];  // host candidates only: no external services (a STUN server could go here)

  class Peer {
    constructor(net, id) {
      this.net = net;
      this.id = id;
      this.pc = null;
      this.rel = null;
      this.unrel = null;
      this.sendSeq = 0;        // guaranteed messages sent to this peer
      this.recvNext = 1;       // next guaranteed sequence number expected from it
      this.pending = new Map();
      this.caps = null;        // what the server says it can do (null: an older server, assume a browser)
      this.native = false;     // a native player: the relay only
      this.viaRelay = 0;
      this.viaChannel = 0;
      this.failed = false;
    }
    channelOpen(reliable) {
      const ch = reliable ? this.rel : this.unrel;
      return !!(ch && ch.readyState === 'open');
    }
    close() {
      try { if (this.pc) this.pc.close(); } catch (e) { /* gone */ }
      this.pc = this.rel = this.unrel = null;
    }
  }

  class Net {
    constructor() {
      this.ws = null;
      this.url = '';
      this.self = 0;
      this.host = 0;
      this.code = '';
      this.peers = new Map();
      this.listeners = [];     // (event) for the page
      this.onSessions = null;
      this.forceRelay = /[?&]netrelay\b/.test(location.search);
      this.serverProto = 0;
      this.stats = { sentRel: 0, sentUnrel: 0, relayed: 0, received: 0 };
    }

    // ---- connection

    connect(url) {
      if (this.ws && this.url === url && this.ws.readyState <= 1) return this.ready;
      this.close();
      this.url = url;
      this.ws = new WebSocket(url);
      this.ws.binaryType = 'arraybuffer';
      this.ready = new Promise((resolve, reject) => {
        this.ws.onopen = () => resolve();
        this.ws.onerror = () => reject(new Error('cannot reach the network server at ' + url));
      });
      this.ws.onmessage = (e) => this.onServer(e.data);
      this.ws.onclose = () => {
        if (this.self) this.toEngine({ kind: 'closed', reason: 'lost' });
        this.reset();
      };
      return this.ready;
    }
    close() {
      if (this.ws) {
        this.ws.onclose = null;
        try { this.ws.close(); } catch (e) { /* closed */ }
      }
      this.ws = null;
      this.reset();
    }
    reset() {
      for (const p of this.peers.values()) p.close();
      this.peers.clear();
      this.self = this.host = 0;
      this.code = '';
      this.serverProto = 0;
    }
    serverSend(o) {
      if (this.ws && this.ws.readyState === 1) this.ws.send(JSON.stringify(o));
    }
    async hostSession(url, desc) {
      await this.connect(url);
      this.serverSend({ t: 'host', desc, caps: CAPS, proto: PROTO });
    }
    async joinSession(url, code, password, version) {
      await this.connect(url);
      this.serverSend({ t: 'join', code, password, version, caps: CAPS, proto: PROTO });
    }
    async listSessions(url) {
      await this.connect(url);
      this.serverSend({ t: 'list' });
    }
    describe(desc) { this.serverSend({ t: 'describe', desc }); }
    kick(id) { this.serverSend({ t: 'kick', id }); }
    leave() {
      this.serverSend({ t: 'leave' });
      for (const p of this.peers.values()) p.close();
      this.peers.clear();
      this.self = this.host = 0;
    }

    // ---- server messages

    onServer(data) {
      if (data instanceof ArrayBuffer) {
        // Relayed: [u32 from][u8 flags][payload].
        const v = new DataView(data);
        if (data.byteLength < 5) return;
        const from = v.getUint32(0, true);
        this.onData(from, new Uint8Array(data, 5));
        return;
      }
      let m;
      try { m = JSON.parse(data); } catch (e) { return; }
      switch (m.t) {
        case 'sessions':
          if (this.onSessions) this.onSessions(m.list);
          break;
        case 'welcome':
          this.serverProto = m.proto || 0;
          break;
        case 'opened':
          this.self = m.self;
          this.host = m.host;
          this.code = m.code;
          for (const id of m.roster) if (id !== this.self) this.addPeer(id, true, m.caps ? m.caps[id] : null);
          this.toEngine({ kind: 'opened', self: m.self, host: m.host, roster: m.roster });
          this.emit({ t: 'opened', code: m.code });
          break;
        case 'joined':
          this.addPeer(m.peer, false, m.caps);
          this.toEngine({ kind: 'joined', peer: m.peer });
          break;
        case 'left': {
          const p = this.peers.get(m.peer);
          if (p) p.close();
          this.peers.delete(m.peer);
          this.toEngine({ kind: 'left', peer: m.peer });
          break;
        }
        case 'host':
          this.host = m.host;
          this.toEngine({ kind: 'host', host: m.host });
          break;
        case 'closed':
          this.toEngine({ kind: 'closed', reason: m.reason });
          this.emit({ t: 'closed', reason: m.reason });
          for (const p of this.peers.values()) p.close();
          this.peers.clear();
          this.self = this.host = 0;
          break;
        case 'signal':
          this.onSignal(m.from, m.data);
          break;
        default:
          break;
      }
    }

    emit(e) { for (const f of this.listeners) f(e); }

    // ---- WebRTC

    addPeer(id, offerer, caps) {
      if (this.peers.has(id)) return this.peers.get(id);
      const p = new Peer(this, id);
      this.peers.set(id, p);
      if (Array.isArray(caps)) {
        p.caps = caps;
        p.native = !caps.includes('webrtc');  // a native player (or anything without WebRTC): the relay
      }
      if (p.native || this.forceRelay || typeof RTCPeerConnection === 'undefined') return p;
      // The newcomer offers to everyone already there.
      if (offerer) this.startPc(p, true);
      return p;
    }

    startPc(p, offerer) {
      let pc;
      try {
        pc = new RTCPeerConnection({ iceServers: ICE });
      } catch (e) {
        p.failed = true;
        return;
      }
      p.pc = pc;
      pc.onicecandidate = (e) => {
        if (e.candidate) this.serverSend({ t: 'signal', to: p.id, data: { candidate: e.candidate.toJSON() } });
      };
      pc.onconnectionstatechange = () => {
        if (pc.connectionState === 'failed') p.failed = true;  // the relay carries on
      };
      const setup = (ch) => {
        ch.binaryType = 'arraybuffer';
        ch.onmessage = (e) => this.onData(p.id, new Uint8Array(e.data));
        if (ch.label === 'rel') p.rel = ch;
        else p.unrel = ch;
      };
      if (offerer) {
        setup(pc.createDataChannel('rel', { ordered: true }));
        setup(pc.createDataChannel('unrel', { ordered: false, maxRetransmits: 0 }));
        pc.createOffer().then((o) => pc.setLocalDescription(o)).then(() => {
          this.serverSend({ t: 'signal', to: p.id, data: { sdp: pc.localDescription.toJSON() } });
        }).catch(() => { p.failed = true; });
      } else {
        pc.ondatachannel = (e) => setup(e.channel);
      }
    }

    onSignal(from, data) {
      if (!data || !(data.sdp || data.candidate)) return;  // not WebRTC (a native player's UDP details)
      let p = this.peers.get(from);
      if (!p) p = this.addPeer(from, false);
      if (this.forceRelay || p.native) return;
      if (!p.pc) this.startPc(p, false);
      const pc = p.pc;
      if (!pc) return;
      if (data.sdp) {
        pc.setRemoteDescription(data.sdp).then(() => {
          if (data.sdp.type === 'offer') {
            return pc.createAnswer().then((a) => pc.setLocalDescription(a)).then(() => {
              this.serverSend({ t: 'signal', to: from, data: { sdp: pc.localDescription.toJSON() } });
            });
          }
          return null;
        }).catch(() => { p.failed = true; });
      } else if (data.candidate) {
        pc.addIceCandidate(data.candidate).catch(() => { /* a late candidate */ });
      }
    }

    // ---- sending and receiving (the engine's messages)

    // to = 0: every other peer. Returns false when a peer cannot be reached.
    send(to, reliable, bytes) {
      if (!this.self) return false;
      if (to === 0) {
        let ok = true;
        for (const id of this.peers.keys()) ok = this.sendTo(id, reliable, bytes) && ok;
        return ok;
      }
      return this.sendTo(to, reliable, bytes);
    }
    sendTo(id, reliable, bytes) {
      const p = this.peers.get(id);
      if (!p) return false;
      let frame;
      if (reliable) {
        frame = new Uint8Array(5 + bytes.length);
        frame[0] = REL;
        new DataView(frame.buffer).setUint32(1, ++p.sendSeq, true);
        frame.set(bytes, 5);
        this.stats.sentRel++;
      } else {
        frame = new Uint8Array(1 + bytes.length);
        frame[0] = UNREL;
        frame.set(bytes, 1);
        this.stats.sentUnrel++;
      }
      if (p.channelOpen(reliable)) {
        try {
          (reliable ? p.rel : p.unrel).send(frame);
          p.viaChannel++;
          return true;
        } catch (e) { /* fall back to the relay */ }
      }
      if (!this.ws || this.ws.readyState !== 1) return false;
      const out = new Uint8Array(5 + frame.length);
      const v = new DataView(out.buffer);
      v.setUint32(0, id, true);
      out[4] = reliable ? 1 : 0;
      out.set(frame, 5);
      this.ws.send(out);
      p.viaRelay++;
      this.stats.relayed++;
      return true;
    }
    onData(from, data) {
      const p = this.peers.get(from);
      if (!p || data.length < 1) return;
      this.stats.received++;
      if (data[0] === REL) {
        if (data.length < 5) return;
        const seq = new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(1, true);
        if (seq < p.recvNext || p.pending.has(seq)) return;  // a duplicate
        p.pending.set(seq, data.slice(5));
        while (p.pending.has(p.recvNext)) {
          const m = p.pending.get(p.recvNext);
          p.pending.delete(p.recvNext);
          p.recvNext++;
          this.toEngine({ kind: 'message', peer: from, reliable: true, data: m });
        }
      } else {
        this.toEngine({ kind: 'message', peer: from, reliable: false, data: data.slice(1) });
      }
    }

    // ---- into the engine (engine/platform/web_transport.cpp)

    toEngine(e) {
      if (!window.CCI || !CCI.engineReady || typeof Module === 'undefined' || !Module._cc_net_push) return;
      const kinds = { opened: 0, joined: 1, left: 2, host: 3, message: 4, closed: 5 };
      const json = JSON.stringify({ self: e.self || 0, host: e.host || 0, roster: e.roster || [], reason: e.reason || '' });
      const data = e.data || new Uint8Array(0);
      const ptr = data.length ? Module._malloc(data.length) : 0;
      if (ptr) Module.HEAPU8.set(data, ptr);
      Module.ccall('cc_net_push', null, ['number', 'number', 'number', 'number', 'number', 'string'],
        [kinds[e.kind], e.peer || 0, e.reliable ? 1 : 0, ptr, data.length, json]);
      if (ptr) Module._free(ptr);
    }

    // State for the panel.
    linkInfo() {
      const out = [];
      for (const p of this.peers.values()) {
        out.push({ id: p.id, rtc: p.channelOpen(true) && p.channelOpen(false), failed: p.failed, native: p.native, relayed: p.viaRelay, direct: p.viaChannel });
      }
      return out;
    }
  }

  window.CCINet = new Net();
})();
