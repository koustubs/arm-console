(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const average = new ArmCore.MovingAverage(5);
  let port = null, reader = null, readerDone = null, writer = null, demoTimer = null, source = '', firmware = false;
  let rows = [], points = [], lastReading = 0, legacyGrip = null, run = 0, demoTick = 0, jobRunning = false;
  const maxRows = 20000, maxPoints = 150;
  function controls() {
    const connected = !!port || !!demoTimer;
    $('connect').disabled = connected || jobRunning; $('reconnect').disabled = connected || jobRunning;
    $('demo').disabled = connected || jobRunning; $('disconnect').disabled = !connected;
    $('start').disabled = !port || !firmware; $('stop').disabled = !port || !firmware;
    for (const id of ['setup', 'refreshPorts', 'upload', 'uploadPort', 'uploadSketch']) $(id).disabled = jobRunning;
  }
  function log(message) {
    const el = document.createElement('div'); el.textContent = `${new Date().toLocaleTimeString()}  ${message}`;
    if ($('events').textContent === 'No records yet.') $('events').replaceChildren();
    $('events').prepend(el); while ($('events').children.length > 150) $('events').lastChild.remove();
  }
  function addRow(row) { rows.push(row); if (rows.length > maxRows) rows.shift(); }
  function resetReadings() {
    average.values = []; points = []; lastReading = 0; legacyGrip = null; firmware = false;
    for (const id of ['raw', 'filtered', 'grip', 'state']) $(id).textContent = '—';
    $('pose').textContent = 'Base — · Shoulder — · Elbow — · Gripper —'; draw();
  }
  function accept(line) {
    const record = ArmCore.parseLine(line); if (!record) return;
    const row = { received_at: new Date().toISOString(), source, kind: record.type, raw_line: line };
    if (record.type === 'hello') { firmware = true; controls(); log('Arm Controller firmware detected.'); }
    if (record.type === 'sample' || record.type === 'legacyDistance') {
      if (record.type === 'sample') { firmware = true; controls(); }
      lastReading = Date.now(); const filtered = average.add(record.distance);
      $('raw').textContent = record.distance === null ? 'No echo' : `${record.distance.toFixed(1)} cm`;
      $('filtered').textContent = filtered === null ? '—' : `${filtered.toFixed(1)} cm`;
      $('banner').textContent = `${source === 'demo' ? 'SIMULATED DATA · ' : ''}${record.state === 'FAULT' ? 'Controller is in fault state. Inspect the arm before restarting.' : record.distance === null ? 'Invalid / absent echo; filter reset.' : 'Receiving distance readings.'}`;
      if (record.type === 'sample') {
        $('state').textContent = record.state.replaceAll('_', ' ');
        $('grip').textContent = record.grip ? 'Active' : 'Inactive';
        $('pose').textContent = `Base ${record.base}° · Shoulder ${record.shoulder}° · Elbow ${record.elbow}° · Gripper ${record.gripper}°`;
        Object.assign(row, { device_ms: record.deviceMs, sequence: record.seq, echo_us: record.echoUs, grip_input: record.grip, state: record.state, base_command_deg: record.base, shoulder_command_deg: record.shoulder, elbow_command_deg: record.elbow, gripper_command_deg: record.gripper });
      } else row.grip_input = legacyGrip;
      row.distance_cm = record.distance; row.filtered_cm = filtered === null ? null : filtered.toFixed(2);
      points.push({ raw: record.distance, filtered }); if (points.length > maxPoints) points.shift(); draw();
    } else if (record.type === 'legacyGrip') { legacyGrip = record.grip; $('grip').textContent = record.grip ? 'Active' : 'Inactive'; row.grip_input = record.grip; }
    else if (record.type !== 'hello') {
      row.message = record.message || record.raw; row.device_ms = record.deviceMs;
      log(`${record.type.toUpperCase()} · ${row.message}`);
      if (record.type === 'event') $('state').textContent = record.message.replaceAll('_', ' ');
      if (record.type === 'fault') $('banner').textContent = `Controller fault: ${record.message}. Motion has stopped; inspect the arm before restarting.`;
    }
    addRow(row);
  }
  function draw() {
    const canvas = $('chart'), ctx = canvas.getContext('2d'), w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h); ctx.strokeStyle = '#2b403c'; ctx.lineWidth = 1;
    for (let i = 0; i < 4; i++) { const y = 20 + i * (h - 40) / 3; ctx.beginPath(); ctx.moveTo(40, y); ctx.lineTo(w - 10, y); ctx.stroke(); }
    const valid = points.flatMap(p => [p.raw, p.filtered]).filter(v => v !== null && Number.isFinite(v));
    const max = Math.max(20, ...valid); ctx.fillStyle = '#829c94'; ctx.font = '12px sans-serif';
    ctx.fillText(`${max.toFixed(0)} cm`, 0, 16); ctx.fillText('0', 20, h - 15);
    for (const [key, color] of [['raw', '#6caaa0'], ['filtered', '#b6f1c4']]) {
      ctx.beginPath(); let active = false; ctx.strokeStyle = color; ctx.lineWidth = key === 'raw' ? 1.5 : 3;
      points.forEach((p, i) => { const v = p[key]; if (v === null) { active = false; return; }
        const x = 40 + i * (w - 50) / Math.max(maxPoints - 1, points.length - 1), y = h - 20 - v / max * (h - 40);
        if (!active) ctx.moveTo(x, y); else ctx.lineTo(x, y); active = true;
      }); ctx.stroke();
    }
  }
  async function disconnect() {
    run++; lastReading = 0; if (demoTimer) { clearInterval(demoTimer); demoTimer = null; }
    const oldReader = reader, oldDone = readerDone, oldWriter = writer, oldPort = port; reader = writer = port = null; readerDone = null;
    try { if (oldReader) await oldReader.cancel(); } catch (_) {}
    try { if (oldDone) await oldDone; } catch (_) {}
    try { if (oldWriter) oldWriter.releaseLock(); } catch (_) {}
    try { if (oldPort) await oldPort.close(); } catch (_) {}
    $('status').textContent = 'Disconnected'; $('banner').textContent = 'Disconnected. Readings shown are from the previous session.'; controls();
  }
  async function connect(chosen) {
    if (!('serial' in navigator)) { log('Web Serial is unavailable. Use Chrome or Edge on localhost.'); return; }
    try {
      let next;
      if (chosen) next = await navigator.serial.requestPort();
      else { const available = await navigator.serial.getPorts(); if (available.length !== 1) { log(available.length ? 'More than one permitted device: use Connect Arduino to choose.' : 'No permitted device yet. Use Connect Arduino first.'); return; } next = available[0]; }
      await next.open({ baudRate: 9600 }); port = next; source = 'live'; resetReadings();
      $('status').textContent = 'Connected · live'; $('banner').textContent = 'Connected. Waiting for serial data…'; controls(); log('Serial port opened at 9600 baud.');
      writer = next.writable.getWriter(); const token = ++run;
      const lines = new ArmCore.LineBuffer(accept), decoder = new TextDecoder();
      reader = next.readable.getReader(); const currentReader = reader;
      let released; readerDone = new Promise(resolve => { released = resolve; });
      try {
        while (token === run) { const { value, done } = await currentReader.read(); if (done) break; lines.push(decoder.decode(value, { stream: true })); }
      } finally { currentReader.releaseLock(); released(); if (reader === currentReader) reader = null; }
      if (token === run) { log('Device disconnected or serial stream ended.'); await disconnect(); }
    } catch (err) { if (err.name !== 'NotFoundError') log(`Connection error: ${err.message}`); if (port) await disconnect(); }
  }
  async function send(command) {
    if (!writer || !firmware) return;
    try { await writer.write(new TextEncoder().encode(command + '\n')); log(`Sent ${command}; waiting for controller acknowledgement.`); }
    catch (err) { log(`Could not send ${command}: ${err.message}`); }
  }
  $('connect').onclick = () => connect(true); $('reconnect').onclick = () => connect(false);
  $('disconnect').onclick = disconnect; $('start').onclick = () => send('START'); $('stop').onclick = () => send('STOP');
  $('demo').onclick = () => {
    resetReadings(); source = 'demo'; demoTick = 0; $('status').textContent = 'Demo · simulated';
    demoTimer = setInterval(() => { demoTick++; const value = demoTick % 25 === 0 ? -1 : 12 + 5 * Math.sin(demoTick / 10); const echo = value < 0 ? 0 : Math.round(value / .01715);
      accept(`S,${demoTick},${demoTick * 150},${echo},${value.toFixed(1)},0,SCAN,${80 + (demoTick % 40)},110,130,30`);
    }, 150); controls(); log('Demo started: generated data is not a measurement.');
  };
  $('export').onclick = () => {
    const blob = new Blob([ArmCore.csv(rows)], { type: 'text/csv;charset=utf-8' }); const url = URL.createObjectURL(blob);
    const a = document.createElement('a'); a.href = url; a.download = `arm-session-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  $('clear').onclick = () => { rows = []; points = []; average.values = []; $('events').replaceChildren(); log('Session log cleared.'); draw(); };
  async function api(path, options) {
    const response = await fetch(path, { cache: 'no-store', ...options });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `Server error ${response.status}`);
    return result;
  }
  $('refreshPorts').onclick = async () => {
    try {
      $('uploadStatus').textContent = 'Looking for serial ports…';
      const result = await api('/api/ports');
      $('uploadPort').replaceChildren();
      const first = document.createElement('option'); first.value = ''; first.textContent = 'Select your Uno port'; $('uploadPort').append(first);
      for (const item of result.ports) {
        const option = document.createElement('option'); option.value = item.address;
        option.textContent = `${item.label}${item.boards.length ? ' · ' + item.boards.join(', ') : ' · unidentified device'}`;
        $('uploadPort').append(option);
      }
      $('uploadStatus').textContent = result.ports.length ? 'Select the port belonging to your Uno. Unidentified ports are not verified.' : 'No serial ports found. Check the USB cable and connection.';
    } catch (error) { $('uploadStatus').textContent = error.message; }
  };
  async function startJob(path, body) {
    jobRunning = true; controls(); $('uploadOutput').hidden = true;
    try {
      const session = await api('/api/session');
      await api(path, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Arm-Token': session.token }, body: JSON.stringify(body) });
      $('uploadStatus').textContent = path === '/api/setup' ? 'Installing build tools; this can take a few minutes…' : 'Compiling and uploading; keep the Arduino connected…';
      let result;
      do { await new Promise(resolve => setTimeout(resolve, 1000)); result = await api('/api/job'); } while (result.state === 'running');
      $('uploadStatus').textContent = result.state === 'success' ? (path === '/api/setup' ? 'Build tools installed.' : 'Upload and verification completed. Connect the Arduino to monitor it.') : 'Operation failed. See details below.';
      $('uploadOutput').textContent = result.output || ''; $('uploadOutput').hidden = !result.output;
    } catch (error) { $('uploadStatus').textContent = `Operation error: ${error.message}. Check the local server and job status before retrying.`; }
    finally { jobRunning = false; controls(); }
  }
  $('setup').onclick = () => startJob('/api/setup', {});
  $('upload').onclick = async () => {
    const selected = $('uploadPort').value, sketch = $('uploadSketch').value;
    if (!selected) { $('uploadStatus').textContent = 'Find ports and select your Uno first.'; return; }
    const warning = sketch === 'original' ? 'The original sketch starts moving automatically after reset.' : 'The new sketch waits for Start, but the old sketch can move if the board resets during upload.';
    if (!confirm(`Upload to ${selected}? This replaces the sketch currently on the board. ${warning} Clear the arm and be ready to disconnect motor power.`)) return;
    jobRunning = true; controls();
    if (port || demoTimer) await disconnect();
    await startJob('/api/upload', { port: selected, sketch });
  };
  setInterval(() => { if (lastReading && Date.now() - lastReading > 3000) $('banner').textContent = `${source === 'demo' ? 'SIMULATED DATA · ' : ''}Reading is stale: no new distance sample for over 3 seconds.`; }, 1000);
  if (!('serial' in navigator)) $('banner').textContent = 'Web Serial unavailable here. Use Chrome or Edge on localhost; demo remains available.';
  controls(); draw();
})();
