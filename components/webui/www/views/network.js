// Network (settings page): connection status for everyone; interface, LAN /
// Wi-Fi addresses, Wi-Fi search and test in the pro view.
import { useEffect, useRef, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, errText, t } from '../lib/i18n.js';
import { SaveBar, useMsg } from '../lib/ui.js';

add({
  en: {
    'nw.title': 'Network', 'nw.active': 'Active', 'nw.lan': 'LAN', 'nw.wlan': 'Wi-Fi', 'nw.ap': 'Setup Wi-Fi',
    'nw.fallback': 'Wi-Fi starts in {s} s', 'nw.pro': 'Network settings can be changed in the Pro view.',
    'nw.act.lan': 'LAN', 'nw.act.wlan': 'Wi-Fi', 'nw.act.none': 'no connection',
    'nw.lan.missing': 'W5500 not found', 'nw.lan.off': 'switched off', 'nw.lan.nolink': 'no cable / link', 'nw.lan.noip': 'link, waiting for an address',
    'nw.wlan.off': 'off', 'nw.wlan.connecting': 'connecting to {s} …', 'nw.wlan.connected': '{s}, {r} dBm',
    'nw.wlan.disconnected': 'not connected ({x})', 'nw.wlan.none': 'not configured',
    'nw.ap.on': 'on, {n} client(s)', 'nw.ap.off': 'off', 'nw.static': 'static',
    'nw.mode': 'Interface', 'nw.mode.auto': 'Automatic', 'nw.mode.lan': 'LAN only', 'nw.mode.wlan': 'Wi-Fi only',
    'nw.mode.h.auto': 'LAN first. Wi-Fi starts after the LAN has been down for 30 s and stops again when the cable is back.',
    'nw.mode.h.lan': 'The Wi-Fi radio stays off.', 'nw.mode.h.wlan': 'Ethernet is switched off.',
    'nw.dhcp': 'Automatic address (DHCP)', 'nw.ip': 'IP address', 'nw.netmask': 'Subnet mask', 'nw.gateway': 'Gateway', 'nw.dns1': 'DNS 1', 'nw.dns2': 'DNS 2',
    'nw.ssid': 'Network (SSID)', 'nw.password': 'Password', 'nw.pw.keep': 'unchanged',
    'nw.scan': 'Search', 'nw.scan.run': 'Searching …', 'nw.scan.none': 'No networks found.', 'nw.scan.fail': 'Search failed.',
    'nw.test': 'Test Wi-Fi', 'nw.test.run': 'Connecting to {s} …', 'nw.test.ok': 'Connected – address {ip}. Not saved yet.', 'nw.test.fail': 'Connection failed: {r}',
    'nw.saved': 'Saved. The settings apply in a moment.',
    'nw.saved.moved': 'Saved. The device will be at {u} – this page may lose the connection.',
    'nw.field': 'Please check: {f}',
    'nw.r.timeout': 'no answer', 'nw.r.no_ap_found': 'network not found', 'nw.r.wrong_password': 'wrong password',
    'nw.r.security_mismatch': 'security not supported (WPA2 or newer required)', 'nw.r.connection_failed': 'connection failed',
    'err.wlan_not_configured': '"Wi-Fi only" needs a Wi-Fi network', 'err.busy': 'A search is already running',
    'err.wifi_unavailable': 'Wi-Fi is not available',
  },
  de: {
    'nw.title': 'Netzwerk', 'nw.active': 'Aktiv', 'nw.lan': 'LAN', 'nw.wlan': 'WLAN', 'nw.ap': 'Setup-WLAN',
    'nw.fallback': 'WLAN startet in {s} s', 'nw.pro': 'Netzwerk-Einstellungen lassen sich in der Profi-Ansicht ändern.',
    'nw.act.lan': 'LAN', 'nw.act.wlan': 'WLAN', 'nw.act.none': 'keine Verbindung',
    'nw.lan.missing': 'W5500 nicht gefunden', 'nw.lan.off': 'abgeschaltet', 'nw.lan.nolink': 'kein Kabel / Link', 'nw.lan.noip': 'Link, warte auf Adresse',
    'nw.wlan.off': 'aus', 'nw.wlan.connecting': 'verbinde mit {s} …', 'nw.wlan.connected': '{s}, {r} dBm',
    'nw.wlan.disconnected': 'nicht verbunden ({x})', 'nw.wlan.none': 'nicht eingerichtet',
    'nw.ap.on': 'an, {n} Client(s)', 'nw.ap.off': 'aus', 'nw.static': 'statisch',
    'nw.mode': 'Schnittstelle', 'nw.mode.auto': 'Automatisch', 'nw.mode.lan': 'Nur LAN', 'nw.mode.wlan': 'Nur WLAN',
    'nw.mode.h.auto': 'LAN zuerst. WLAN startet, wenn das LAN 30 s weg ist, und geht wieder aus, sobald das Kabel zurück ist.',
    'nw.mode.h.lan': 'Das WLAN-Funkmodul bleibt aus.', 'nw.mode.h.wlan': 'Ethernet wird abgeschaltet.',
    'nw.dhcp': 'Adresse automatisch (DHCP)', 'nw.ip': 'IP-Adresse', 'nw.netmask': 'Subnetzmaske', 'nw.gateway': 'Gateway', 'nw.dns1': 'DNS 1', 'nw.dns2': 'DNS 2',
    'nw.ssid': 'Netzwerk (SSID)', 'nw.password': 'Passwort', 'nw.pw.keep': 'unverändert',
    'nw.scan': 'Suchen', 'nw.scan.run': 'Suche …', 'nw.scan.none': 'Keine Netzwerke gefunden.', 'nw.scan.fail': 'Suche fehlgeschlagen.',
    'nw.test': 'WLAN testen', 'nw.test.run': 'Verbinde mit {s} …', 'nw.test.ok': 'Verbunden – Adresse {ip}. Noch nicht gespeichert.', 'nw.test.fail': 'Verbindung fehlgeschlagen: {r}',
    'nw.saved': 'Gespeichert. Die Einstellungen gelten gleich.',
    'nw.saved.moved': 'Gespeichert. Das Gerät ist dann unter {u} erreichbar – diese Seite kann die Verbindung verlieren.',
    'nw.field': 'Bitte prüfen: {f}',
    'nw.r.timeout': 'keine Antwort', 'nw.r.no_ap_found': 'Netzwerk nicht gefunden', 'nw.r.wrong_password': 'falsches Passwort',
    'nw.r.security_mismatch': 'Verschlüsselung nicht unterstützt (WPA2 oder neuer nötig)', 'nw.r.connection_failed': 'Verbindung fehlgeschlagen',
    'err.wlan_not_configured': '„Nur WLAN“ braucht ein WLAN-Netzwerk', 'err.busy': 'Es läuft bereits eine Suche',
    'err.wifi_unavailable': 'WLAN ist nicht verfügbar',
  },
});

const F = ['ip', 'netmask', 'gateway', 'dns1', 'dns2'];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const ipOf = c => ({ dhcp: c.dhcp, ...Object.fromEntries(F.map(k => [k, c[k] || ''])) });
const draftOf = c => ({ mode: c.mode, lan: ipOf(c.lan), wlan: { ...ipOf(c.wlan), ssid: c.wlan.ssid, password: '' } });

// Draft -> PUT body; the password only goes along when typed or when the network changes
function bodyOf(d, cfg) {
  const ip = x => ({ dhcp: x.dhcp, ...Object.fromEntries(F.map(k => [k, x[k].trim()])) });
  const w = { ...ip(d.wlan), ssid: d.wlan.ssid };
  if (d.wlan.password || d.wlan.ssid !== cfg.wlan.ssid || !cfg.wlan.password_set) w.password = d.wlan.password;
  return { mode: d.mode, lan: ip(d.lan), wlan: w };
}

function Addr({ a, pro }) {
  if (!a) return null;
  if (!pro) return html`${a.ip} <span class="mut">(${a.dhcp ? 'DHCP' : t('nw.static')})</span>`;
  return html`${a.ip} / ${a.netmask}${a.gateway && ' · GW ' + a.gateway}${a.dns1 && ' · DNS ' + a.dns1}
    <span class="mut"> (${a.dhcp ? 'DHCP' : t('nw.static')})</span>`;
}

function Status({ s, pro }) {
  const L = s.lan, W = s.wlan;
  const lan = !L.present ? html`<span class="err">${t('nw.lan.missing')}</span>`
    : !L.running ? t('nw.lan.off') : !L.link ? t('nw.lan.nolink')
      : L.ip ? html`<${Addr} a=${L.address || { ip: L.ip, dhcp: true }} pro=${pro}/>` : t('nw.lan.noip');
  const wlan = !W.configured ? t('nw.wlan.none')
    : W.state === 'connected' ? html`${t('nw.wlan.connected', { s: W.ssid, r: W.rssi })}${W.ip && html`<br/><${Addr} a=${W.address || { ip: W.ip, dhcp: true }} pro=${pro}/>`}`
      : W.state === 'connecting' ? t('nw.wlan.connecting', { s: W.ssid })
        : W.state === 'disconnected' ? html`<span class="warn">${t('nw.wlan.disconnected', { x: t('nw.r.' + (W.reason || 'connection_failed')) })}</span>`
          : t('nw.wlan.off');
  return html`<dl class="stats net">
    <dt>${t('nw.active')}</dt><dd>${t('nw.act.' + s.active)}</dd>
    <dt>${t('nw.lan')}</dt><dd>${lan}</dd>
    <dt>${t('nw.wlan')}</dt><dd>${wlan}${s.fallback_in_s !== undefined && html`<br/><span class="mut">${t('nw.fallback', { s: s.fallback_in_s })}</span>`}</dd>
    ${(pro || s.ap.active) && html`<dt>${t('nw.ap')}</dt><dd>${s.ap.active ? t('nw.ap.on', { n: s.ap.clients }) : t('nw.ap.off')}</dd>`}
  </dl>`;
}

function IpFields({ p, v, bad, onChange, cur }) {
  // switching to a static address: start from the addresses in use
  const toggle = dhcp => {
    const patch = { dhcp };
    if (!dhcp) for (const k of F) if (!v[k]) patch[k] = (cur && cur[k]) || (k === 'netmask' ? '255.255.255.0' : '');
    onChange(patch);
  };
  return html`<label class="check"><input type="checkbox" checked=${v.dhcp} onChange=${e => toggle(e.target.checked)}/>${t('nw.dhcp')}</label>
    ${!v.dhcp && html`<div class="ipgrid">${F.map(k => html`<label class="field">${t('nw.' + k)}
      <input id=${p + '-' + k} class=${bad === p + '.' + k ? 'bad' : ''} inputmode="decimal" autocomplete="off" spellcheck="false"
        value=${v[k]} onInput=${e => onChange({ [k]: e.target.value })}/></label>`)}</div>`}`;
}

export function NetworkCard({ pro }) {
  const [s, setS] = useState(null);       // status
  const [cfg, setCfg] = useState(null);   // saved settings
  const [d, setDraft] = useState(null);
  const [busy, setBusy] = useState('');   // '', 'save', 'scan', 'test'
  const [nets, setNets] = useState(null); // scan result
  const [bad, setBad] = useState('');     // field the device rejected
  const msg = useMsg();
  const scanMsg = useMsg();
  const alive = useRef(true);
  const status = () => api('GET', '/api/v1/net/status').then(j => alive.current && setS(j)).catch(e => alive.current && setS(prev => prev || { error: e }));
  useEffect(() => {
    alive.current = true;
    status();
    const id = setInterval(status, 3000);
    return () => { alive.current = false; clearInterval(id); };
  }, []);
  useEffect(() => {
    if (pro && !cfg) api('GET', '/api/v1/net/settings').then(c => { setCfg(c); setDraft(draftOf(c)); }).catch(e => msg.err(e));
  }, [pro]);

  const head = html`<h2>${t('nw.title')}</h2>`;
  if (!s) return html`<section class="card">${head}<p class="mut">…</p></section>`;
  const statusPart = s.error ? html`<p class="msg err">${errText(s.error)}</p>` : html`<${Status} s=${s} pro=${pro}/>`;
  if (!pro) return html`<section class="card">${head}${statusPart}<p class="small mut">${t('nw.pro')}</p></section>`;
  if (!d) return html`<section class="card">${head}${statusPart}${msg.node}</section>`;

  const cur = s.error ? {} : { lan: s.lan.address, wlan: s.wlan.address };
  const setPart = (part, patch) => setDraft(prev => ({ ...prev, [part]: { ...prev[part], ...patch } }));
  const dirty = JSON.stringify(bodyOf(d, cfg)) !== JSON.stringify(bodyOf(draftOf(cfg), cfg));
  const fail = e => {
    const f = e.code === 'invalid_param' && e.body && e.body.error && e.body.error.field;
    setBad(f || '');
    if (!f) { msg.err(e); return; }
    const [a, b] = f.split('.');
    msg.err(t('nw.field', { f: b ? t('nw.' + a) + ' · ' + t('nw.' + b) : t('nw.' + a) }));
    const el = document.getElementById(f.replace('.', '-'));
    if (el) el.focus();
  };

  const submit = e => {
    e.preventDefault();
    const b = bodyOf(d, cfg);
    setBusy('save');
    setBad('');
    api('PUT', '/api/v1/net/settings', b)
      .then(c => {
        // static LAN address changed while we talk over the LAN: show where to go
        const h = location.hostname;
        const moved = !b.lan.dhcp && b.lan.ip && b.lan.ip !== h && cfg.lan.ip !== b.lan.ip && !/\.local$/.test(h) && h !== '192.168.4.1';
        setCfg(c);
        setDraft(draftOf(c));
        msg.ok(moved ? t('nw.saved.moved', { u: 'http://' + b.lan.ip + '/#/settings' }) : t('nw.saved'));
        setTimeout(status, 2500);
      })
      .catch(fail)
      .finally(() => setBusy(''));
  };

  const scan = async () => {
    setBusy('scan');
    setNets(null);
    scanMsg.ok(t('nw.scan.run'));
    try {
      await api('POST', '/api/v1/net/scan').catch(e => { if (e.code !== 'busy') throw e; });
      let r;
      for (let i = 0; i < 40 && alive.current; ++i) {
        await sleep(750);
        r = await api('GET', '/api/v1/net/scan');
        if (r.state !== 'running') break;
      }
      if (!r || r.state !== 'done') { scanMsg.err(t('nw.scan.fail')); return; }
      if (r.networks.length) scanMsg.clear(); else scanMsg.ok(t('nw.scan.none'));
      setNets(r.networks);
    } catch (e) {
      scanMsg.err(e);
    } finally {
      setBusy('');
    }
  };

  const test = async () => {
    const w = bodyOf(d, cfg).wlan;
    setBusy('test');
    setBad('');
    msg.ok(t('nw.test.run', { s: w.ssid }));
    try {
      await api('POST', '/api/v1/net/wlan/test', w);
      let r;
      for (let i = 0; i < 60 && alive.current; ++i) {
        await sleep(1000);
        r = await api('GET', '/api/v1/net/wlan/test');
        if (r.state !== 'running') break;
      }
      if (r && r.state === 'ok') msg.ok(t('nw.test.ok', { ip: r.ip }));
      else msg.err(t('nw.test.fail', { r: t('nw.r.' + ((r && r.reason) || 'timeout')) }));
    } catch (e) {
      fail(e);
    } finally {
      setBusy('');
      status();
    }
  };

  return html`<form class="card netform" novalidate onSubmit=${submit}>
    ${head}${statusPart}
    <div class="field"><label for="mode">${t('nw.mode')}</label>
      <select id="mode" value=${d.mode} class=${bad === 'mode' ? 'bad' : ''} onChange=${e => setDraft(prev => ({ ...prev, mode: e.target.value }))}>
        ${['auto', 'lan', 'wlan'].map(m => html`<option value=${m}>${t('nw.mode.' + m)}</option>`)}
      </select>
      <p class="small mut">${t('nw.mode.h.' + d.mode)}</p></div>
    <fieldset class="sub"><legend>${t('nw.lan')}</legend>
      <${IpFields} p="lan" v=${d.lan} bad=${bad} cur=${cur.lan} onChange=${patch => setPart('lan', patch)}/></fieldset>
    <fieldset class="sub"><legend>${t('nw.wlan')}</legend>
      <label class="field">${t('nw.ssid')}
        <div class="row nowrap"><input id="wlan-ssid" class=${bad === 'wlan.ssid' ? 'bad' : ''} maxlength="32" autocomplete="off" spellcheck="false"
          value=${d.wlan.ssid} onInput=${e => setPart('wlan', { ssid: e.target.value })}/>
          <button type="button" class="sec" disabled=${!!busy} onClick=${scan}>${t('nw.scan')}</button></div></label>
      ${scanMsg.node}
      ${nets && nets.length > 0 && html`<div class="nets">${nets.map(n => html`<button type="button"
        onClick=${() => { setPart('wlan', { ssid: n.ssid }); setNets(null); const p = document.getElementById('wlan-password'); if (p) p.focus(); }}>
        <span>${n.secure ? '🔒 ' : ''}${n.ssid}</span><span class="mut">${n.rssi} dBm</span></button>`)}</div>`}
      <label class="field">${t('nw.password')}
        <input id="wlan-password" type="password" class=${bad === 'wlan.password' ? 'bad' : ''} maxlength="64" autocomplete="new-password"
          placeholder=${cfg.wlan.password_set && d.wlan.ssid === cfg.wlan.ssid ? t('nw.pw.keep') : ''}
          value=${d.wlan.password} onInput=${e => setPart('wlan', { password: e.target.value })}/></label>
      <${IpFields} p="wlan" v=${d.wlan} bad=${bad} cur=${cur.wlan} onChange=${patch => setPart('wlan', patch)}/>
      <button type="button" class="sec" disabled=${!!busy || !d.wlan.ssid} onClick=${test}>${t('nw.test')}</button>
    </fieldset>
    <${SaveBar} busy=${!!busy} dirty=${dirty} onDiscard=${() => { setDraft(draftOf(cfg)); setBad(''); msg.clear(); }}/>
    ${msg.node}
  </form>`;
}
