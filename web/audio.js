// CrazyCity Insanity audio output: one Web Audio script node pulls interleaved stereo frames from
// the engine (cc_audio_render: the music with the sound effects mixed in) at the engine's rate
// (22,050 Hz, MM2's mixing rate); the browser runs the context at that rate, or we resample when
// it cannot. Browsers start audio only after a user gesture, so the context is made on the first
// click or key; a hidden page suspends it (its frames stop too).
'use strict';

(function () {
  const out = {
    ctx: null, node: null, rate: 22050, step: 1,
    srcL: new Float32Array(0), srcR: new Float32Array(0), pos: 0,
    pulls: 0, underruns: 0,
  };

  function engineReady() {
    return !!(window.Module && Module._cc_audio_render && Module.HEAPF32 && window.CCI && CCI.engineReady);
  }
  function pull(n) {
    const ptr = Module._cc_audio_render(n);
    out.pulls++;
    return Module.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + n * 2);
  }

  function process(ev) {
    const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
    const n = L.length;
    if (!engineReady()) {
      L.fill(0);
      R.fill(0);
      out.underruns++;
      return;
    }
    try {
      if (out.step === 1) {
        const f = pull(n);
        for (let i = 0; i < n; i++) { L[i] = f[2 * i]; R[i] = f[2 * i + 1]; }
        return;
      }
      // Linear resampling from the engine's rate to the context's.
      const need = Math.ceil(out.pos + n * out.step) + 2;
      if (out.srcL.length < need) {
        const add = need - out.srcL.length;
        const f = pull(add);
        const nl = new Float32Array(out.srcL.length + add), nr = new Float32Array(out.srcR.length + add);
        nl.set(out.srcL);
        nr.set(out.srcR);
        for (let i = 0; i < add; i++) { nl[out.srcL.length + i] = f[2 * i]; nr[out.srcR.length + i] = f[2 * i + 1]; }
        out.srcL = nl;
        out.srcR = nr;
      }
      let p = out.pos;
      for (let i = 0; i < n; i++, p += out.step) {
        const k = Math.floor(p), t = p - k;
        L[i] = out.srcL[k] + (out.srcL[k + 1] - out.srcL[k]) * t;
        R[i] = out.srcR[k] + (out.srcR[k + 1] - out.srcR[k]) * t;
      }
      const used = Math.floor(p);
      out.srcL = out.srcL.slice(used);
      out.srcR = out.srcR.slice(used);
      out.pos = p - used;
    } catch (e) {
      L.fill(0);
      R.fill(0);
    }
  }

  // The first gesture makes (or resumes) the context.
  function unlock() {
    try {
      if (!out.ctx) {
        if (!engineReady()) return;
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return;
        try { out.rate = Module.ccall('cc_audio_rate', 'number', [], []) || 22050; } catch (e) { out.rate = 22050; }
        try { out.ctx = new AC({ sampleRate: out.rate, latencyHint: 'interactive' }); } catch (e) { out.ctx = new AC(); }
        out.step = out.rate / out.ctx.sampleRate;
        // 1024 frames: about 46 ms at 22,050 Hz between the game and what is heard.
        out.node = out.ctx.createScriptProcessor(1024, 0, 2);
        out.node.onaudioprocess = process;
        out.node.connect(out.ctx.destination);
      }
      if (out.ctx.state !== 'running' && !document.hidden) out.ctx.resume().catch(() => {});
    } catch (e) { /* no audio */ }
  }
  for (const ev of ['pointerdown', 'keydown', 'touchstart']) window.addEventListener(ev, unlock, { capture: true, passive: true });

  document.addEventListener('visibilitychange', () => {
    if (!out.ctx) return;
    try {
      if (document.hidden) out.ctx.suspend();
      else out.ctx.resume();
    } catch (e) { /* ignore */ }
  });

  window.CCIAudio = {
    unlock,
    running() { return !!(out.ctx && out.ctx.state === 'running'); },
    // For the page and tests.
    state() {
      return { context: out.ctx ? out.ctx.state : 'none', rate: out.ctx ? out.ctx.sampleRate : 0, pulls: out.pulls, underruns: out.underruns };
    },
  };
})();
