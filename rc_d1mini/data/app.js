'use strict';
// 飛控板與遙控器共用的網頁前端，依 /api/info 的 features 顯示對應功能。

// ---------------------------------------------------------------------------
// 網頁版本：網頁檔（SPIFFS）與韌體分開更新，各自有版本號。修改網頁檔時請更新此處。
//   1.3.0  主題改為右上角單一圖示按鈕、重新啟動顯示經過秒數、OTA 頁顯示 spiffs.bat 指令
//   1.2.0  DO 設定頁、編譯時間、OTA 先檢查 SPIFFS 映像大小
//   1.1.0  搖桿校正、OLED 顯示、初始免登入、明亮／黑暗／玻璃三種主題
//   1.0.0  初版：系統狀態、網路設定、PID 設定、OTA、使用者設定
// ---------------------------------------------------------------------------
const WEB_VERSION = '1.3.0';

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
  if (opts.timeout) init.signal = AbortSignal.timeout(opts.timeout);
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
  sticks: { enter: () => { stickFormLoaded = false; poll(loadSticks, 150); } },
  oled: { enter: loadOled },
  do: { enter: () => { doLoaded.fill(false); poll(loadDo, 1000); } },
  ota: { enter: () => {
    $('#otaFsHint').textContent = info.fsPart
      ? `本裝置 SPIFFS 分區大小：${info.fsPart} bytes（${fsHex()}），SPIFFS 映像請用「${spiffsCmd()}」產生。`
      : '';
  } },
  user: { enter: () => { $('#userForm').user.value = info.user || ''; renderAuth(); } },
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
// 主題（存在瀏覽器，各裝置 / 各瀏覽器分開記憶）
const THEMES = ['light', 'dark', 'glass'];
const THEME_NAMES = { light: '明亮', dark: '黑暗', glass: '玻璃' };
const SVG = (body) => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
  stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
const THEME_ICONS = {
  // 太陽
  light: SVG('<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>'),
  // 月亮
  dark: SVG('<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>'),
  // 閃光（玻璃）
  glass: SVG('<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z"/><path d="M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"/>'),
};

function setTheme(t, save = true) {
  if (!THEMES.includes(t)) t = 'light';
  document.documentElement.setAttribute('data-theme', t);
  // 按鈕顯示目前主題的圖示，提示文字說明下一個主題
  const next = THEMES[(THEMES.indexOf(t) + 1) % THEMES.length];
  const btn = $('#themeBtn');
  btn.innerHTML = THEME_ICONS[t];
  btn.title = `主題：${THEME_NAMES[t]}（點擊切換為${THEME_NAMES[next]}）`;
  btn.setAttribute('aria-label', btn.title);
  if (save) {
    try { localStorage.setItem('theme', t); } catch (e) { /* 無痕模式等情況無法儲存 */ }
  }
}

// ---------------------------------------------------------------------------
// 登入狀態
function renderAuth() {
  const on = !!info.authRequired;
  $('#sideUser').textContent = on ? '登入：' + info.user : '未設定登入帳密';
  const note = $('#authNote');
  note.className = 'auth-note ' + (on ? 'locked' : 'open');
  note.textContent = on
    ? `已啟用登入，帳號：${info.user}`
    : '目前未設定帳密，任何連上此網路的人都能開啟網頁。設定帳號密碼後即需登入。';
  $('#userClearBtn').classList.toggle('hidden', !on);
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
    ['韌體編譯時間', info.build],
    ['網頁版本', WEB_VERSION],
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
  if (s.rc) renderRc(s.rc, s.do || []);
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

function renderRc(r, dos) {
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
    ['封包', `送出 ${l.txRate} 包/秒，收到 ${l.rxRate ?? 0} 包/秒`],
    l.ok && ['飛控姿態', `${deg(t.roll)} / ${deg(t.pitch)}`],
    l.ok && t.vbat > 0 && ['飛控電池', t.vbat.toFixed(2) + ' V'],
    l.ok && ['回應延遲', l.age + ' ms'],
    ...dos.map(d => [d.name, d.on ? 'ON' : 'OFF']),
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
// 搖桿校正（遙控器）
const STICK_AXES = ['油門', 'Roll', 'Pitch', 'Yaw'];
let stickFormLoaded = false;

function buildStickRows() {
  $('#stickRows').innerHTML = STICK_AXES.map((name, i) =>
    `<div class="stick-row" id="stick${i}">` +
    `<span class="stick-name">${name}</span>` +
    `<div class="track"><div class="range"></div><div class="seen"></div><div class="center"></div><div class="pos"></div></div>` +
    `<span class="stick-val"><b class="out"></b> <span class="raw muted small"></span></span></div>`).join('');
}

function adcPct(v) {
  return (Math.max(0, Math.min(4095, v)) / 4095) * 100 + '%';
}

async function loadSticks() {
  const s = await api('/api/sticks');
  if (!$('#stick0')) buildStickRows();

  s.axes.forEach((a, i) => {
    const row = $('#stick' + i), raw = s.live.raw[i], out = s.live.out[i];
    const range = $('.range', row), seen = $('.seen', row);
    range.style.left = adcPct(a.min);
    range.style.width = `calc(${adcPct(a.max)} - ${adcPct(a.min)})`;
    $('.center', row).style.left = adcPct(a.center);
    $('.center', row).style.display = i === 0 ? 'none' : '';
    $('.pos', row).style.left = adcPct(raw);
    if (s.calibrating && a.seenMin !== undefined) {
      seen.style.display = 'block';
      seen.style.left = adcPct(a.seenMin);
      seen.style.width = `calc(${adcPct(a.seenMax)} - ${adcPct(a.seenMin)})`;
    } else {
      seen.style.display = 'none';
    }
    $('.out', row).textContent = i === 0 ? out : (out > 0 ? '+' : '') + out;
    $('.raw', row).textContent = `ADC ${raw}`;
  });

  badges($('#stickBadges'), [
    s.calibrating ? ['校正中', 'warn'] : s.calibrated ? ['已校正', 'ok'] : ['未校正（開機自動取中點）', 'warn'],
    s.live.arm ? ['解鎖開關 ON', 'bad'] : ['解鎖開關 OFF', 'ok'],
  ]);

  const msg = $('#stickCalMsg');
  if (s.calibrating) {
    const travel = s.axes.map((a, i) =>
      `${STICK_AXES[i]} ${a.seenMin !== undefined ? a.seenMax - a.seenMin : 0}`).join('、');
    msg.textContent = `校正中，目前行程：${travel}`;
  } else {
    msg.textContent = '';
  }
  msg.classList.toggle('active', s.calibrating);
  $('#stickStartBtn').disabled = s.calibrating;
  $('#stickFinishBtn').disabled = !s.calibrating;
  $('#stickCancelBtn').disabled = !s.calibrating;

  if (!stickFormLoaded) {
    const f = $('#stickForm');
    s.axes.forEach((a, i) => (f['inv' + i].checked = a.invert));
    f.deadband.value = s.deadband;
    stickFormLoaded = true;
  }
}

async function stickAction(path, confirmText) {
  if (confirmText && !confirm(confirmText)) return;
  try {
    toast((await api(path, { method: 'POST' })).msg);
    if (path.endsWith('/reset')) stickFormLoaded = false;
  } catch (e) {
    toast(e.message, true);
  }
}

// ---------------------------------------------------------------------------
// DO 設定（遙控器）
const DO_MODES = ['開關', '定時', '點動'];
const WEEKDAYS = ['日', '一', '二', '三', '四', '五', '六'];
const STRAPPING_PINS = [2, 5, 15];
const doLoaded = [false, false];

function buildDoCards(channels, pins) {
  const pinOptions = pins.map(p =>
    `<option value="${p}">GPIO${p}${STRAPPING_PINS.includes(p) ? '（開機腳，慎用）' : ''}</option>`).join('');
  const html = channels.map((c, i) => {
    const sched = c.sched.map((s, n) => `
      <div class="sched-row" data-n="${n}">
        <label class="check"><input type="checkbox" name="s${n}_en"> 排程 ${n + 1}</label>
        <input type="time" name="s${n}_on" required> ～ <input type="time" name="s${n}_off" required>
        <div class="days">${WEEKDAYS.map((w, k) =>
          `<label><input type="checkbox" name="s${n}_d${k}"><span>${w}</span></label>`).join('')}</div>
      </div>`).join('');
    return `
    <div class="card" id="do${i}">
      <div class="do-head"><h2 class="do-title"></h2><span class="badge do-state"></span></div>
      <div class="do-control">
        <button type="button" class="btn do-toggle"></button>
        <button type="button" class="btn primary do-pulse">觸發</button>
        <span class="do-note muted small"></span>
      </div>
      <form class="form do-form" data-ch="${i}">
        <label>名稱<input name="name" maxlength="23" required></label>
        <div class="fields-2">
          <label>輸出腳位<select name="pin">${pinOptions}</select></label>
          <label>觸發準位<select name="activeLow">
            <option value="0">高電位觸發</option><option value="1">低電位觸發</option></select></label>
        </div>
        <div class="seg">${DO_MODES.map((m, k) =>
          `<label><input type="radio" name="mode" value="${k}"> ${m}</label>`).join('')}</div>
        <label class="do-field-pulse">點動保持時間（秒，0.1~3600）
          <input name="pulseSec" type="number" min="0.1" max="3600" step="0.1" required></label>
        <div class="do-field-sched sched-list">${sched}</div>
        <button class="btn primary">儲存</button>
      </form>
    </div>`;
  }).join('');
  $('#doGrid').insertAdjacentHTML('beforeend', html);

  $$('.do-form').forEach(f => {
    f.addEventListener('change', () => syncDoForm(f));
    f.onsubmit = saveDo;
  });
  channels.forEach((c, i) => {
    const card = $('#do' + i);
    $('.do-toggle', card).onclick = e => doAction('/api/do/set', { ch: i, on: e.target.dataset.on === '1' ? '0' : '1' });
    $('.do-pulse', card).onclick = () => doAction('/api/do/pulse', { ch: i });
  });
}

// 依表單中選擇的模式顯示對應欄位
function syncDoForm(f) {
  const mode = f.querySelector('[name=mode]:checked')?.value;
  $('.do-field-pulse', f).classList.toggle('hidden', mode !== '2');
  $('.do-field-sched', f).classList.toggle('hidden', mode !== '1');
  $$('.sched-row', f).forEach(r => r.classList.toggle('off', !r.querySelector('[type=checkbox]').checked));
}

function fillDoForm(f, c) {
  f.name.value = c.name;
  f.pin.value = c.pin;
  f.activeLow.value = c.activeLow ? '1' : '0';
  f.querySelector(`[name=mode][value="${c.mode}"]`).checked = true;
  f.pulseSec.value = +(c.pulseMs / 1000).toFixed(1);
  c.sched.forEach((s, n) => {
    f[`s${n}_en`].checked = s.en;
    f[`s${n}_on`].value = s.on;
    f[`s${n}_off`].value = s.off;
    for (let k = 0; k < 7; k++) f[`s${n}_d${k}`].checked = !!(s.days >> k & 1);
  });
  syncDoForm(f);
}

async function loadDo() {
  const d = await api('/api/do');
  if (!$('#do0')) buildDoCards(d.channels, d.pins);
  $('#doTime').textContent = d.timeValid ? `${d.time}（星期${WEEKDAYS[d.weekday]}）` : '尚未校時';

  d.channels.forEach((c, i) => {
    const card = $('#do' + i);
    $('.do-title', card).textContent = `${c.name}（GPIO${c.pin}）`;
    const st = $('.do-state', card);
    st.textContent = c.on ? 'ON' : 'OFF';
    st.className = 'badge do-state ' + (c.on ? 'ok' : '');

    // 控制區依「已儲存」的模式顯示
    const tog = $('.do-toggle', card), pulse = $('.do-pulse', card), note = $('.do-note', card);
    tog.classList.toggle('hidden', c.mode !== 0);
    pulse.classList.toggle('hidden', c.mode !== 2);
    tog.dataset.on = c.on ? '1' : '0';
    tog.textContent = c.on ? '關閉' : '開啟';
    tog.classList.toggle('on', c.on);
    if (c.mode === 0) note.textContent = '開關模式：按按鈕切換 ON / OFF';
    else if (c.mode === 2) note.textContent = c.pulseLeftMs > 0
      ? `輸出中，剩 ${(c.pulseLeftMs / 1000).toFixed(1)} 秒`
      : `點動模式：觸發後 ON ${(c.pulseMs / 1000).toFixed(1)} 秒`;
    else {
      const n = c.sched.filter(s => s.en).length;
      note.textContent = !d.timeValid ? '定時模式：裝置尚未校時，排程暫停' : `定時模式：${n} 組排程啟用中`;
    }

    if (!doLoaded[i]) {
      fillDoForm($('.do-form', card), c);
      doLoaded[i] = true;
    }
  });
}

async function doAction(path, form) {
  try {
    toast((await api(path, { form })).msg);
    loadDo();
  } catch (e) { toast(e.message, true); }
}

async function saveDo(e) {
  e.preventDefault();
  const f = e.target, ch = +f.dataset.ch;
  const body = {
    ch, name: f.name.value.trim(), pin: f.pin.value, activeLow: f.activeLow.value,
    mode: f.querySelector('[name=mode]:checked').value, pulseSec: f.pulseSec.value,
  };
  for (let n = 0; n < 4; n++) {
    let days = 0;
    for (let k = 0; k < 7; k++) if (f[`s${n}_d${k}`].checked) days |= 1 << k;
    body[`s${n}_en`] = f[`s${n}_en`].checked ? '1' : '0';
    body[`s${n}_on`] = f[`s${n}_on`].value;
    body[`s${n}_off`] = f[`s${n}_off`].value;
    body[`s${n}_days`] = days;
  }
  try {
    toast((await api('/api/do/config', { form: body })).msg);
    doLoaded[ch] = false;
    loadDo();
  } catch (err) { toast(err.message, true); }
}

function syncTime(force) {
  return api('/api/time', { form: { epoch: (Date.now() / 1000).toFixed(0), force: force ? '1' : '0' } });
}

// ---------------------------------------------------------------------------
// OLED 顯示（遙控器）
async function loadOled() {
  const d = await api('/api/display');
  const f = $('#oledForm');
  f.querySelector(`[name=mode][value="${d.mode}"]`).checked = true;
  f.rotateSec.value = d.rotateSec;
  syncOledForm();
}

function syncOledForm() {
  const f = $('#oledForm');
  f.rotateSec.disabled = f.querySelector('[name=mode]:checked')?.value !== '2';
}

// ---------------------------------------------------------------------------
// OTA
function uploadOta(e) {
  e.preventDefault();
  const form = e.target, file = form.file.files[0];
  if (!file) return;
  const type = form.querySelector('[name=type]:checked').value;
  if (type === 'fs' && info.fsPart && file.size !== info.fsPart) {
    $('#otaState').textContent = `映像大小 ${file.size} bytes 與本裝置 SPIFFS 分區 ${info.fsPart} bytes 不符，` +
      `請用「${spiffsCmd()}」重新產生。`;
    return;
  }
  const btn = form.querySelector('button'), bar = $('#otaBar'), st = $('#otaState');
  const fd = new FormData();
  fd.append('file', file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', `/api/ota?type=${type}&size=${file.size}`);
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
      st.textContent = r.msg;
      waitReboot(type === 'fs' ? '網頁檔已更新，等待裝置重新上線…' : '韌體已更新，等待裝置以新版本啟動…');
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

function fsHex() {
  return '0x' + (info.fsPart || 0).toString(16).toUpperCase();
}

// 產生本裝置 SPIFFS 映像的 spiffs.bat 指令
function spiffsCmd() {
  const presets = { 0x160000: '', 0x20000: ' min', 0xA0000: ' minimal', 0x1E0000: ' noota', 0xE0000: ' huge' };
  const dev = info.device === 'RC' ? 'rc' : 'fc';
  const size = info.fsPart in presets ? presets[info.fsPart] : ' ' + fsHex();
  return `spiffs.bat ${dev}${size}`;
}

// 顯示「重新啟動中」並計時，裝置重新回應後自動重新載入
function waitReboot(msg = '等待裝置重新上線…') {
  clearTimeout(pollTimer);
  pollTimer = null;
  $('#rebootTitle').textContent = '裝置重新啟動中';
  $('#rebootMsg').textContent = msg;
  $('#rebootHint').classList.add('hidden');
  $('#rebootSpinner').classList.remove('done');
  $('#rebootOverlay').classList.remove('hidden');

  const t0 = Date.now();
  const secs = () => Math.floor((Date.now() - t0) / 1000);
  const tick = setInterval(() => {
    $('#rebootSecs').textContent = secs();
    if (secs() >= 60) $('#rebootHint').classList.remove('hidden');
  }, 200);

  // 裝置約 1 秒後才真正重啟，3 秒後再開始確認，避免誤判為已上線
  const check = async () => {
    try {
      await api('/api/info', { timeout: 2500 });
      clearInterval(tick);
      $('#rebootSecs').textContent = secs();
      $('#rebootTitle').textContent = '已重新上線';
      $('#rebootMsg').textContent = `重新啟動共 ${secs()} 秒，重新載入頁面…`;
      $('#rebootHint').classList.add('hidden');
      $('#rebootSpinner').classList.add('done');
      setTimeout(() => location.reload(), 1000);
    } catch (e) {
      setTimeout(check, 1000);
    }
  };
  setTimeout(check, 3000);
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
      await api('/api/reboot', { method: 'POST' });
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
      await api('/api/eth', { form: { dhcp, ip: f.ip.value, gw: f.gw.value, mask: f.mask.value, dns: f.dns.value } });
      waitReboot(dhcp === '0'
        ? `RJ45 設定已儲存。若你是透過 RJ45 連線，重新啟動後請改連 http://${f.ip.value}/`
        : 'RJ45 設定已儲存（DHCP），等待裝置重新上線…');
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

  // 搖桿校正
  $('#stickStartBtn').onclick = () => stickAction('/api/sticks/start');
  $('#stickFinishBtn').onclick = () => stickAction('/api/sticks/finish');
  $('#stickCancelBtn').onclick = () => stickAction('/api/sticks/cancel');
  $('#stickResetBtn').onclick = () =>
    stickAction('/api/sticks/reset', '恢復預設值會清除校正資料，並以目前位置作為中點。請先放開搖桿，確定？');
  $('#stickForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target, body = { deadband: f.deadband.value };
    for (let i = 0; i < 4; i++) body['inv' + i] = f['inv' + i].checked ? '1' : '0';
    try { toast((await api('/api/sticks/options', { form: body })).msg); } catch (err) { toast(err.message, true); }
  };

  // DO 時間同步
  $('#timeSyncBtn').onclick = async () => {
    try { toast((await syncTime(true)).msg); loadDo(); } catch (e) { toast(e.message, true); }
  };

  // OLED 顯示
  $$('#oledForm [name=mode]').forEach(r => (r.onchange = syncOledForm));
  $('#oledForm').onsubmit = async e => {
    e.preventDefault();
    const f = e.target;
    const body = { mode: f.querySelector('[name=mode]:checked').value, rotateSec: f.rotateSec.value };
    try { toast((await api('/api/display', { form: body })).msg); } catch (err) { toast(err.message, true); }
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
  $('#userClearBtn').onclick = async () => {
    if (!confirm('清除帳密後，任何人都能開啟網頁，確定？')) return;
    try {
      toast((await api('/api/user/clear', { method: 'POST' })).msg);
      info = await api('/api/info');
      $('#userForm').reset();
      renderAuth();
    } catch (e) { toast(e.message, true); }
  };

  // 主題
  $('#themeBtn').onclick = () => {
    const cur = document.documentElement.getAttribute('data-theme');
    setTheme(THEMES[(THEMES.indexOf(cur) + 1) % THEMES.length]);
  };
}

async function init() {
  setTheme(document.documentElement.getAttribute('data-theme'), false);
  bind();
  try {
    info = await api('/api/info');
  } catch (e) {
    toast('無法讀取裝置資訊：' + e.message, true);
  }
  document.title = info.name || '控制面板';
  $('#brandName').textContent = info.name || '—';
  $('#brandFw').textContent = (info.fw ? `韌體 v${info.fw}｜` : '') + `網頁 v${WEB_VERSION}`;
  renderAuth();
  $$('[data-feature]').forEach(el => el.classList.toggle('off', !info.features[el.dataset.feature]));
  // 遙控器的定時排程需要時間：裝置尚未校時就用瀏覽器時間同步
  if (info.features.do) syncTime(false).catch(() => {});
  route();
}

init();
