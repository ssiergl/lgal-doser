// MQTT / Home Assistant (settings page): connection status for everyone;
// broker, TLS with CA upload, discovery and remote control in the pro view.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, t } from '../lib/i18n.js';
import { SaveBar, useMsg } from '../lib/ui.js';

add({
  en: {
    'mq.title': 'MQTT / Home Assistant', 'mq.state': 'Connection',
    'mq.st.disabled': 'off', 'mq.st.connecting': 'connecting to {h} …', 'mq.st.connected': 'connected to {h}',
    'mq.last': 'last error: {e}',
    'mq.e.dns': 'host name not found', 'mq.e.tcp': 'broker not reachable', 'mq.e.timeout': 'no answer',
    'mq.e.tls': 'TLS failed (certificate?)', 'mq.e.auth': 'user or password rejected', 'mq.e.refused': 'connection refused',
    'mq.pro': 'MQTT is set up in the Pro view.',
    'mq.enabled': 'Connect to an MQTT broker',
    'mq.host': 'Broker', 'mq.host.ph': 'host name or IP address', 'mq.port': 'Port',
    'mq.tls': 'Encryption', 'mq.tls.off': 'none', 'mq.tls.bundle': 'TLS, public certificate', 'mq.tls.custom': 'TLS, own CA certificate',
    'mq.tls.h.off': 'Only within your own network.',
    'mq.tls.h.bundle': 'For brokers with a certificate from a public authority (e.g. cloud brokers).',
    'mq.tls.h.custom': 'For your own broker with a self-signed certificate: upload its CA certificate (PEM).',
    'mq.ca': 'CA certificate', 'mq.ca.none': 'none uploaded', 'mq.ca.info': '{s}, valid until {d}', 'mq.ca.more': ' (+{n} more)',
    'mq.ca.upload': 'Upload …', 'mq.ca.replace': 'Replace …', 'mq.ca.remove': 'Remove', 'mq.ca.saved': 'Certificate saved.',
    'mq.ca.removed': 'Certificate removed.',
    'mq.user': 'User', 'mq.password': 'Password', 'mq.pw.keep': 'unchanged',
    'mq.base': 'Base topic', 'mq.base.h': 'All topics of this device start with it.',
    'mq.discovery': 'Home Assistant discovery', 'mq.discovery.h': 'Creates the device with all pumps in Home Assistant (MQTT integration required).',
    'mq.prefix': 'Discovery prefix',
    'mq.control': 'Allow control via MQTT',
    'mq.control.h': 'Home Assistant – and anyone with access to the broker – can then start doses, pause schedules and stop all pumps. Daily limits and lock rules still apply.',
    'mq.topics': 'Topics', 'mq.saved': 'Saved. The connection is being re-established.',
    'mq.field': 'Please check: {f}',
    'err.ca_missing': 'Upload a CA certificate first', 'err.invalid_certificate': 'Not a valid PEM certificate',
    'err.ca_in_use': 'The certificate is in use – choose another encryption first',
  },
  de: {
    'mq.title': 'MQTT / Home Assistant', 'mq.state': 'Verbindung',
    'mq.st.disabled': 'aus', 'mq.st.connecting': 'verbinde mit {h} …', 'mq.st.connected': 'verbunden mit {h}',
    'mq.last': 'letzter Fehler: {e}',
    'mq.e.dns': 'Hostname nicht gefunden', 'mq.e.tcp': 'Broker nicht erreichbar', 'mq.e.timeout': 'keine Antwort',
    'mq.e.tls': 'TLS fehlgeschlagen (Zertifikat?)', 'mq.e.auth': 'Benutzer oder Passwort abgelehnt', 'mq.e.refused': 'Verbindung abgelehnt',
    'mq.pro': 'MQTT wird in der Profi-Ansicht eingerichtet.',
    'mq.enabled': 'Mit einem MQTT-Broker verbinden',
    'mq.host': 'Broker', 'mq.host.ph': 'Hostname oder IP-Adresse', 'mq.port': 'Port',
    'mq.tls': 'Verschlüsselung', 'mq.tls.off': 'keine', 'mq.tls.bundle': 'TLS, öffentliches Zertifikat', 'mq.tls.custom': 'TLS, eigenes CA-Zertifikat',
    'mq.tls.h.off': 'Nur im eigenen Netzwerk.',
    'mq.tls.h.bundle': 'Für Broker mit Zertifikat einer öffentlichen Stelle (z. B. Cloud-Broker).',
    'mq.tls.h.custom': 'Für einen eigenen Broker mit selbst signiertem Zertifikat: dessen CA-Zertifikat (PEM) hochladen.',
    'mq.ca': 'CA-Zertifikat', 'mq.ca.none': 'keins hochgeladen', 'mq.ca.info': '{s}, gültig bis {d}', 'mq.ca.more': ' (+{n} weitere)',
    'mq.ca.upload': 'Hochladen …', 'mq.ca.replace': 'Ersetzen …', 'mq.ca.remove': 'Entfernen', 'mq.ca.saved': 'Zertifikat gespeichert.',
    'mq.ca.removed': 'Zertifikat entfernt.',
    'mq.user': 'Benutzer', 'mq.password': 'Passwort', 'mq.pw.keep': 'unverändert',
    'mq.base': 'Basis-Topic', 'mq.base.h': 'Alle Topics dieses Geräts beginnen damit.',
    'mq.discovery': 'Home-Assistant-Erkennung', 'mq.discovery.h': 'Legt das Gerät mit allen Pumpen in Home Assistant an (MQTT-Integration nötig).',
    'mq.prefix': 'Discovery-Präfix',
    'mq.control': 'Steuerung über MQTT erlauben',
    'mq.control.h': 'Home Assistant – und jeder mit Zugriff auf den Broker – kann dann Dosierungen starten, Zeitpläne pausieren und alle Pumpen stoppen. Tageslimits und Sperrregeln gelten weiter.',
    'mq.topics': 'Topics', 'mq.saved': 'Gespeichert. Die Verbindung wird neu aufgebaut.',
    'mq.field': 'Bitte prüfen: {f}',
    'err.ca_missing': 'Zuerst ein CA-Zertifikat hochladen', 'err.invalid_certificate': 'Kein gültiges PEM-Zertifikat',
    'err.ca_in_use': 'Das Zertifikat wird verwendet – erst eine andere Verschlüsselung wählen',
  },
});

const FIELD = { host: 'mq.host', port: 'mq.port', tls: 'mq.tls', username: 'mq.user', password: 'mq.password',
  base_topic: 'mq.base', discovery_prefix: 'mq.prefix' };

const draftOf = s => ({ enabled: s.enabled, host: s.host, port: s.port ? String(s.port) : '', tls: s.tls,
  username: s.username, password: '', base_topic: s.base_topic, discovery: s.discovery,
  discovery_prefix: s.discovery_prefix, control: s.control });

function bodyOf(d) {
  const b = { ...d, host: d.host.trim(), port: d.port.trim() === '' ? 0 : Number(d.port), base_topic: d.base_topic.trim(),
    discovery_prefix: d.discovery_prefix.trim() };
  if (!d.password) delete b.password;  // keep the stored one
  return b;
}

function Status({ doc }) {
  const st = doc.status, s = doc.settings;
  const h = s.host + ':' + doc.port;
  const err = st.error && html`<br/><span class="warn">${t('mq.last', { e: t('mq.e.' + st.error) })}</span>`;
  const txt = st.state === 'connected' ? html`<span class="ok">${t('mq.st.connected', { h })}</span>`
    : st.state === 'connecting' ? html`${t('mq.st.connecting', { h })}${err}` : t('mq.st.disabled');
  return html`<dl class="stats net"><dt>${t('mq.state')}</dt><dd>${txt}</dd></dl>`;
}

function CaField({ doc, onDone, msg }) {
  const [busy, setBusy] = useState(false);
  const file = useRef(null);
  const ca = doc.ca;
  const upload = async e => {
    const f = e.target.files[0];
    e.target.value = '';
    if (!f) return;
    setBusy(true);
    try {
      onDone(await api('PUT', '/api/v1/mqtt/ca', await f.text(), 'application/x-pem-file'));
      msg.ok(t('mq.ca.saved'));
    } catch (x) { msg.err(x); } finally { setBusy(false); }
  };
  const remove = async () => {
    setBusy(true);
    try { onDone(await api('DELETE', '/api/v1/mqtt/ca')); msg.ok(t('mq.ca.removed')); } catch (x) { msg.err(x); } finally { setBusy(false); }
  };
  return html`<div class="field"><label>${t('mq.ca')}</label>
    <p class="small">${ca.present ? html`${t('mq.ca.info', { s: ca.subject || '?', d: ca.valid_to || '?' })}${ca.count > 1 ? t('mq.ca.more', { n: ca.count - 1 }) : ''}`
      : html`<span class="mut">${t('mq.ca.none')}</span>`}</p>
    <div class="row">
      <input ref=${file} type="file" accept=".pem,.crt,.cer,application/x-pem-file" hidden onChange=${upload}/>
      <button type="button" class="sec" disabled=${busy} onClick=${() => file.current.click()}>${t(ca.present ? 'mq.ca.replace' : 'mq.ca.upload')}</button>
      ${ca.present && html`<button type="button" class="sec" disabled=${busy} onClick=${remove}>${t('mq.ca.remove')}</button>`}
    </div></div>`;
}

export function MqttCard({ pro }) {
  const [doc, setDoc] = useState(null);
  const [d, setDraft] = useState(null);
  const [busy, setBusy] = useState(false);
  const [bad, setBad] = useState('');
  const msg = useMsg();
  const alive = useRef(true);
  const load = (reset) => api('GET', '/api/v1/mqtt').then(j => {
    if (!alive.current) return;
    setDoc(j);
    if (reset) setDraft(draftOf(j.settings));
  }).catch(e => alive.current && msg.err(e));
  useEffect(() => {
    alive.current = true;
    load(true);
    const id = setInterval(() => load(false), 3000);
    return () => { alive.current = false; clearInterval(id); };
  }, []);

  const head = html`<h2>${t('mq.title')}</h2>`;
  if (!doc || !d) return html`<section class="card">${head}${msg.node || html`<p class="mut">…</p>`}</section>`;
  if (!pro) return html`<section class="card">${head}<${Status} doc=${doc}/><p class="small mut">${t('mq.pro')}</p></section>`;

  const set = patch => setDraft(prev => ({ ...prev, ...patch }));
  const dirty = JSON.stringify(bodyOf(d)) !== JSON.stringify(bodyOf(draftOf(doc.settings)));
  const cls = f => (bad === f ? 'bad' : '');
  const submit = e => {
    e.preventDefault();
    setBusy(true);
    setBad('');
    api('PUT', '/api/v1/mqtt', bodyOf(d))
      .then(j => { setDoc(j); setDraft(draftOf(j.settings)); msg.ok(t('mq.saved')); })
      .catch(x => {
        const f = x.body && x.body.error && x.body.error.field;
        if (x.code === 'invalid_param' && f && FIELD[f]) {
          setBad(f);
          msg.err(t('mq.field', { f: t(FIELD[f]) }));
          const el = document.getElementById('mq-' + f);
          if (el) el.focus();
        } else msg.err(x);
      })
      .finally(() => setBusy(false));
  };
  const base = d.base_topic.trim() || doc.base_topic;
  const defPort = d.tls === 'off' ? 1883 : 8883;

  return html`<form class="card mqtt" novalidate onSubmit=${submit}>
    ${head}<${Status} doc=${doc}/>
    <label class="check"><input type="checkbox" checked=${d.enabled} onChange=${e => set({ enabled: e.target.checked })}/>${t('mq.enabled')}</label>
    <div class="mq-hp">
      <label class="field">${t('mq.host')}
        <input id="mq-host" class=${cls('host')} maxlength="64" autocomplete="off" spellcheck="false" value=${d.host}
          placeholder=${t('mq.host.ph')}
          onInput=${e => set({ host: e.target.value })}/></label>
      <label class="field">${t('mq.port')}
        <input id="mq-port" class=${cls('port')} inputmode="numeric" maxlength="5" placeholder=${defPort} value=${d.port}
          onInput=${e => set({ port: e.target.value.replace(/\D/g, '') })}/></label>
    </div>
    <div class="field"><label for="mq-tls">${t('mq.tls')}</label>
      <select id="mq-tls" class=${cls('tls')} value=${d.tls} onChange=${e => set({ tls: e.target.value })}>
        ${['off', 'bundle', 'custom'].map(v => html`<option value=${v}>${t('mq.tls.' + v)}</option>`)}
      </select>
      <p class="small mut">${t('mq.tls.h.' + d.tls)}</p></div>
    ${d.tls === 'custom' && html`<${CaField} doc=${doc} msg=${msg} onDone=${j => setDoc(j)}/>`}
    <div class="mq-up">
      <label class="field">${t('mq.user')}
        <input id="mq-username" class=${cls('username')} maxlength="64" autocomplete="off" spellcheck="false" value=${d.username}
          onInput=${e => set({ username: e.target.value })}/></label>
      <label class="field">${t('mq.password')}
        <input id="mq-password" type="password" class=${cls('password')} maxlength="64" autocomplete="new-password"
          disabled=${!d.username} placeholder=${doc.settings.password_set && d.username ? t('mq.pw.keep') : ''}
          value=${d.password} onInput=${e => set({ password: e.target.value })}/></label>
    </div>
    <label class="field">${t('mq.base')}
      <input id="mq-base_topic" class=${cls('base_topic')} maxlength="64" autocomplete="off" spellcheck="false"
        placeholder=${doc.base_topic} value=${d.base_topic} onInput=${e => set({ base_topic: e.target.value })}/></label>
    <p class="small mut">${t('mq.base.h')}</p>
    <label class="check"><input type="checkbox" checked=${d.discovery} onChange=${e => set({ discovery: e.target.checked })}/>
      <span>${t('mq.discovery')}<br/><span class="small mut">${t('mq.discovery.h')}</span></span></label>
    ${d.discovery && html`<label class="field">${t('mq.prefix')}
      <input id="mq-discovery_prefix" class=${cls('discovery_prefix')} maxlength="32" autocomplete="off" spellcheck="false"
        value=${d.discovery_prefix} onInput=${e => set({ discovery_prefix: e.target.value })}/></label>`}
    <label class="check"><input type="checkbox" checked=${d.control} onChange=${e => set({ control: e.target.checked })}/>
      <span>${t('mq.control')}<br/><span class=${'small ' + (d.control ? 'warn' : 'mut')}>${t('mq.control.h')}</span></span></label>
    <details class="mq-topics"><summary>${t('mq.topics')}</summary>
      <ul class="small mono">
        <li>${base}/status</li><li>${base}/state</li><li>${base}/pump/1…5/state</li>
        ${d.control && html`<li>${base}/pump/1…5/dose/set</li><li>${base}/pump/1…5/amount/set</li>
          <li>${base}/pump/1…5/schedule/set</li><li>${base}/stop/set</li>`}
      </ul></details>
    <${SaveBar} busy=${busy} dirty=${dirty} onDiscard=${() => { setDraft(draftOf(doc.settings)); setBad(''); msg.clear(); }}/>
    ${msg.node}
  </form>`;
}
