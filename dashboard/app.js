const BASE = ((window.APP_CONFIG && window.APP_CONFIG.API_BASE) || '').replace(/\/+$/, '');
const POLL_MS = 1000, TIMEOUT_MS = 6000, TZ = 'Asia/Colombo';

const $ = s => document.querySelector(s), f = (n, d = 2) => Number(n).toFixed(d);
let token = null, st = null, held = null, dragging = false, polling = false;
let dailyWastageWs = 0;
let wasteDate = new Date().toLocaleDateString('en-US', { timeZone: TZ });
let lastPollTime = Date.now();

try {
  token = sessionStorage.getItem('tok');
  const storedWaste = JSON.parse(localStorage.getItem('energy_waste'));
  if (storedWaste && storedWaste.date === wasteDate) dailyWastageWs = storedWaste.ws;
} catch (e) {}

function updateClock() {
  const d = new Date();
  $('#timeDisp').textContent = d.toLocaleTimeString('en-US', { timeZone: TZ, hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: true });
  $('#dateDisp').textContent = d.toLocaleDateString('en-US', { timeZone: TZ, weekday: 'short', year: 'numeric', month: 'short', day: 'numeric' });
}
setInterval(updateClock, 1000);
updateClock();

const sha = async s => [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s)))].map(b => b.toString(16).padStart(2, '0')).join('');

async function api(path, o = {}) {
  const ac = new AbortController(), t = setTimeout(() => ac.abort(), TIMEOUT_MS);
  try {
    const headers = { 'ngrok-skip-browser-warning': '1' };
    if (o.body) headers['Content-Type'] = 'application/json';
    if (token) headers.Authorization = 'Bearer ' + token;
    const r = await fetch(BASE + path, { ...o, headers, signal: ac.signal });
    const j = await r.json().catch(() => ({}));
    if (r.status === 401 && token) setToken(null);
    return j;
  } finally { clearTimeout(t); }
}

function toast(m) {
  const t = $('#toast');
  t.textContent = m;
  t.classList.add('show');
  clearTimeout(toast.t);
  toast.t = setTimeout(() => t.classList.remove('show'), 2200);
}

function playErrorSound() {
  try {
    const ctx = new (window.AudioContext || window.webkitAudioContext)();
    const osc = ctx.createOscillator(), gain = ctx.createGain();
    osc.type = 'square';
    osc.frequency.setValueAtTime(180, ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(40, ctx.currentTime + 0.2);
    gain.gain.setValueAtTime(0.3, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + 0.2);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start();
    osc.stop(ctx.currentTime + 0.2);
  } catch (e) {}
}

function showLoginError(msg) {
  const el = $('#err');
  el.textContent = msg;
  void el.offsetWidth;
  el.classList.add('glitch');
  playErrorSound();
}

$('#theme').onclick = () => {
  const d = document.documentElement, n = d.dataset.theme === 'dark' ? 'light' : 'dark';
  d.dataset.theme = n;
  try { localStorage.setItem('theme', n); } catch (e) {}
};

function setToken(t) {
  token = t;
  try { t ? sessionStorage.setItem('tok', t) : sessionStorage.removeItem('tok'); } catch (e) {}
  $('#ctl').classList.toggle('locked', !t);
  $('#authBtn').textContent = t ? 'Sign out' : 'Sign in';
}
setToken(token);

const openLogin = () => {
  $('#err').textContent = '';
  $('#err').classList.remove('glitch');
  $('#pw').value = '';
  $('#dlg').showModal();
  $('#pw').focus();
};
$('#authBtn').onclick = () => token ? (setToken(null), toast('Signed out')) : openLogin();
$('#unlock').onclick = openLogin;
$('#cancel').onclick = () => $('#dlg').close();

$('#form').addEventListener('submit', async e => {
  e.preventDefault();
  $('#err').textContent = '';
  $('#err').classList.remove('glitch');
  try {
    const { nonce } = await api('/api/challenge');
    if (!nonce) throw new Error('no nonce');
    const proof = await sha(await sha($('#pw').value) + nonce);
    const r = await api('/api/login', { method: 'POST', body: JSON.stringify({ nonce, proof }) });
    if (!r.token) { showLoginError(r.error || 'Access Denied'); return; }
    setToken(r.token);
    $('#dlg').close();
    toast('Controls unlocked');
  } catch (x) {
    showLoginError('Cannot reach the node. Check the API URL.');
  }
});

async function send(body) {
  try {
    const r = await api('/api/control', { method: 'POST', body: JSON.stringify(body) });
    if (r.ok) { toast('Sent to node'); if (r.power) { st = r; render(); } }
    else toast(r.error === 'unauthorized' ? 'Session expired. Sign in again.' : (r.error || 'Node did not accept the command'));
  } catch (e) { toast('Cannot reach the node'); }
}

const setPct = v => { $('#pct').textContent = v + '%'; $('#sl').style.setProperty('--p', v + '%'); };
const duty = pct => Math.round(pct * 2.55);
let tm = 0;
$('#sl').addEventListener('input', e => {
  dragging = true;
  setPct(e.target.value);
  const n = Date.now();
  if (n - tm > 250) { tm = n; send({ mode: 'manual', brightness: duty(e.target.value) }); }
});
$('#sl').addEventListener('change', e => { dragging = false; send({ mode: 'manual', brightness: duty(e.target.value) }); });
$('#mode').onchange = e => send({ mode: e.target.checked ? 'manual' : 'auto' });
$('#on').onclick = () => send({ mode: 'manual', brightness: duty($('#sl').value > 0 ? $('#sl').value : 100) });
$('#off').onclick = () => send({ mode: 'manual', brightness: 0 });

$('#hold').onclick = () => {
  if (held) held = null;
  else if (st) {
    const p = st.power, avgW = Number.isFinite(p.avgW10) ? p.avgW10 : p.watts;
    held = { ...p, avgW, avgA: p.volts > 0 ? avgW / p.volts : 0, at: new Date().toLocaleTimeString('en-US', { timeZone: TZ }) };
  }
  render();
};

function render() {
  if (!st) return;
  const p = st.power, s = st.sensors, pct = Math.round(st.brightness / 2.55);

  const pill = $('#pill');
  pill.className = 'pill ' + (st.online ? 'on' : 'off');
  pill.querySelector('b').textContent = st.online ? 'Node online' : 'Node offline';

  $('#w').textContent = f(p.watts);
  $('#v').textContent = f(p.volts, 1);
  $('#a').textContent = f(p.amps, 3);
  $('#br').textContent = pct + '%';
  $('#bar').style.width = pct + '%';

  $('#holdSec').classList.toggle('holding', !!held);
  $('#hold').classList.toggle('held', !!held);
  $('#hold').textContent = held ? 'Release hold' : 'Hold reading';

  $('#mode').checked = st.mode === 'manual';
  if (!dragging) { $('#sl').value = pct; setPct(pct); }

  const yn = (b, a, z) => `<b><i class="dot ${b ? 'on' : ''}"></i>${b ? a : z}</b>`;
  $('#chips').innerHTML = [
    ['Ambient light (LDR)', `<b>${s.ldr}<small style="font-weight:400;color:var(--mu)">/ 4095</small></b>`],
    ['PIR sensor', yn(s.pir1, 'Detected', 'Clear')],
    ['Room motion', yn(s.motion, 'Occupied', 'Empty')],
    ['Lamp', yn(s.lamp, 'On', 'Off')],
    ['System mode', `<b>${st.mode === 'manual' ? 'Manual' : 'Automatic'}</b>`]
  ].map(c => `<div class="chip"><span>${c[0]}</span>${c[1]}</div>`).join('');

  const rows = [
    { label: 'Voltage', unit: 'V', live: p.volts, held: held?.volts, d: 1 },
    { label: 'Instantaneous current', unit: 'A', live: p.amps, held: held?.amps, d: 3 },
    { label: 'Active power', unit: 'W', live: p.watts, held: held?.watts, d: 2 },
    { label: 'Runtime', unit: 'h', live: p.runtimeH, held: held?.runtimeH, d: 3 },
    { label: 'Cumulative energy', unit: 'Wh', live: p.wh, held: held?.wh, d: 4 },
    { label: 'Wasted today (Daytime)', unit: 'Wh', live: dailyWastageWs / 3600, held: null, d: 4 }
  ];
  $('#rows').innerHTML = rows.map(r => `<tr><td>${r.label}</td><td>${f(r.live, r.d)}</td><td>${r.held != null ? f(r.held, r.d) : '–'}</td><td>${r.unit}</td></tr>`).join('');

  $('#heldGrid').innerHTML = held
    ? `<div><span>Power at hold</span><b>${f(held.watts)} W</b></div><div><span>Average power</span><b>${f(held.avgW)} W</b></div><div><span>Current at hold</span><b>${f(held.amps, 3)} A</b></div><div><span>Average current</span><b>${f(held.avgA, 3)} A</b></div>`
    : '';
  $('#heldNote').textContent = held ? `Held at ${held.at}. Average of the last 10 s.` : 'Freezes this section and averages the last 10 seconds of power draw.';
}

function setOffline(msg) {
  const p = $('#pill');
  p.className = 'pill off';
  p.querySelector('b').textContent = msg;
}

function trackDaytimeWaste(deltaSec) {
  const hr = parseInt(new Intl.DateTimeFormat('en-US', { timeZone: TZ, hour: 'numeric', hour12: false }).format(new Date()), 10);
  const curDate = new Date().toLocaleDateString('en-US', { timeZone: TZ });
  if (curDate !== wasteDate) { dailyWastageWs = 0; wasteDate = curDate; }
  if (hr >= 6 && hr < 18 && st.brightness > 0) {
    dailyWastageWs += st.power.watts * deltaSec;
    try { localStorage.setItem('energy_waste', JSON.stringify({ date: wasteDate, ws: dailyWastageWs })); } catch (e) {}
  }
}

async function poll() {
  if (polling || document.hidden) { lastPollTime = Date.now(); return; }
  polling = true;
  try {
    const j = await api('/api/status');
    const now = Date.now();
    const deltaSec = Math.min((now - lastPollTime) / 1000, 10);
    lastPollTime = now;
    if (j && j.power && j.sensors) {
      st = j;
      trackDaytimeWaste(deltaSec);
      render();
    } else setOffline('Node offline');
  } catch (e) { setOffline('Server unreachable'); }
  finally { polling = false; }
}

if (!BASE) setOffline('Set API_BASE in config.js');
else { poll(); setInterval(poll, POLL_MS); document.addEventListener('visibilitychange', poll); }
