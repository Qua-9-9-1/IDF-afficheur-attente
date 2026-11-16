// Runs the simulator (WebAssembly, WASI command) in a Web Worker so the app keeps its blocking
// loops: sleeps use Atomics.wait, files and HTTP use synchronous XMLHttpRequest.
'use strict';

const enc = new TextEncoder();
const dec = new TextDecoder();
let memory = null;
let keys = null;   // Int32Array on a SharedArrayBuffer: pending key bitmask, written by the page
let sleeper = null; // Int32Array used only for Atomics.wait
const results = [new Uint8Array(0), new Uint8Array(0)];
let stdoutLine = '';

class Exit extends Error {
  constructor(code) { super('exit ' + code); this.code = code; }
}

const u8 = () => new Uint8Array(memory.buffer);
const dv = () => new DataView(memory.buffer);
const str = (ptr, len) => dec.decode(u8().slice(ptr, ptr + len));

function log(text) { postMessage({ type: 'log', text }); }

function syncGet(url, headers, binary) {
  const x = new XMLHttpRequest();
  x.open('GET', url, false);
  if (binary) x.responseType = 'arraybuffer';
  for (const [k, v] of headers) x.setRequestHeader(k, v);
  x.send();
  return x;
}

const sim = {
  now_ms: () => performance.now(),
  sleep_ms: (ms) => { Atomics.wait(sleeper, 0, 0, Math.max(0, ms)); },
  poll_keys: () => Atomics.exchange(keys, 0, 0),
  present: (ptr, w, h) => {
    const px = new Uint16Array(memory.buffer, ptr, w * h).slice();
    postMessage({ type: 'frame', w, h, px }, [px.buffer]);
  },
  read_file: (p, l) => {
    const path = str(p, l);
    try {
      const x = syncGet('/sd' + path, [], true);
      if (x.status !== 200) return -1;
      results[0] = new Uint8Array(x.response);
      return results[0].length;
    } catch (e) {
      return -1;
    }
  },
  http_get: (u, ul, h, hl) => {
    const url = str(u, ul);
    const headers = str(h, hl).split('\n').filter(Boolean).map((line) => {
      const i = line.indexOf(':');
      return [line.slice(0, i).trim(), line.slice(i + 1).trim()];
    });
    results[0] = new Uint8Array(0);
    results[1] = new Uint8Array(0);
    try {
      const x = syncGet('/proxy?u=' + encodeURIComponent(url), headers, true);
      if (x.getResponseHeader('X-Sim-Error')) return -1;
      results[0] = new Uint8Array(x.response);
      results[1] = enc.encode(x.getAllResponseHeaders());
      return x.status;
    } catch (e) {
      log('[worker] HTTP error: ' + e.message);
      return -1;
    }
  },
  result_len: (slot) => results[slot].length,
  result_copy: (slot, dst) => { u8().set(results[slot], dst); },
  quit: () => { postMessage({ type: 'quit' }); },
};

function makeWasi(args) {
  const argBufs = args.map((a) => enc.encode(a + '\0'));
  const EBADF = 8, ENOSYS = 52;
  return {
    args_sizes_get: (argcPtr, sizePtr) => {
      dv().setUint32(argcPtr, argBufs.length, true);
      dv().setUint32(sizePtr, argBufs.reduce((n, b) => n + b.length, 0), true);
      return 0;
    },
    args_get: (argvPtr, bufPtr) => {
      let p = bufPtr;
      argBufs.forEach((b, i) => {
        dv().setUint32(argvPtr + 4 * i, p, true);
        u8().set(b, p);
        p += b.length;
      });
      return 0;
    },
    environ_sizes_get: (countPtr, sizePtr) => {
      dv().setUint32(countPtr, 0, true);
      dv().setUint32(sizePtr, 0, true);
      return 0;
    },
    environ_get: () => 0,
    fd_write: (fd, iovs, iovsLen, nwrittenPtr) => {
      let total = 0;
      let text = '';
      for (let i = 0; i < iovsLen; i++) {
        const ptr = dv().getUint32(iovs + 8 * i, true);
        const len = dv().getUint32(iovs + 8 * i + 4, true);
        text += str(ptr, len);
        total += len;
      }
      if (fd === 1 || fd === 2) {
        stdoutLine += text;
        let nl;
        while ((nl = stdoutLine.indexOf('\n')) >= 0) {
          log(stdoutLine.slice(0, nl));
          stdoutLine = stdoutLine.slice(nl + 1);
        }
      }
      dv().setUint32(nwrittenPtr, total, true);
      return 0;
    },
    fd_read: () => EBADF,
    fd_close: () => 0,
    fd_seek: () => EBADF,
    fd_fdstat_get: (fd, statPtr) => {
      if (fd > 2) return EBADF;
      for (let i = 0; i < 24; i++) dv().setUint8(statPtr + i, 0);
      dv().setUint8(statPtr, 2); // character device
      return 0;
    },
    fd_prestat_get: () => EBADF, // no preopened directory
    fd_prestat_dir_name: () => EBADF,
    clock_time_get: (id, _precision, timePtr) => {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      dv().setBigUint64(timePtr, ns, true);
      return 0;
    },
    clock_res_get: (_id, resPtr) => { dv().setBigUint64(resPtr, 1000n, true); return 0; },
    random_get: (ptr, len) => { crypto.getRandomValues(u8().subarray(ptr, ptr + len)); return 0; },
    sched_yield: () => 0,
    proc_exit: (code) => { throw new Exit(code); },
    poll_oneoff: () => ENOSYS,
  };
}

onmessage = async (e) => {
  const { wasmUrl, args, keyBuffer } = e.data;
  keys = new Int32Array(keyBuffer);
  sleeper = new Int32Array(new SharedArrayBuffer(4));
  try {
    const module = await WebAssembly.compileStreaming(fetch(wasmUrl));
    const provided = { sim, wasi_snapshot_preview1: makeWasi(args) };
    const imports = {};
    for (const imp of WebAssembly.Module.imports(module)) {
      imports[imp.module] = imports[imp.module] || {};
      const fn = provided[imp.module] && provided[imp.module][imp.name];
      imports[imp.module][imp.name] = fn || ((...a) => { log(`[worker] unimplemented import ${imp.module}.${imp.name}`); return 52; });
    }
    const instance = await WebAssembly.instantiate(module, imports);
    memory = instance.exports.memory;
    instance.exports._start();
    postMessage({ type: 'quit' });
  } catch (err) {
    if (err instanceof Exit) {
      postMessage({ type: 'quit', code: err.code });
    } else {
      postMessage({ type: 'error', text: String(err && err.stack || err) });
    }
  }
};
