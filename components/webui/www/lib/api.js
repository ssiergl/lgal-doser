// REST and WebSocket access to the device.

export class ApiError extends Error {
  constructor(code, message, status, body) {
    super(message || code);
    this.code = code;
    this.status = status;
    this.body = body;
  }
}

function toLogin() {
  location.href = '/login?next=' + encodeURIComponent('/' + location.hash);
}

// method, path, optional JSON body -> parsed JSON; throws ApiError.
// With a content type the body is sent as it is (text, file).
export async function api(method, path, body, type) {
  const opt = { method, cache: 'no-store', headers: {} };
  if (body !== undefined) {
    opt.headers['Content-Type'] = type || 'application/json';
    opt.body = type ? body : JSON.stringify(body);
  }
  let r;
  try {
    r = await fetch(path, opt);
  } catch (e) {
    throw new ApiError('network', e.message, 0);
  }
  let j = null;
  try { j = await r.json(); } catch (e) { /* empty body */ }
  if (r.status === 401) {
    toLogin();
    throw new ApiError('unauthorized', 'Login required', 401, j);
  }
  if (!r.ok) {
    const e = (j && j.error) || {};
    throw new ApiError(e.code || 'http_' + r.status, e.message, r.status, j);
  }
  return j;
}

// Query string from an object, undefined/null members are left out
export function qs(o) {
  return Object.entries(o)
    .filter(([, v]) => v !== undefined && v !== null && v !== '')
    .map(([k, v]) => k + '=' + encodeURIComponent(v)).join('&');
}

// ---------------------------------------------------------------------------
// WebSocket /api/v1/ws: status push + request/response (see lgal/ws.h)

let ws = null;
let nextId = 0;
let retry = 1000;
const waiting = new Map();
const listeners = { status: new Set(), state: new Set() };

export const liveOpen = () => ws !== null && ws.readyState === 1;

export function onLive(kind, f) {
  listeners[kind].add(f);
  return () => listeners[kind].delete(f);
}

function setState() { listeners.state.forEach(f => f(liveOpen())); }

export function connectLive() {
  const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/api/v1/ws';
  ws = new WebSocket(url);
  ws.onopen = () => { retry = 1000; setState(); };
  ws.onmessage = ev => {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    if (m.type === 'status') { listeners.status.forEach(f => f(m)); return; }
    const w = waiting.get(m.id);
    if (!w) return;
    waiting.delete(m.id);
    clearTimeout(w.timer);
    if (m.ok) w.ok(m); else w.fail(new ApiError(m.error, m.error, 409, m));
  };
  ws.onclose = () => {
    ws = null;
    setState();
    for (const w of waiting.values()) { clearTimeout(w.timer); w.fail(new ApiError('network', 'closed', 0)); }
    waiting.clear();
    setTimeout(connectLive, retry);
    retry = Math.min(retry * 2, 10000);
  };
}

// Request over the WebSocket -> reply; rejects after 3 s
export function liveCall(msg) {
  return new Promise((ok, fail) => {
    if (!liveOpen()) { fail(new ApiError('network', 'offline', 0)); return; }
    const id = ++nextId;
    const timer = setTimeout(() => { waiting.delete(id); fail(new ApiError('network', 'timeout', 0)); }, 3000);
    waiting.set(id, { ok, fail, timer });
    ws.send(JSON.stringify({ ...msg, id }));
  });
}
