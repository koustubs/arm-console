// Exercise the real browser controller against a minimal DOM and a fake serial device.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const core = require('../web/core.js');
const elements = {};
function element(id) {
  const el = { id, textContent: id === 'events' ? 'No records yet.' : '', disabled: false, children: [],
    prepend(child) { this.children.unshift(child); }, append(child) { this.children.push(child); },
    replaceChildren() { this.children = []; this.textContent = ''; },
    remove() {}, click() {},
    get lastChild() { return this.children.at(-1); },
    getContext() { return { clearRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, fillText() {} }; },
    width: 1000, height: 280 };
  return el;
}
const document = { getElementById(id) { return elements[id] ||= element(id); }, createElement() { return element('created'); } };
const timers = []; let pendingRead; let closed = false; let failFetch = false; let confirmed = true; const sent = []; const requests = [];
const fakeReader = {
  read() { return new Promise(resolve => { pendingRead = resolve; }); },
  async cancel() { pendingRead({ done: true }); }, releaseLock() {}
};
const fakeWriter = { async write(value) { sent.push(new TextDecoder().decode(value)); }, releaseLock() {} };
const port = { async open(options) { assert.equal(options.baudRate, 9600); },
  readable: { getReader: () => fakeReader }, writable: { getWriter: () => fakeWriter }, async close() { closed = true; } };
const context = { ArmCore: core, document, navigator: { serial: { requestPort: async () => port, getPorts: async () => [port] } },
  TextEncoder, TextDecoder, Blob, URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} },
  confirm: () => confirmed,
  fetch: async (path, options) => { if (failFetch) throw new TypeError('Failed to fetch'); requests.push({ path, options }); return { ok: true, json: async () => path === '/api/session' ? { token: 'test-token' } : path === '/api/job' ? { state: 'success', output: 'verified' } : path === '/api/ports' ? { ports: [{ address: 'COM7', label: 'COM7', boards: ['Arduino Uno'] }, { address: 'COM8', label: 'COM8', boards: [] }] } : { state: 'running' } }; },
  setInterval: fn => { timers.push(fn); return timers.length; }, clearInterval() {}, setTimeout: fn => fn(), Date, console };
vm.runInNewContext(fs.readFileSync(require.resolve('../web/app.js'), 'utf8'), context);
(async () => {
  assert.equal(elements.start.disabled, true);
  elements.demo.onclick(); assert.equal(elements.status.textContent, 'Demo · simulated');
  timers.at(-1)(); assert.match(elements.raw.textContent, /cm/); assert.match(elements.banner.textContent, /SIMULATED/);
  assert.equal(elements.start.disabled, true);
  await elements.disconnect.onclick();
  elements.connect.onclick(); await new Promise(resolve => setImmediate(resolve));
  pendingRead({ done: false, value: new TextEncoder().encode('ARM,1\nS,1,150,580,9.9,0,SCAN,80,110,130,30\n') });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements.raw.textContent, '9.9 cm'); assert.equal(elements.start.disabled, false);
  await elements.start.onclick(); assert.deepEqual(sent, ['START\n']);
  await elements.refreshPorts.onclick();
  assert.equal(elements.uploadPort.children.length, 3);
  assert.match(elements.uploadPort.children[2].textContent, /unidentified device/);
  elements.uploadPort.value = 'COM7'; elements.uploadSketch.value = 'arm';
  confirmed = false; await elements.upload.onclick();
  assert.equal(requests.some(request => request.path === '/api/upload'), false);
  assert.equal(closed, false); confirmed = true;
  await elements.upload.onclick();
  assert.equal(closed, true); assert.equal(elements.start.disabled, true);
  const upload = requests.find(request => request.path === '/api/upload');
  assert.deepEqual(JSON.parse(upload.options.body), { port: 'COM7', sketch: 'arm' });
  assert.match(elements.uploadStatus.textContent, /verification completed/);
  failFetch = true; await elements.setup.onclick();
  assert.match(elements.uploadStatus.textContent, /Start launch\.bat and keep its window open/);
  failFetch = false; context.location = { protocol: 'file:' }; await elements.setup.onclick();
  assert.match(elements.uploadStatus.textContent, /opened directly/);
  console.log('Dashboard demo and simulated serial connection tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
