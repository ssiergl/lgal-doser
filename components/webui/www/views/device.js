// Device card (settings page): the name of the device, the host name that
// follows from it (own host name in the pro view) and the device pages.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, t } from '../lib/i18n.js';
import { reloadInfo } from '../lib/store.js';
import { SaveBar, useMsg } from '../lib/ui.js';

add({
  en: {
    'dev.name': 'Device name', 'dev.name.ph': 'e.g. Reef tank', 'dev.name.h': 'Shown in the header, in Home Assistant and in the network.',
    'dev.reach': 'Reachable as', 'dev.own': 'Own host name', 'dev.own.h': 'Empty = from the device name. Letters a–z, digits and “-”.',
    'dev.saved': 'Saved. The router shows the new name after the next address renewal or a restart.',
    'dev.long': 'At most 32 bytes (umlauts count twice)', 'dev.bad.host': 'Host name: only a–z, 0–9 and “-”, not at the start or end',
  },
  de: {
    'dev.name': 'Gerätename', 'dev.name.ph': 'z. B. Riffbecken', 'dev.name.h': 'Erscheint in der Kopfzeile, in Home Assistant und im Netzwerk.',
    'dev.reach': 'Erreichbar als', 'dev.own': 'Eigener Hostname', 'dev.own.h': 'Leer = aus dem Gerätenamen. Buchstaben a–z, Ziffern und „-“.',
    'dev.saved': 'Gespeichert. Der Router zeigt den neuen Namen nach der nächsten Adressvergabe oder einem Neustart.',
    'dev.long': 'Höchstens 32 Bytes (Umlaute zählen doppelt)', 'dev.bad.host': 'Hostname: nur a–z, 0–9 und „-“, nicht am Anfang oder Ende',
  },
});

const kMax = 32;
const bytes = s => new TextEncoder().encode(s).length;
const kLatin = { ä: 'ae', ö: 'oe', ü: 'ue', ß: 'ss', æ: 'ae' };

// Same rule as the firmware (lgal/device_name.h): lower case, umlauts as
// ae/oe/ue/ss, accents dropped, everything else becomes one "-"
export function slug(name) {
  let out = '', dash = false;
  for (const ch of name.toLowerCase()) {
    let s = null;
    if (/[a-z0-9]/.test(ch)) s = ch;
    else if (kLatin[ch]) s = kLatin[ch];
    else if (/[à-ÿ]/.test(ch)) { const b = ch.normalize('NFD')[0]; if (/[a-z]/.test(b)) s = b; }
    if (s === null) { dash = true; continue; }
    if (dash && out) out += '-';
    dash = false;
    out += s;
  }
  return out.slice(0, kMax).replace(/-+$/, '');
}
const validHost = h => h.length <= kMax && /^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/.test(h);

export function DeviceCard({ pro }) {
  const [doc, setDoc] = useState(null);
  const [d, setD] = useState(null);
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const take = j => { setDoc(j); setD({ name: j.name, own: j.own_hostname }); };
  useEffect(() => { api('GET', '/api/v1/system/device').then(take).catch(e => msg.err(e)); }, []);

  const links = html`<ul class="links">
    <li><a href="/setup">${t('set.setup')}</a></li>
    <li><a href="#/maintenance">${t('set.maint')}</a></li>
  </ul>`;
  if (!doc) return html`<section class="card"><h2>${t('set.device')}</h2>${msg.node}${links}</section>`;

  const name = d.name.trim(), own = d.own.trim().toLowerCase();
  const host = own || slug(name) || doc.default_hostname;
  const dirty = name !== doc.name || own !== doc.own_hostname;
  const submit = e => {
    e.preventDefault();
    if (bytes(name) > kMax) return msg.err(t('dev.long'));
    if (own && !validHost(own)) return msg.err(t('dev.bad.host'));
    setBusy(true);
    api('PUT', '/api/v1/system/device', { name, own_hostname: own })
      .then(j => { take(j); msg.ok(t('dev.saved')); reloadInfo(); })
      .catch(x => msg.err(x)).finally(() => setBusy(false));
  };
  return html`<form class="card" novalidate onSubmit=${submit}>
    <h2>${t('set.device')}</h2>
    <label class="field">${t('dev.name')}
      <input type="text" maxlength="32" autocomplete="off" placeholder=${t('dev.name.ph')} value=${d.name}
        class=${bytes(name) > kMax ? 'bad' : ''} onInput=${e => setD(o => ({ ...o, name: e.target.value }))}/></label>
    <p class="small mut">${t('dev.name.h')}</p>
    ${pro && html`<label class="field">${t('dev.own')}
      <input type="text" maxlength="32" autocomplete="off" spellcheck="false" placeholder=${slug(name) || doc.default_hostname}
        value=${d.own} class=${own && !validHost(own) ? 'bad' : ''} onInput=${e => setD(o => ({ ...o, own: e.target.value }))}/></label>
      <p class="small mut">${t('dev.own.h')}</p>`}
    <dl class="stats net"><dt>${t('dev.reach')}</dt><dd class="mono">${host}.local</dd></dl>
    <${SaveBar} busy=${busy} dirty=${dirty} onDiscard=${() => { take(doc); msg.clear(); }}/>
    ${msg.node}
    ${links}
  </form>`;
}
