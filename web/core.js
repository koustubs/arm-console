(function (root) {
  'use strict';
  function parseLine(line) {
    line = line.trim();
    if (!line) return null;
    const parts = line.split(',');
    if (parts[0] === 'S' && parts.length === 11) {
      const fields = [parts[1], parts[2], parts[3], parts[4], parts[5], ...parts.slice(7)];
      const nums = fields.map(Number);
      if (fields.some(field => !/^-?\d+(?:\.\d+)?$/.test(field)) || nums.some(n => !Number.isFinite(n)) || !/^[A-Z_]+$/.test(parts[6]) ||
          !Number.isInteger(nums[0]) || nums[0] < 0 || !Number.isInteger(nums[1]) || nums[1] < 0 ||
          !Number.isInteger(nums[2]) || nums[2] < 0 || ![0, 1].includes(nums[4]) ||
          nums.slice(5).some(n => !Number.isInteger(n) || n < 0 || n > 180) ||
          (nums[2] === 0 ? nums[3] !== -1 : nums[3] < 0)) return { type: 'unknown', raw: line };
      return { type: 'sample', seq: nums[0], deviceMs: nums[1], echoUs: nums[2],
        distance: nums[2] ? nums[3] : null, grip: nums[4], state: parts[6],
        base: nums[5], shoulder: nums[6], elbow: nums[7], gripper: nums[8] };
    }
    let match = /^DISTANCE:\s*(-?\d+(?:\.\d+)?)(?:\s*CM)?$/i.exec(line);
    if (match) return { type: 'legacyDistance', distance: Number(match[1]) >= 0 ? Number(match[1]) : null };
    match = /^GRIP:\s*([01])$/.exec(line);
    if (match) return { type: 'legacyGrip', grip: Number(match[1]) };
    if (line === 'ARM,1') return { type: 'hello' };
    if (parts[0] === 'E' && parts.length === 3 && /^\d+$/.test(parts[1]) && Number.isSafeInteger(Number(parts[1])) && /^[A-Z_]+$/.test(parts[2])) return { type: 'event', message: parts[2], deviceMs: Number(parts[1]) };
    if (['F', 'A', 'N'].includes(parts[0]) && parts.length === 2) return { type: parts[0] === 'F' ? 'fault' : parts[0] === 'A' ? 'ack' : 'nack', message: parts[1] };
    return { type: 'unknown', raw: line };
  }
  class LineBuffer {
    constructor(onLine) { this.text = ''; this.onLine = onLine; this.dropping = false; }
    push(chunk) {
      let start = 0;
      while (start < chunk.length) {
        const end = chunk.indexOf('\n', start);
        const fragment = chunk.slice(start, end === -1 ? undefined : end);
        if (!this.dropping) {
          this.text += fragment;
          if (this.text.length > 4096) {
            this.text = ''; this.dropping = true;
            this.onLine('[oversized serial line discarded]');
          }
        }
        if (end === -1) break;
        if (!this.dropping) this.onLine(this.text.replace(/\r$/, ''));
        this.text = ''; this.dropping = false; start = end + 1;
      }
    }
  }
  class MovingAverage {
    constructor(size = 5) { this.size = size; this.values = []; }
    add(value) {
      if (value === null || !Number.isFinite(value)) { this.values = []; return null; }
      this.values.push(value); if (this.values.length > this.size) this.values.shift();
      return this.values.reduce((a, b) => a + b, 0) / this.values.length;
    }
  }
  function csv(rows) {
    const keys = ['received_at', 'source', 'kind', 'device_ms', 'sequence', 'echo_us', 'distance_cm', 'filtered_cm', 'grip_input', 'state', 'base_command_deg', 'shoulder_command_deg', 'elbow_command_deg', 'gripper_command_deg', 'message', 'raw_line'];
    const cell = v => '"' + String(v ?? '').replace(/"/g, '""') + '"';
    return [keys.join(','), ...rows.map(row => keys.map(k => cell(row[k])).join(','))].join('\r\n') + '\r\n';
  }
  const api = { parseLine, LineBuffer, MovingAverage, csv };
  if (typeof module !== 'undefined') module.exports = api;
  root.ArmCore = api;
})(globalThis);
