// Page side of the simulator: canvas display, keyboard/mouse -> encoder keys, worker lifecycle.
// URL parameters: demo=rer|metro|transilien|tram|bus (live demo), keys=<n/p/s/e/w script>, exit=<ms>, shot=<name.png> (posted on exit),
// quit=1 (with shot: tells the local server to stop after saving the capture).
'use strict';

const KEY = { NEXT: 1, PREV: 2, SEL: 4, ESC: 8 };
const params = new URLSearchParams(location.search);
const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const image = ctx.createImageData(320, 170);
const logEl = document.getElementById('log');
const statusEl = document.getElementById('status');

const DEMOS = ['rer', 'metro', 'transilien', 'tram', 'bus'];
const demo = DEMOS.includes(params.get('demo')) ? params.get('demo') : (params.get('demo') ? 'rer' : '');
document.getElementById(demo ? 'nav-demo-' + demo : 'nav-app').setAttribute('aria-current', 'page');

function appendLog(text, cls) {
  const line = document.createElement('div');
  line.textContent = text;
  if (cls) line.className = cls;
  logEl.appendChild(line);
  while (logEl.childNodes.length > 500) logEl.removeChild(logEl.firstChild);
  logEl.scrollTop = logEl.scrollHeight;
}

if (!self.crossOriginIsolated) {
  statusEl.textContent = 'Page non isolée (COOP/COEP) : lance le simulateur avec scripts/sim.ps1.';
  statusEl.classList.add('error');
  throw new Error('crossOriginIsolated is required');
}

const keyBuffer = new SharedArrayBuffer(4);
const keys = new Int32Array(keyBuffer);
const press = (bit) => Atomics.or(keys, 0, bit);

function drawFrame(px) {
  const d = image.data;
  for (let i = 0, j = 0; i < px.length; i++, j += 4) {
    const c = px[i];
    const r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    d[j] = (r << 3) | (r >> 2);
    d[j + 1] = (g << 2) | (g >> 4);
    d[j + 2] = (b << 3) | (b >> 2);
    d[j + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
}

function savePng() {
  canvas.toBlob((blob) => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `idfm-sim-${new Date().toISOString().replace(/[:.]/g, '-')}.png`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }, 'image/png');
}

let shotPosted = false;
function postShot() {
  const name = params.get('shot');
  if (!name || shotPosted) return;
  shotPosted = true;
  canvas.toBlob((blob) => {
    const q = new URLSearchParams({ name });
    if (params.get('quit')) q.set('quit', '1');
    fetch('/shot?' + q, { method: 'POST', body: blob });
  }, 'image/png');
}

// ---- input ----
window.addEventListener('keydown', (e) => {
  const map = {
    ArrowRight: KEY.NEXT, ArrowDown: KEY.NEXT, ArrowLeft: KEY.PREV, ArrowUp: KEY.PREV,
    Enter: KEY.SEL, ' ': KEY.SEL, Escape: KEY.ESC, Backspace: KEY.ESC,
  };
  if (map[e.key]) { press(map[e.key]); e.preventDefault(); return; }
  if (e.key === 'p' || e.key === 'P') savePng();
  if (e.key === 'r' || e.key === 'R') location.reload();
});
canvas.addEventListener('wheel', (e) => { press(e.deltaY > 0 ? KEY.NEXT : KEY.PREV); e.preventDefault(); }, { passive: false });
canvas.addEventListener('click', () => press(KEY.SEL));
canvas.addEventListener('contextmenu', (e) => { press(KEY.ESC); e.preventDefault(); });
for (const [id, bit] of [['btn-prev', KEY.PREV], ['btn-next', KEY.NEXT], ['btn-sel', KEY.SEL], ['btn-esc', KEY.ESC]]) {
  document.getElementById(id).addEventListener('click', () => press(bit));
}

// ---- program ----
const args = ['idfm_sim'];
if (demo) args.push('--demo', demo);
if (params.get('keys')) args.push('--keys', params.get('keys'));
if (params.get('exit')) args.push('--exit-after', params.get('exit'));

const worker = new Worker('worker.js');
worker.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'frame') {
    drawFrame(m.px);
    if (statusEl.textContent === 'Chargement…') statusEl.textContent = 'En marche';
  } else if (m.type === 'log') {
    appendLog(m.text);
  } else if (m.type === 'quit') {
    statusEl.textContent = 'Programme terminé · R pour redémarrer';
    postShot();
  } else if (m.type === 'error') {
    statusEl.textContent = 'Erreur du programme (voir le journal)';
    statusEl.classList.add('error');
    appendLog(m.text, 'error');
    postShot();
  }
};
worker.postMessage({ wasmUrl: '/idfm_sim.wasm', args, keyBuffer });
canvas.focus();
