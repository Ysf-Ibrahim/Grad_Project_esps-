/* AcoustiScan dashboard logic: node list, capture jobs, results, charts, history. */
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var NS = 'http://www.w3.org/2000/svg';
  var state = { nodes: [], selected: null, job: null, capture: null, history: [] };

  function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }
  function el(tag, attrs, parent) { var e = document.createElementNS(NS, tag); for (var k in attrs) e.setAttribute(k, attrs[k]); if (parent) parent.appendChild(e); return e; }
  function h(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
  function api(path, opts) {
    return fetch(path, opts).then(function (r) { return r.json().then(function (j) { if (!r.ok) throw new Error(j.error || r.statusText); return j; }); });
  }
  function fmt(n, d) { return (n === null || n === undefined || isNaN(n)) ? '–' : Number(n).toFixed(d === undefined ? 1 : d); }
  function ago(s) { if (s < 60) return Math.round(s) + ' s ago'; if (s < 3600) return Math.round(s / 60) + ' min ago'; return Math.round(s / 3600) + ' h ago'; }

  // ------------------------------------------------------------ header --
  function refreshInfo() {
    api('/api/info').then(function (i) { $('info-ip').textContent = (i.local_ips && i.local_ips.length) ? i.local_ips.join(', ') : 'no network'; }).catch(function () {});
  }
  function tickClock() { $('info-clock').textContent = new Date().toLocaleString(); }

  // -------------------------------------------------------------- nodes --
  function refreshNodes() {
    api('/api/nodes').then(function (nodes) {
      state.nodes = nodes;
      renderNodes();
      var online = nodes.filter(function (n) { return n.online; }).length;
      $('info-online').textContent = online;
      $('info-total').textContent = nodes.length;
      if (state.selected) {
        var cur = nodes.filter(function (n) { return n.id === state.selected; })[0];
        if (cur) updateSelectedHeader(cur);
      }
    }).catch(function () {});
  }

  function renderNodes() {
    var list = $('node-list');
    list.innerHTML = '';
    if (!state.nodes.length) {
      list.appendChild(h('div', 'empty', 'No nodes found yet. Power a provisioned node on the same network; it appears here within a few seconds.'));
      return;
    }
    state.nodes.forEach(function (n) {
      var row = h('div', 'node' + (n.id === state.selected ? ' selected' : ''));
      row.setAttribute('role', 'button'); row.tabIndex = 0;
      var left = h('div');
      left.appendChild(h('span', 'id', n.id));
      if (n.name) left.appendChild(h('span', 'name', n.name));
      row.appendChild(left);
      var st = h('span', 'status');
      var dot = h('span', 'dot' + (n.online ? (n.mic === 'flat' ? ' bad' : ' on') : ''));
      st.appendChild(dot);
      st.appendChild(document.createTextNode(n.online ? (n.state === 'idle' ? 'Online' : 'Busy: ' + n.state) : 'Offline, ' + ago(n.last_seen_ago)));
      row.appendChild(st);
      var meta = h('div', 'meta');
      meta.appendChild(h('span', 'num', n.ip));
      if (n.online) meta.appendChild(h('span', 'num', n.rssi + ' dBm'));
      var mic = h('span', n.mic === 'flat' ? 'bad' : '', 'mic ' + (n.mic || '?'));
      meta.appendChild(mic);
      if (n.provisioned === 0) meta.appendChild(h('span', 'warn', 'unprovisioned'));
      row.appendChild(meta);
      row.addEventListener('click', function () { selectNode(n.id); });
      row.addEventListener('keydown', function (ev) { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); selectNode(n.id); } });
      list.appendChild(row);
    });
  }

  function selectNode(id) {
    state.selected = id;
    renderNodes();
    var n = state.nodes.filter(function (x) { return x.id === id; })[0];
    if (n) updateSelectedHeader(n);
    loadHistory();
  }

  function updateSelectedHeader(n) {
    $('sel-title').textContent = n.id + (n.name ? ' · ' + n.name : '');
    var parts = [n.online ? 'Online' : 'Offline', n.ip];
    if (n.online) parts.push(n.rssi + ' dBm', 'mic ' + n.mic, 'fw ' + n.fw);
    $('sel-status').textContent = parts.join('  ·  ');
    var busy = state.job && state.job.phase && ['queued', 'connecting', 'recording', 'transferring', 'analyzing'].indexOf(state.job.phase) >= 0;
    $('start').disabled = !n.online || busy || n.state !== 'idle';
  }

  // ------------------------------------------------------------ capture --
  function startCapture() {
    if (!state.selected) return;
    var dur = document.querySelector('input[name="dur"]:checked').value;
    var body = { node_id: state.selected, seconds: Number(dur), label: $('label').value };
    $('start').disabled = true;
    setProgress(0, 'Starting…', false);
    api('/api/capture', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(function (j) { state.job = { id: j.job_id, phase: 'queued' }; pollJob(); })
      .catch(function (e) { setProgress(0, 'Could not start: ' + e.message, true); state.job = null; refreshNodes(); });
  }

  function pollJob() {
    if (!state.job) return;
    api('/api/jobs/' + state.job.id).then(function (job) {
      state.job = job;
      var pct = job.percent || 0;
      if (job.phase === 'recording') setProgress(pct * 0.7, job.message, false);
      else if (job.phase === 'transferring') setProgress(70 + pct * 0.25, job.message, false);
      else if (job.phase === 'analyzing') setProgress(97, job.message, false);
      else if (job.phase === 'done') {
        setProgress(100, job.message, false);
        state.job = null;
        loadCapture(job.capture_id);
        loadHistory();
        refreshNodes();
        return;
      } else if (job.phase === 'error') {
        setProgress(0, 'Failed: ' + job.message, true);
        state.job = null;
        refreshNodes();
        return;
      } else setProgress(2, job.message || job.phase, false);
      setTimeout(pollJob, 400);
    }).catch(function (e) { setProgress(0, 'Lost contact with the server: ' + e.message, true); state.job = null; });
  }

  function setProgress(pct, text, bad) {
    $('bar-fill').style.width = Math.max(0, Math.min(100, pct)) + '%';
    var t = $('progress-text');
    t.textContent = text;
    t.className = 'muted' + (bad ? ' bad' : '');
  }

  // ------------------------------------------------------------ results --
  function loadCapture(id) {
    api('/api/captures/' + id).then(function (cap) {
      state.capture = cap;
      $('results').hidden = false;
      $('result-meta').textContent = cap.node.id + ' · ' + cap.time + (cap.label ? ' · ' + cap.label : '');
      renderTiles(cap.stats, cap.node_stats || {});
      drawWaveform(cap.charts.waveform);
      drawSpectrum(cap.charts.spectrum, cap.stats);
      drawSpectrogram(cap.charts.spectrogram);
      markCurrentRow(id);
    }).catch(function (e) { setProgress(0, 'Could not load the capture: ' + e.message, true); });
  }

  function tile(k, v, unit, sub, subClass) {
    var t = h('div', 'tile');
    t.appendChild(h('div', 'k', k));
    var val = h('div', 'v', v);
    if (unit) { var u = h('small', '', unit); val.appendChild(u); }
    t.appendChild(val);
    if (sub !== undefined) t.appendChild(h('div', 's' + (subClass ? ' ' + subClass : ''), sub));
    return t;
  }

  function renderTiles(s, ns) {
    var box = $('tiles');
    box.innerHTML = '';
    box.appendChild(tile('Peak level', fmt(s.peak_dbfs), 'dBFS', s.peak + ' of 32768'));
    box.appendChild(tile('RMS level', fmt(s.rms_ac_dbfs), 'dBFS', fmt(s.rms_ac, 0) + ' LSB, DC removed'));
    box.appendChild(tile('Noise floor', fmt(s.noise_floor_dbfs), 'dBFS', 'median spectrum bin'));
    box.appendChild(tile('Dominant frequency', fmt(s.dominant_hz), 'Hz', fmt(s.dominant_dbfs) + ' dBFS'));
    box.appendChild(tile('Crest factor', fmt(s.crest_db), 'dB', 'peak to RMS'));
    box.appendChild(tile('DC offset', (s.mean >= 0 ? '+' : '') + fmt(s.mean), 'LSB', Math.abs(s.mean) > 8000 ? 'unusually large' : 'normal', Math.abs(s.mean) > 8000 ? 'warn' : ''));
    box.appendChild(tile('Clipping', String(s.clip), 'samples', s.clip > 0 ? 'overload, too loud or too close' : 'no overload', s.clip > 0 ? 'warn' : 'ok'));
    var alive = !s.constant;
    box.appendChild(tile('Mic health', alive ? 'Alive' : 'Flat', '', alive ? ('data line ' + (ns.data_high_pct || '?') + ' % high') : 'no signal from the microphone', alive ? 'ok' : 'bad'));
    box.appendChild(tile('Length', fmt(s.seconds, 3), 's', s.samples + ' samples at ' + s.rate + ' Hz' + (s.capture_ms ? ', recorded in ' + s.capture_ms + ' ms' : ''), s.timing_ok ? '' : 'warn'));
    box.appendChild(tile('Voice band', fmt(s.band_voice_dbfs), 'dBFS', '300 to 3400 Hz; low ' + fmt(s.band_low_dbfs) + ', high ' + fmt(s.band_high_dbfs)));
  }

  // ------------------------------------------------------------- charts --
  function hover(wrap, svg, tip, W, L, pw, fn) {
    var cross = el('line', { x1: 0, y1: 0, x2: 0, y2: 0, stroke: css('--ink'), 'stroke-width': 1, 'stroke-dasharray': '3 3', opacity: 0 }, svg);
    wrap.onmousemove = function (ev) {
      var r = svg.getBoundingClientRect(), fx = (ev.clientX - r.left) / r.width * W;
      if (fx < L || fx > L + pw) { cross.setAttribute('opacity', 0); tip.classList.remove('show'); return; }
      var info = fn((fx - L) / pw);
      cross.setAttribute('x1', fx); cross.setAttribute('x2', fx); cross.setAttribute('y1', info.y0); cross.setAttribute('y2', info.y1); cross.setAttribute('opacity', 1);
      tip.textContent = info.text;
      tip.style.left = (ev.clientX - r.left) + 'px'; tip.style.top = (r.top - wrap.getBoundingClientRect().top + 22) + 'px';
      tip.classList.add('show');
    };
    wrap.onmouseleave = function () { cross.setAttribute('opacity', 0); tip.classList.remove('show'); };
  }

  function drawWaveform(env) {
    var svg = $('wave'); svg.innerHTML = '';
    var W = 1000, H = 230, L = 46, R = 12, T = 12, B = 30, pw = W - L - R, ph = H - T - B, cy = T + ph / 2;
    var g = el('g', {}, svg);
    var secs = env.seconds, step = secs > 12 ? 5 : 1;
    for (var s = 0; s <= secs + 1e-6; s += step) {
      var x = L + s / secs * pw;
      el('line', { x1: x, y1: T, x2: x, y2: T + ph, 'class': 'grid-line' }, g);
      el('text', { x: x, y: H - 10, 'class': 'axis-text', 'text-anchor': 'middle' }, g).textContent = s + ' s';
    }
    [[1, '1.0'], [0.5, '0.5'], [0, '0'], [-0.5, '-0.5'], [-1, '-1.0']].forEach(function (p) {
      var y = cy - p[0] * ph / 2;
      el('line', { x1: L, y1: y, x2: L + pw, y2: y, 'class': p[0] === 0 ? 'axis-line' : 'grid-line' }, g);
      el('text', { x: L - 6, y: y + 3, 'class': 'axis-text', 'text-anchor': 'end' }, g).textContent = p[1];
    });
    var n = env.columns, d = '';
    for (var i = 0; i < n; i++) d += (i ? 'L' : 'M') + (L + i / n * pw).toFixed(1) + ',' + (cy - env.max[i] * ph / 2).toFixed(1);
    for (i = n - 1; i >= 0; i--) d += 'L' + (L + i / n * pw).toFixed(1) + ',' + (cy - env.min[i] * ph / 2).toFixed(1);
    el('path', { d: d + 'Z', fill: css('--series'), 'fill-opacity': 0.85, stroke: css('--series'), 'stroke-width': 1 }, g);
    hover($('wave-wrap'), svg, $('wave-tip'), W, L, pw, function (u) {
      var i = Math.min(n - 1, Math.floor(u * n));
      var pk = Math.max(Math.abs(env.max[i]), Math.abs(env.min[i]));
      return { y0: T, y1: T + ph, text: (u * secs).toFixed(2) + ' s   peak ' + (20 * Math.log10(Math.max(pk, 1e-5))).toFixed(1) + ' dBFS' };
    });
  }

  function drawSpectrum(spec, stats) {
    var svg = $('spec'); svg.innerHTML = '';
    var W = 640, H = 260, L = 44, R = 14, T = 12, B = 30, pw = W - L - R, ph = H - T - B;
    var fmax = spec.freqs[spec.freqs.length - 1];
    var g = el('g', {}, svg);
    function xf(f) { return L + f / fmax * pw; }
    function yd(d) { return T + (0 - Math.max(-100, Math.min(0, d))) / 100 * ph; }
    for (var f = 0; f <= fmax; f += 1000) {
      el('line', { x1: xf(f), y1: T, x2: xf(f), y2: T + ph, 'class': 'grid-line' }, g);
      el('text', { x: xf(f), y: H - 10, 'class': 'axis-text', 'text-anchor': 'middle' }, g).textContent = f ? (f / 1000) + 'k' : '0 Hz';
    }
    for (var d = 0; d >= -100; d -= 20) {
      el('line', { x1: L, y1: yd(d), x2: L + pw, y2: yd(d), 'class': d === -100 ? 'axis-line' : 'grid-line' }, g);
      el('text', { x: L - 6, y: yd(d) + 3, 'class': 'axis-text', 'text-anchor': 'end' }, g).textContent = d;
    }
    var path = '', area = 'M' + L + ',' + (T + ph);
    for (var i = 0; i < spec.freqs.length; i++) {
      var x = xf(spec.freqs[i]).toFixed(1), y = yd(spec.db[i]).toFixed(1);
      path += (i ? 'L' : 'M') + x + ',' + y; area += 'L' + x + ',' + y;
    }
    area += 'L' + (L + pw) + ',' + (T + ph) + 'Z';
    el('path', { d: area, fill: css('--series-fill') }, g);
    el('path', { d: path, fill: 'none', stroke: css('--series'), 'stroke-width': 1.4, 'stroke-linejoin': 'round' }, g);
    if (stats && stats.dominant_hz) {
      var px = xf(stats.dominant_hz), py = yd(stats.dominant_dbfs);
      el('circle', { cx: px, cy: py, r: 4, fill: css('--series'), stroke: '#fff', 'stroke-width': 2 }, g);
      var anchor = px > L + pw * 0.75 ? 'end' : 'start';
      el('text', { x: px + (anchor === 'end' ? -8 : 8), y: Math.max(T + 10, py - 6), 'class': 'axis-text', fill: css('--ink'), 'font-size': 11, 'font-weight': 600, 'text-anchor': anchor }, g)
        .textContent = fmt(stats.dominant_hz) + ' Hz, ' + fmt(stats.dominant_dbfs) + ' dBFS';
    }
    hover($('spec-wrap'), svg, $('spec-tip'), W, L, pw, function (u) {
      var i = Math.min(spec.freqs.length - 1, Math.round(u * (spec.freqs.length - 1)));
      return { y0: T, y1: T + ph, text: fmt(spec.freqs[i], 0) + ' Hz   ' + fmt(spec.db[i]) + ' dBFS' };
    });
  }

  function heat(u) {
    // white -> light blue -> series blue -> near black (louder = darker) on a white page
    var stops = [[0, [255, 255, 255]], [0.35, [198, 216, 240]], [0.7, [47, 95, 158]], [1, [12, 24, 45]]];
    u = Math.max(0, Math.min(1, u));
    for (var i = 0; i < stops.length - 1; i++) {
      var a = stops[i], b = stops[i + 1];
      if (u <= b[0]) { var t = (u - a[0]) / (b[0] - a[0]); return 'rgb(' + Math.round(a[1][0] + (b[1][0] - a[1][0]) * t) + ',' + Math.round(a[1][1] + (b[1][1] - a[1][1]) * t) + ',' + Math.round(a[1][2] + (b[1][2] - a[1][2]) * t) + ')'; }
    }
    return 'rgb(12,24,45)';
  }

  function drawSpectrogram(sg) {
    var c = $('sgram'), ctx = c.getContext('2d');
    var cols = sg.cols, rows = sg.rows;
    c.width = 640; c.height = 256;
    var cw = c.width / cols, rh = c.height / rows;
    for (var r = 0; r < rows; r++) {
      for (var i = 0; i < cols; i++) {
        var v = (sg.data[r][i] - sg.db_min) / (sg.db_max - sg.db_min);
        ctx.fillStyle = heat(v);
        ctx.fillRect(i * cw, r * rh, cw + 0.5, rh + 0.5);
      }
    }
    var ax = $('sgram-axis'); ax.innerHTML = '';
    var secs = sg.seconds, step = secs > 12 ? 5 : (secs > 3 ? 2 : 0.5);
    for (var s = 0; s <= secs + 1e-6; s += step) ax.appendChild(h('span', '', (Math.round(s * 10) / 10) + ' s'));
  }

  // ------------------------------------------------------------ history --
  function loadHistory() {
    if (!state.selected) return;
    $('hist-node').textContent = '· ' + state.selected;
    api('/api/captures?node=' + encodeURIComponent(state.selected)).then(function (rows) {
      state.history = rows;
      var tb = $('history').querySelector('tbody');
      tb.innerHTML = '';
      $('hist-empty').hidden = rows.length > 0;
      rows.forEach(function (r) {
        var tr = h('tr', state.capture && state.capture.id === r.id ? 'current' : '');
        tr.dataset.id = r.id;
        tr.appendChild(h('td', 'num', r.time || ''));
        tr.appendChild(h('td', '', r.label || ''));
        tr.appendChild(h('td', 'num', fmt(r.seconds, 1) + ' s'));
        tr.appendChild(h('td', 'num', fmt(r.peak_dbfs) + ' dBFS'));
        tr.appendChild(h('td', 'num', fmt(r.rms_dbfs) + ' dBFS'));
        tr.appendChild(h('td', 'num', fmt(r.dominant_hz) + ' Hz'));
        var res = h('td');
        var cls = r.result === 'OK' ? 'res-ok' : (r.result === 'Mic flat' ? 'res-bad' : 'res-warn');
        res.appendChild(h('span', cls, r.result || ''));
        tr.appendChild(res);
        var act = h('td');
        var open = h('a', '', 'Open'); open.href = '#'; open.onclick = function (ev) { ev.preventDefault(); loadCapture(r.id); };
        var wav = h('a', '', 'WAV'); wav.href = '/api/captures/' + r.id + '/wav';
        act.appendChild(open); act.appendChild(wav);
        tr.appendChild(act);
        tb.appendChild(tr);
      });
    }).catch(function () {});
  }

  function markCurrentRow(id) {
    var rows = $('history').querySelectorAll('tbody tr');
    for (var i = 0; i < rows.length; i++) rows[i].className = rows[i].dataset.id === id ? 'current' : '';
  }

  // -------------------------------------------------------------- setup --
  $('start').addEventListener('click', startCapture);
  $('add-form').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var ip = $('add-ip').value.trim(), msg = $('add-msg');
    if (!ip) return;
    msg.className = 'msg'; msg.textContent = 'Asking ' + ip + ' …';
    api('/api/nodes/add', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ip: ip }) })
      .then(function (hb) { msg.textContent = 'Added ' + hb.id; $('add-ip').value = ''; refreshNodes(); })
      .catch(function (e) { msg.className = 'msg bad'; msg.textContent = e.message; });
  });
  refreshInfo(); refreshNodes(); tickClock();
  setInterval(refreshNodes, 2000);
  setInterval(refreshInfo, 10000);
  setInterval(tickClock, 1000);
})();
