// Device data shared by all views. Full data by REST, live values merged in
// from the WebSocket status push.
//   pumps  GET /api/v1/pumps            every 10 s (1 s without WebSocket)
//   sched  GET /api/v1/schedule/status  every 30 s and after a run ends
//   info   GET /api/v1/system/info      at start and every 60 s
import { api, connectLive, liveOpen, onLive } from './api.js';
import { hub } from './hub.js';

const changes = hub();
export const state = {
  pumps: null,   // array of 5 (GET /api/v1/pumps)
  sched: null,   // GET /api/v1/schedule/status
  info: null,    // GET /api/v1/system/info
  live: false,   // WebSocket open
  timeValid: true,
  error: null,   // last load error (ApiError)
};

export const useStore = () => { changes.use(); return state; };
const emit = () => changes.emit();

let last = { pumps: 0, sched: 0, info: 0 };

async function load(key, path, apply) {
  last[key] = Date.now();
  try {
    apply(await api('GET', path));
    state.error = null;
  } catch (e) {
    state.error = e;
  }
  emit();
}

export const reloadPumps = () => load('pumps', '/api/v1/pumps', j => { state.pumps = j.pumps; });
export const reloadSched = () => load('sched', '/api/v1/schedule/status', j => { state.sched = j; });
export const reloadInfo = () => load('info', '/api/v1/system/info', j => { state.info = j; });
export const reloadAll = () => Promise.all([reloadPumps(), reloadSched(), reloadInfo()]);

// Live values of the status push into the full pump objects
function merge(m) {
  state.timeValid = m.time_valid;
  if (!state.pumps || !m.pumps) return;
  let ended = false;
  for (const l of m.pumps) {
    const p = state.pumps[l.pump - 1];
    if (!p) continue;
    if (p.activity !== 'idle' && l.activity === 'idle') ended = true;
    Object.assign(p, {
      activity: l.activity, phase: l.phase, trigger: l.trigger, target_ml: l.target_ml,
      done_ml: l.done_ml, rpm: l.rpm, prime_s: l.prime_s,
    });
    p.container.remaining_ml = l.remaining_ml;
    if (l.percent !== undefined) p.container.percent = l.percent;
    p.daily.used_ml = l.used_today_ml;
    if (l.last) p.last = { ...p.last, ...l.last };
  }
  emit();
  if (ended) { reloadPumps(); reloadSched(); }
}

export function start() {
  onLive('status', merge);
  onLive('state', open => { state.live = open; emit(); });
  connectLive();
  reloadAll();
  setInterval(() => {
    const now = Date.now();
    if (now - last.pumps >= (liveOpen() ? 10000 : 1000)) reloadPumps();
    if (now - last.sched >= 30000) reloadSched();
    if (now - last.info >= 60000) reloadInfo();
  }, 500);
}
