'use strict';

const byId = id => document.getElementById(id);
const keys = ['frame_rate', 'exposure', 'analogue_gain'];
const names = ['Frame rate', 'Exposure', 'Analog gain'];
const actions = ['start', 'stop', 'apply', 'refresh', 'snapshot', 'save', 'record', 'finish'];
const unavailable = '—';
let initialized = false;
let state = null;
let busy = false;
let readAttempted = false;
let revision = 0;
let lastStamp = '';

async function getJson(path, options = {}, timeout = 4000) {
  const abort = new AbortController();
  const timer = setTimeout(() => abort.abort(), timeout);
  try {
    const response = await fetch(path, { ...options, signal: abort.signal });
    const data = await response.json();
    if (!response.ok || data.success === false) throw new Error(data.message || 'Request failed.');
    return data;
  } finally {
    clearTimeout(timer);
  }
}

function values() {
  const requested = {};
  for (const key of keys) {
    const field = byId(key);
    if (!field.reportValidity() || !Number.isSafeInteger(field.valueAsNumber)) {
      throw new Error('Enter a whole number within the allowed range.');
    }
    requested[key] = field.valueAsNumber;
  }
  return requested;
}

function message(text, error = false) {
  byId('result').textContent = text;
  byId('result').classList.toggle('error', error);
}

function showStatus(label, level, detail = '') {
  byId('status').textContent = label;
  byId('status').dataset.level = level;
  byId('diagnostic').hidden = !detail;
  byId('diagnostic').textContent = detail;
  byId('diagnostic').dataset.level = level;
}

function hideImages() {
  lastStamp = '';
  for (const side of ['left', 'right']) {
    byId(side).hidden = true;
    byId(side).removeAttribute('src');
  }
  byId('stamp').textContent = 'Awaiting a stereo pair';
}

function liveRate(value, fresh) {
  if (!fresh || value == null || value === '' || !Number.isFinite(Number(value))) return unavailable;
  return Number(value).toFixed(2);
}

function showReadback(actual) {
  for (const [i, key] of keys.entries()) {
    byId('actual-' + key).textContent = actual ? actual[key] : unavailable;
    const input = byId(key);
    input.min = actual ? actual.minimum[i] : key === 'frame_rate' ? 1 : 0;
    input.step = actual ? actual.step[i] : 1;
    if (actual) input.max = actual.maximum[i];
    else input.removeAttribute('max');
  }
  byId('ranges').textContent = actual
    ? (actual.simulated ? 'Simulated controls' : 'Driver control values') + '\n' +
      keys.map((_, i) => names[i] + '  ' + actual.minimum[i] + '–' + actual.maximum[i] + ', step ' + actual.step[i]).join('\n')
    : 'Available after readback.';
}

function canControl(s) {
  return s && s.running && s.diagnostic_fresh && ['running', 'degraded'].includes(s.diagnostic?.state);
}

function buttons() {
  const locked = !state || busy || state.busy;
  byId('start').hidden = Boolean(state?.running);
  byId('stop').hidden = !state?.running;
  byId('start').disabled = locked || state.running;
  byId('stop').disabled = locked || !state.running;
  byId('save').disabled = locked;
  const bag = state?.recording;
  const active = ['starting', 'recording', 'finalizing'].includes(bag?.state);
  byId('record').hidden = active;
  byId('finish').hidden = !active;
  byId('record').disabled = locked || !bag?.available || !state?.preview_fresh || !state?.running;
  byId('finish').disabled = locked || bag?.state === 'finalizing';
  for (const id of ['refresh', 'apply', 'snapshot']) byId(id).disabled = locked || !canControl(state);
}

function render(s) {
  state = s;
  const bag = s.recording || {};
  const labels = {idle: 'Ready', starting: 'Starting recorder', recording: 'Recording',
                  finalizing: 'Saving…', saved: 'Saved', error: 'Recording error'};
  byId('bag-status').textContent = bag.available ? labels[bag.state] : 'Requires ROS';
  byId('bag-status').dataset.recording = String(bag.state === 'recording');
  const seconds = bag.seconds || 0;
  byId('bag-time').textContent = bag.path
    ? Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0') : '';
  byId('bag-path').textContent = bag.path || '';
  byId('bag-error').textContent = bag.message || '';
  byId('mode').textContent = s.mode;
  byId('logs').textContent = 'Session folder: ' + s.log_root;
  if (!initialized) {
    for (const key of keys) byId(key).value = s.requested[key];
    initialized = true;
  }
  const diag = s.diagnostic || {};
  const recent = s.running && s.diagnostic_fresh;
  const reporting = recent && ['running', 'degraded'].includes(diag.state);
  byId('capture').textContent = liveRate(diag.capture_fps, reporting);
  byId('publication').textContent = liveRate(diag.publication_fps, reporting);
  byId('target').textContent = s.readback?.frame_rate ?? s.requested.frame_rate;
  showReadback(recent ? s.readback : null);
  if (s.acquisition_exited) {
    showStatus('Exited', 'error', 'Acquisition exited with code ' + s.exit_code + '. Check the session log.');
  } else if (!s.running) {
    showStatus('Stopped', 'idle');
  } else if (!recent) {
    showStatus(s.diagnostic ? 'Diagnostics stale' : 'Starting', s.diagnostic ? 'warning' : 'idle');
  } else {
    const raw = String(diag.state || 'unknown').replace(/_/g, ' ');
    const level = Number(diag.level) >= 2 ? 'error' : Number(diag.level) === 1 ? 'warning' : 'ok';
    showStatus(raw.charAt(0).toUpperCase() + raw.slice(1), level, level === 'ok' ? '' : diag.message);
  }
  if (!s.running) readAttempted = false;
  if (!s.running || !s.preview_fresh) hideImages();
  buttons();
}

function disconnected() {
  state = null;
  byId('bag-status').textContent = 'Status unavailable';
  byId('bag-status').dataset.recording = 'false';
  for (const id of ['bag-time', 'bag-path', 'bag-error']) byId(id).textContent = '';
  for (const id of ['capture', 'publication', 'target']) byId(id).textContent = unavailable;
  showReadback(null);
  hideImages();
  showStatus('Disconnected', 'error', 'The local viewer is unavailable.');
  buttons();
}

async function update() {
  const seq = ++revision;
  try {
    const s = await getJson('/api/state');
    if (seq !== revision) return;
    render(s);
    if (canControl(s) && !s.readback && !readAttempted && !busy && !s.busy) {
      readAttempted = true;
      await command('refresh', true);
    }
  } catch {
    if (seq === revision) disconnected();
  }
}

async function command(action, automatic = false) {
  if (busy || byId(action).disabled) return;
  busy = true;
  buttons();
  try {
    const body = ['start', 'apply', 'save'].includes(action) ? {controls: values()} : {};
    const result = await getJson('/api/' + action, {
      method: 'POST', headers: {'Content-Type': 'application/json', 'X-ArduStereo-Token': token},
      body: JSON.stringify(body)
    }, action === 'stop' ? 40000 : 16000);
    if (!automatic && !['record', 'finish'].includes(action)) message(result.path || result.message || 'Done.');
    if (action === 'start') readAttempted = false;
  } catch (error) {
    message(error.name === 'AbortError' ? 'Request timed out. Check the state and read controls before retrying.' : error.message, true);
  } finally {
    busy = false;
    await update();
  }
}

async function preview() {
  try {
    if (state?.running) {
      const p = await getJson('/api/preview');
      if (state?.running && p.fresh) {
        if (p.stamp_ns !== lastStamp) {
          for (const side of ['left', 'right']) {
            byId(side).src = p[side];
            byId(side).hidden = false;
          }
          lastStamp = p.stamp_ns;
          byId('stamp').textContent = 'Pair timestamp: ' + lastStamp + ' ns (host receipt)';
        }
      } else hideImages();
    }
  } catch { hideImages(); }
  setTimeout(preview, 250);
}

for (const id of actions) byId(id).addEventListener('click', () => command(id));
async function poll() { await update(); setTimeout(poll, 500); }
poll();
preview();
