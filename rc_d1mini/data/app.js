'use strict';
// 飛控板與遙控器共用的網頁前端，依 /api/info 的 features 顯示對應功能。

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];

let info = { features: {} };
let pollTimer = null;

// ---------------------------------------------------------------------------
// 工具
async function api(path, opts = {}) {
  const init = { method: opts.method || 'GET', cache: 'no-store' };
  if (opts.form) {
    init.method = opts.method || 'POST';
    init.body = new URLSearchParams(opts.form);
  }
  const res = await fetch(path, init);
  let data = {};
  try { data = await res.json(); } catch (e) { /* 非 JSON */ }
  if (!res.ok) throw new Error(data.msg || `HTTP ${res.status}`);
  return data;
}

function toast(msg, err = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'show' + (err ? ' err' : '');
  clearTimeout(t._h);
  t._h = setTimeout(() => (t.className = ''), 3500);
}

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function kv(el, rows) {
  el.innerHTML = rows
    .filter(r => r)
    .map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v === undefined || v === '' ? '—' : esc(v)}</dd>`)
    .join('');
}

function badges(el, list) {
  el.innerHTML = list.filter(b => b).map(([text, cls]) => `<span class="badge ${cls || ''}">${esc(text)}</span>`).join('');
}

function bytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(2) + ' MB';
}

function duration(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return (d ? d + ' 天 ' : '') + (h ? h + ' 時 ' : '') + m + ' 分 ' + (s % 60) + ' 秒';
}

function rssiText(r) {
  if (r === undefined) return '';
  const q = r >= -55 ? '極佳' : r >= -67 ? '良好' : r >= -75 ? '普通' : '微弱';
  return `${r} dBm（${q}）`;
}

function deg(v) {
  return (v >= 0 ? '+' : '') + Number(v).toFixed(1) + '°';
}

function busy(btn, on) {
  if (btn) btn.disabled = on;
}

// ---------------------------------------------------------------------------
// 路由
const pages = {
  status: { enter: () => poll(loadStatus, 2000) },
  network: { enter: loadNetwork },
  pid: { enter: () => { loadPid(); poll(loadAttitude, 200); } },
  ota: {},
  user: { enter: () => ($('#userForm').user.value = info.user || '') },
};

function poll(fn, ms) {
  const tick = async () => {
    try { await fn(); } catch (e) { /* 下次再試 */ }
    pollTimer = setTimeout(tick, ms);
  };
  tick();
}

function route() {
  clearTimeout(pollTimer);
  pollTimer = null;
  let name = location.hash.slice(1);
  const page = $('#page-' + name);
  if (!pages[name] || !page || page.classList.contains('off')) name = 'status';
  $$('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + name));
  $$('nav a').forEach(a => a.classList.toggle('active', a.getAttribute('href') === '#' + name));
  document.body.classList.remove('menu-open');
  $('#topTitle').textContent = `${info.name || ''} · ${$('nav a.active')?.textContent || ''}`;
  pages[name].enter?.();
}

// ---------------------------------------------------------------------------
// 系統狀態
async function loadStatus() {
  const s = await api('/api/status');
  const w = s.wifi, sta = w.sta, ap = w.ap;
  kv($('#wifiInfo'), [
    ['模式', w.mode],
    ['STA 狀態', sta.connected ? '已連線' : sta.configured ? '連線中…' : '未設定'],
    ['SSID', sta.ssid],
    sta.connected && ['IP 位址', sta.ip],
    sta.connected && ['子網路遮罩', sta.mask],
    sta.connected && ['閘道', sta.gw],
    sta.connected && ['DNS', sta.dns],
    sta.connected && ['訊號', rssiText(sta.rssi)],
    sta.connected && ['頻道', sta.ch],
    ['MAC', sta.mac],
    ['AP', ap.active ? `${ap.ssid}（${ap.ip}，${ap.clients} 台連線）` : '關閉'],
  ]);

  if (s.eth) {
    const e = s.eth;
    kv($('#ethInfo'), [
      ['網路線', e.link ? `已連接 ${e.speed} Mbps ${e.fullDuplex ? '全雙工' : '半雙工'}` : '未連接'],
      ['設定', e.dhcp ? 'DHCP' : '固定 IP'],
      ['IP 位址', e.ip],
      ['子網路遮罩', e.mask],
      ['閘道', e.gw],
      ['DNS', e.dns],
      ['MAC', e.mac],
    ]);
  }

  kv($('#sysInfo'), [
    ['裝置', info.name],
    ['韌體版本', info.fw],
    ['運行時間', duration(s.uptime)],
    ['可用記憶體', bytes(s.heap)],
  ]);

  const fs = s.fs;
  $('#fsBar').style.width = (fs.total ? (fs.used / fs.total) * 100 : 0) + '%';
  $('#fsSummary').textContent = `已使用 ${bytes(fs.used)} / ${bytes(fs.total)}（剩餘 ${bytes(fs.total - fs.used)}）`;
  $('#fsFiles').innerHTML = fs.files.length
    ? fs.files.sort((a, b) => a.name.localeCompare(b.name))
        .map(f => `<tr><td>${esc(f.name)}</td><td class="num">${bytes(f.size)}</td></tr>`).join('')
    : '<tr><td colspan="2" class="muted">（沒有檔案）</td></tr>';

  if (s.flight) renderFlight(s.flight);
  if (s.rc) renderRc(s.rc);
}

function renderFlight(f) {
  badges($('#flightBadges'), [
    f.armed ? ['已解鎖', 'bad'] : ['已上鎖', 'ok'],
    f.failsafe && ['失控保護', 'bad'],
    [f.imuOk ? 'IMU 正常' : 'IMU 異常', f.imuOk ? 'ok' : 'bad'],
    [f.linkOk ? '遙控連線中' : '無遙控訊號', f.linkOk ? 'ok' : 'warn'],
    f.lockout && ['系統鎖定', 'warn'],
  ]);
  kv($('#flightInfo'), [
    ['Roll / Pitch', `${deg(f.roll)} / ${deg(f.pitch)}`],
    ['Yaw 角速度', f.yawRate + ' °/s'],
    ['馬達 (µs)', f.motors.join(' / ')],
    f.vbat > 0 && ['電池', f.vbat.toFixed(2) + ' V'],
    ['遙控器', f.link.peer ? `${f.link.peer}（${f.link.rate} 包/秒）` : '—'],
    ['控制迴圈', `${f.loopUs} µs（最大 ${f.loopMaxUs} µs）`],
    !f.armed && ['上鎖原因', f.disarmReason],
  ]);
}

function renderRc(r) {
  const t = r.telem, l = r.link;
  badges($('#rcBadges'), [
    [l.ok ? '飛控連線中' : '未連線飛控', l.ok ? 'ok' : 'warn'],
    r.arm ? ['解鎖開關 ON', 'bad'] : ['解鎖開關 OFF', 'ok'],
    l.ok && t.armed && ['飛控已解鎖', 'bad'],
    l.ok && t.failsafe && ['飛控失控保護', 'bad'],
    l.ok && !t.imuOk && ['飛控 IMU 異常', 'bad'],
  ]);
  const sk = r.sticks;
  kv($('#rcInfo'), [
    ['油門', sk.thr],
    ['Roll / Pitch / Yaw', `${sk.roll} / ${sk.pitch} / ${sk.yaw}`],
    ['目標', l.target],
    ['飛控 IP', l.fcIp],
    ['發送', `${l.txRate} 包/秒`],
    l.ok && ['飛控姿態', `${deg(t.roll)} / ${deg(t.pitch)}`],
    l.ok && t.vbat > 0 && ['飛控電池', t.vbat.toFixed(2) + ' V'],
    l.ok && ['回應延遲', l.age + ' ms'],
  ]);
}

// ---------------------------------------------------------------------------
// 網路設定
async function loadNetwork() {
  const s = await api('/api/status');
  const wf = $('#wifiForm');
  wf.ssid.value = s.wifi.sta.ssid || '';
  wf.pass.value = '';
  if (s.eth) {
    const f = $('#ethForm'), e = s.eth;
    f.querySelector(`[name=dhcp][value="${e.dhcp ? 1 : 0}"]`).checked = true;
    f.ip.value = e.cfgIp; f.gw.value = e.cfgGw; f.mask.value = e.cfgMask; f.dns.value = e.cfgDns;
    syncEthStatic();
  }
  if (info.features.target) {
    const t = await api('/api/target');
    const f = $('#targetForm');
    f.ip.value = t.ip; f.port.value = t.port;
  }
}

function syncEthStatic() {
  $('#ethStatic').disabled = $('#ethForm').querySelector('[name=dhcp]:checked')?.value === '1';
}

async function scanWifi() {
  const btn = $('#scanBtn'), st = $('#scanState'), list = $('#scanList');
  busy(btn, true);
  st.textContent = '掃描中…';
  list.innerHTML = '';
  try {
    await api('/api/wifi/scan', { method: 'POST' });
    let r;
    for (let i = 0; i < 30; i++) {
      await new Promise(ok => setTimeout(ok, 700));
      r = await api('/api/wifi/scan');
      if (r.status === 'done') break;
    }
    if (!r || r.status !== 'done') throw new Error('掃描逾時');
    const nets = new Map();
    for (const n of r.networks) {
      if (n.ssid && (!nets.has(n.ssid) || nets.get(n.ssid).rssi < n.rssi)) nets.set(n.ssid, n);
    }
    const sorted = [...nets.values()].sort((a, b) => b.rssi - a.rssi);
    st.textContent = `找到 ${sorted.length} 個網路`;
    list.innerHTML = sorted.map(n =>
      `<li data-ssid="${esc(n.ssid)}"><span>${n.secure ? '&#128274; ' : ''}${esc(n.ssid)}</span>` +
      `<span class="sig">${n.rssi} dBm · CH${n.ch}</span></li>`).join('');
  } catch (e) {
    st.textContent = e.message;
  } finally {
    busy(btn, false);
  }
}

// ---------------------------------------------------------------------------
// PID 設定
let pidData = null;

function fillPidForm(p) {
  const f = $('#pidForm');
  for (const ax of ['roll', 'pitch', 'yaw'])
    for (const k of ['kp', 'ki', 'kd']) f[`${ax}_${k}`].value = +p[ax][k].toFixed(4);
  for (const k of ['levelKp', 'maxAngle', 'maxYawRate', 'iLimit']) f[k].value = +p[k].toFixed(3);
}

async function loadPid() {
  pidData = await api('/api/pid');
  fillPidForm(pidData);
  const same = ['kp', 'ki', 'kd'].every(k => pidData.roll[k] === pidData.pitch[k]);
  $('#linkRP').checked = same;
  syncLinkRP();
  $('#trimInfo').textContent = `目前水平修正：Roll ${pidData.trimRoll.toFixed(2)}°、Pitch ${pidData.trimPitch.toFixed(2)}°`;
}

function syncLinkRP() {
  const on = $('#linkRP').checked, f = $('#pidForm');
  for (const k of ['kp', 'ki', 'kd']) {
    f[`pitch_${k}`].disabled = on;
    if (on) f[`pitch_${k}`].value = f[`roll_${k}`].value;
  }
}

let lastCal = 'idle';
async function loadAttitude() {
  const f = await api('/api/flight');
  $('#horizonSky').style.transform = `translateY(${Math.max(-60, Math.min(60, f.pitch * 1.5))}px) rotate(${-f.roll}deg)`;
  kv($('#attInfo'), [
    ['Roll', deg(f.roll)],
    ['Pitch', deg(f.pitch)],
    ['Yaw', f.yawRate + ' °/s'],
    ['狀態', f.armed ? '已解鎖' : '已上鎖'],
    ['馬達', f.motors.join(' ')],
  ]);
  const cal = { idle: '', running: '校正中…', done: '校正完成', failed: '校正失敗：機身有晃動，請重試' }[f.cal];
  $('#calState').textContent = cal;
  if (lastCal === 'running' && f.cal === 'done') loadPid();
  lastCal = f.cal;
}

// ---------------------------------------------------------------------------
// OTA
function uploadOta(e) {
  e.preventDefault();
  const form = e.target, file = form.file.files[0];
  if (!file) return;
  const type = form.querySelector('[name=type]:checked').value;
  const btn = form.querySelector('button'), bar = $('#otaBar'), st = $('#otaState');
  const fd = new FormData();
  fd.append('file', file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota?type=' + type);
  xhr.upload.onprogress = ev => {
    if (!ev.lengthComputable) return;
    const pct = (ev.loaded / ev.total) * 100;
    bar.style.width = pct + '%';
    st.textContent = `上傳中 ${pct.toFixed(0)}%（${bytes(ev.loaded)} / ${bytes(ev.total)}）`;
  };
  xhr.onload = () => {
    let r = {};
    try { r = JSON.parse(xhr.responseText); } catch (err) { /* ignore */ }
    if (xhr.status === 200 && r.ok) {
      st.textContent = r.msg + ' 稍候自動重新整理…';
      waitReboot();
    } else {
      st.textContent = r.msg || `更新失敗 (HTTP ${xhr.status})`;
      busy(btn, false);
    }
  };
  xhr.onerror = () => { st.textContent = '連線中斷'; busy(btn, false); };
  busy(btn, true);
  bar.style.width = '0';
  xhr.send(fd);
}

function waitReboot() {
  setTimeout(async function check() {
    try {
      await api('/api/info');
      location.reload();
    } catch (e) {
      setTimeout(check, 2000);
    }
  }, 5000);
}

// ---------------------------------------------------------------------------
// 事件
function bind() {
  window.addEventListener('hashchange', route);
  $('#menuBtn').onclick = () => document.body.classList.toggle('menu-open');
  $('#scrim').onclick = () => document.body.classList.remove('menu-open');

  $('#rebootBtn').onclick = async () => {
    if (!confirm('確定要重新啟動裝置？')) return;
    try {
      toast((await api('/api/reboot', { method: 'POST' })).msg);
      waitReboot();
    } catch (e) { toast(e.message, true); }
  };

  $$('.pw-toggle').forEach(b => (b.onclick = () => {
    const i = b.previousElementSibling;
    i.type = i.type === 'password' ? 'text' : 'password';
  }));

  // WiFi
  $('#scanBtn').onclick = scanWifi;
  $('#scanList').onclick = e => {
    const li = e.target.closest('li');
    if (!li) return;
    const f = $('#wifiForm');
    f.ssid.value = li.dataset.ssid;
    f.pass.value = '';
    f.pass.focus();
  };
  $('#wifiForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target;
    try {
      toast((await api('/api/wifi', { form: { ssid: f.ssid.value, pass: f.pass.value } })).msg);
    } catch (err) { toast(err.message, true); }
  };
  $('#wifiClearBtn').onclick = async () => {
    if (!confirm('確定清除 WiFi 連線設定？裝置將改為 AP 模式。')) return;
    try {
      toast((await api('/api/wifi/clear', { method: 'POST' })).msg);
      $('#wifiForm').reset();
    } catch (e) { toast(e.message, true); }
  };

  // RJ45
  $$('#ethForm [name=dhcp]').forEach(r => (r.onchange = syncEthStatic));
  $('#ethForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target, dhcp = f.querySelector('[name=dhcp]:checked').value;
    if (!confirm('儲存後裝置會重新啟動，確定？')) return;
    try {
      toast((await api('/api/eth', { form: { dhcp, ip: f.ip.value, gw: f.gw.value, mask: f.mask.value, dns: f.dns.value } })).msg);
      if (dhcp === '0') toast(`重新啟動後請改連 http://${f.ip.value}/`);
      waitReboot();
    } catch (err) { toast(err.message, true); }
  };

  // 飛控連線（遙控器）
  $('#targetForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target;
    try {
      toast((await api('/api/target', { form: { ip: f.ip.value.trim(), port: f.port.value } })).msg);
    } catch (err) { toast(err.message, true); }
  };

  // PID
  $('#linkRP').onchange = syncLinkRP;
  $('#pidForm').addEventListener('input', e => {
    if ($('#linkRP').checked && e.target.name?.startsWith('roll_'))
      $('#pidForm')[e.target.name.replace('roll_', 'pitch_')].value = e.target.value;
  });
  $('#pidForm').onsubmit = async e => {
    e.preventDefault();
    syncLinkRP();
    const f = e.target, body = {};
    for (const el of f.elements) {
      if (!el.name || el.type === 'checkbox') continue;
      const v = Number(el.value);
      if (!Number.isFinite(v) || el.value.trim() === '') return toast(`「${el.name}」不是有效數字`, true);
      body[el.name] = v;
    }
    try {
      toast((await api('/api/pid', { form: body })).msg);
      loadPid();
    } catch (err) { toast(err.message, true); }
  };
  $('#pidDefaultsBtn').onclick = () => {
    if (!pidData) return;
    fillPidForm(pidData.defaults);
    $('#linkRP').checked = true;
    syncLinkRP();
    toast('已填入預設值，按「套用並儲存」生效');
  };
  $('#pidReloadBtn').onclick = loadPid;
  $('#calBtn').onclick = async () => {
    if (!confirm('請將機身放在水平面上並保持靜止，開始校正？')) return;
    try { toast((await api('/api/calibrate', { method: 'POST' })).msg); } catch (e) { toast(e.message, true); }
  };

  // OTA
  $('#otaForm').onsubmit = uploadOta;

  // 使用者
  $('#userForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target;
    if (f.pass.value !== f.pass2.value) return toast('兩次輸入的密碼不一致', true);
    try {
      toast((await api('/api/user', { form: { user: f.user.value.trim(), pass: f.pass.value } })).msg);
      f.reset();
      setTimeout(() => location.reload(), 1500);
    } catch (err) { toast(err.message, true); }
  };
}

async function init() {
  bind();
  try {
    info = await api('/api/info');
  } catch (e) {
    toast('無法讀取裝置資訊：' + e.message, true);
  }
  document.title = info.name || '控制面板';
  $('#brandName').textContent = info.name || '—';
  $('#brandFw').textContent = info.fw ? '韌體 v' + info.fw : '';
  $('#sideUser').textContent = info.user ? '登入：' + info.user : '';
  $$('[data-feature]').forEach(el => el.classList.toggle('off', !info.features[el.dataset.feature]));
  route();
}

init();
