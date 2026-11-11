// Detail view of one pump: container, schedule, boost, manual dose,
// priming, calibration, consumption and settings (fine-tuning only in the pro view).
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, liveCall, liveOpen, qs } from '../lib/api.js';
import { html } from '../lib/html.js';
import { add, getLang, num, t } from '../lib/i18n.js';
import { hm, usePrefs } from '../lib/prefs.js';
import { Container, Ic, PumpStats, PumpTabs, alerts, pumpColor, pumpName, status, whenLocal } from '../lib/pumps.js';
import { reloadPumps, reloadSched, useStore } from '../lib/store.js';
import { Amount, HoldButton, Seg, ask, parseNum, useMsg } from '../lib/ui.js';
import { ScheduleCard } from './schedule.js';

add({
  en: {
    'pump.none': 'There is no pump {n}.',
    'ct.title': 'Container', 'ct.err': 'Alarm', 'ct.errs': 'Alarms', 'ct.warn': 'Warning', 'ct.warns': 'Warnings', 'ct.of': 'of {cap} ml', 'ct.refill': 'Refilled',
    'ct.edit': 'Change', 'ct.cap': 'Container size', 'ct.rem': 'Content now', 'ct.save': 'Save',
    'ct.off': 'No container tracking', 'ct.off.hint': 'Enter a container size to see the fill level and the range.',
    'ct.ok': 'Container saved', 'ct.refilled': 'Container set to full',
    'ct.cancel': 'Cancel',
    'dose.title': 'Dose now', 'dose.btn': 'Dose', 'dose.stop': 'Stop', 'dose.ok': '{ml} ml started',
    'dose.queued': 'Lock time running – the dose starts afterwards', 'dose.lock': 'Lock time to another pump until {at}',
    'dose.lock2': 'Lock time – another pump is still running', 'dose.amount': 'Amount',
    'dose.run': '{done} of {target} ml', 'tier.title': 'Speed',
    'tier.0': 'Slow', 'tier.1': 'Normal', 'tier.2': 'Fast',
    'prime.title': 'Fill tube', 'prime.hint': 'Hold the button until liquid comes out of the tube end. Taken from the container, but not counted as a dose (daily amount, limit).',
    'prime.btn': 'Hold to fill', 'prime.held': 'Filling … release to stop', 'prime.run': '{s} s · {ml} ml',
    'prime.rest': 'No live connection – stops 0.5 s after releasing.',
    'cal.title': 'Calibration', 'cal.ok': 'Calibrated: {v} ml per revolution', 'cal.none': 'Not calibrated yet – amounts use the nominal value.',
    'cal.when': 'on {d}', 'cal.s0': 'Fill the tube up to its end (also possible after Start).', 'cal.s1': 'Put the tube end into a measuring cup.', 'cal.s2': 'Start: the pump delivers about {ml} ml.',
    'cal.dlg.title': 'Calibrate pump {n}', 'cal.dlg.q1': '1 · Is the tube filled?',
    'cal.dlg.fill': 'The tube must be filled with liquid up to its end, air falsifies the measurement. If not: hold the button until liquid comes out (into a glass or back into the container).',
    'cal.dlg.q2': '2 · Tube end into the measuring cup', 'cal.dlg.cup': 'Empty the measuring cup and hold the tube end into it. The pump then delivers about {ml} ml.',
    'cal.dlg.ok': 'Filled – start', 'cal.target': 'Target amount',
    'cal.start': 'Start calibration', 'cal.running': 'Calibration running … {done} ml',
    'cal.measured': 'Measured amount', 'cal.ask': 'The pump should have delivered {ml} ml. How much is in the cup?',
    'cal.apply': 'Apply', 'cal.discard': 'Discard', 'cal.done': 'New calibration saved',
    'use.title': 'Consumption', 'use.limit': 'Daily limit',
    'use.last': 'Last run', 'use.total': 'Total dosed', 'use.tube': 'Tube', 'use.tube.v': '{h} of {lim} h',
    'use.tube.reset': 'Tube replaced', 'use.tube.ask': 'Reset the tube counter of pump {n}? A new tube conveys a little differently: calibrate the pump again afterwards.',
    'use.tube.ok': 'Tube counter reset – please calibrate again',
    'use.none': '–', 'use.off': 'off',
    'res.success': 'done', 'res.aborted': 'stopped', 'res.heartbeat_lost': 'stopped (connection)',
    'res.max_duration': 'stopped (max. time)', 'res.fault': 'driver fault', 'res.locked': 'locked (update)',
    'act.dosing': 'Dose', 'act.priming': 'Filling', 'act.calibrating': 'Calibration',
    'pset.title': 'Settings', 'pset.name': 'Name', 'pset.name.ph': 'e.g. KH or Calcium', 'pset.mode': 'Operation',
    'pset.recal': 'Calibration reminder', 'pset.recal.off': 'Off', 'pset.recal.m': '{m} mon.',
    'pset.recal.hint': 'Warns when the last calibration is older. The tube wears and the amount per turn drifts.',
    'mode.auto': 'Automatic', 'mode.manual_only': 'Manual only', 'mode.disabled': 'Off',
    'mode.hint.auto': 'Schedule and manual doses.', 'mode.hint.manual_only': 'Only manual doses, the schedule is paused.',
    'mode.hint.disabled': 'The pump does not run.',
    'pset.pro': 'Fine-tuning', 'pset.tier': 'Speed', 'pset.retract': 'Anti-drip retraction', 'pset.retract.def': 'Standard ({r} rev.)', 'pset.retract.off': 'Off',
    'pset.retract.hint': 'Pulls the liquid back a little after each dose so the tube end does not drip.',
    'pset.retract.own': 'Own value', 'pset.rev': 'revolutions', 'pset.limit': 'Daily limit', 'pset.limit.hint': '0 = no limit',
    'pset.dir': 'Direction of rotation', 'pset.dir.cw': 'Clockwise (CW)', 'pset.dir.ccw': 'Counter-clockwise (CCW)', 'pset.dir.hint': 'Changes only while the pump stands still. Check the flow direction afterwards by priming briefly.', 'pset.save': 'Save', 'pset.ok': 'Settings saved',
    'pset.thr': 'Container lasts fewer than', 'pset.warn': 'Warning', 'pset.low': 'Alarm', 'pset.thr.def': 'standard', 'pset.days': 'days',
    'pset.thr.hint': 'Days of supply from the schedule. Empty = standard from the service menu. The alarm ("almost empty", yellow LED, MQTT) must not be above the warning.',
    'pset.prohint': 'Pro settings active: {list}',
    'ph.retract': 'anti-drip', 'ph.unretract': 'preparing', 'ph.main': 'delivering', 'ph.none': '',
    'bo.title': 'Boost', 'bo.hint': 'An extra amount, e.g. to raise a value. It is spread evenly over all scheduled doses from now for the chosen days (days × 24 h) and ends by itself.',
    'bo.ml': 'Extra amount', 'bo.days': 'Spread over', 'bo.dunit': 'days', 'bo.start': 'Start boost',
    'bo.per': '≈ {ml} ml extra per day · until {end}', 'bo.started': 'Boost started',
    'bo.active': '{rest} of {total} ml still to dose', 'bo.until': 'Until {end} · {n} doses left at {ml} ml',
    'bo.wait': 'Starts with the next scheduled dose.', 'bo.stop': 'End boost',
    'bo.stop.ask': 'End the boost of pump {n}? The remaining {ml} ml are dropped.', 'bo.stopped': 'Boost ended',
    'bo.noplan': 'A boost needs a schedule – it is added to its doses.',
    'bo.mode': 'The pump is not on “Automatic” – the boost is only dosed while the schedule runs.',
    'bo.limit': 'With the boost about {p} ml per day – more than the daily limit of {l} ml. Doses above the limit are skipped.',
    'err.no_slots': 'No scheduled doses in this period',
    'chip.boost': 'Boost',
  },
  de: {
    'pump.none': 'Pumpe {n} gibt es nicht.',
    'ct.title': 'Behälter', 'ct.err': 'Alarm', 'ct.errs': 'Alarme', 'ct.warn': 'Warnung', 'ct.warns': 'Warnungen', 'ct.of': 'von {cap} ml', 'ct.refill': 'Nachgefüllt',
    'ct.edit': 'Ändern', 'ct.cap': 'Behältergröße', 'ct.rem': 'Inhalt jetzt', 'ct.save': 'Speichern',
    'ct.off': 'Keine Behälter-Erfassung', 'ct.off.hint': 'Behältergröße eintragen, um Füllstand und Reichweite zu sehen.',
    'ct.ok': 'Behälter gespeichert', 'ct.refilled': 'Behälter auf voll gesetzt',
    'ct.cancel': 'Abbrechen',
    'dose.title': 'Jetzt dosieren', 'dose.btn': 'Dosieren', 'dose.stop': 'Stopp', 'dose.ok': '{ml} ml gestartet',
    'dose.queued': 'Sperrzeit läuft – die Dosis startet danach', 'dose.lock': 'Sperrzeit zu einer anderen Pumpe bis {at}',
    'dose.lock2': 'Sperrzeit – eine andere Pumpe läuft noch', 'dose.amount': 'Menge',
    'dose.run': '{done} von {target} ml', 'tier.title': 'Geschwindigkeit',
    'tier.0': 'Langsam', 'tier.1': 'Normal', 'tier.2': 'Schnell',
    'prime.title': 'Schlauch füllen', 'prime.hint': 'Knopf halten, bis am Schlauchende Flüssigkeit austritt. Wird vom Behälter abgezogen, zählt aber nicht als Dosis (Tagesmenge, Limit).',
    'prime.btn': 'Halten zum Füllen', 'prime.held': 'Füllt … loslassen zum Stoppen', 'prime.run': '{s} s · {ml} ml',
    'prime.rest': 'Keine Live-Verbindung – stoppt 0,5 s nach dem Loslassen.',
    'cal.title': 'Kalibrierung', 'cal.ok': 'Kalibriert: {v} ml pro Umdrehung', 'cal.none': 'Noch nicht kalibriert – Mengen nutzen den Nennwert.',
    'cal.when': 'am {d}', 'cal.s0': 'Schlauch bis zum Ende füllen (geht auch nach „Starten“).', 'cal.s1': 'Schlauchende in einen Messbecher halten.', 'cal.s2': 'Starten: Die Pumpe fördert etwa {ml} ml.',
    'cal.dlg.title': 'Pumpe {n} kalibrieren', 'cal.dlg.q1': '1 · Ist der Schlauch gefüllt?',
    'cal.dlg.fill': 'Der Schlauch muss bis zum Ende mit Flüssigkeit gefüllt sein, Luft verfälscht die Messung. Falls nicht: Knopf halten, bis Flüssigkeit austritt (in ein Glas oder zurück in den Behälter).',
    'cal.dlg.q2': '2 · Schlauchende in den Messbecher', 'cal.dlg.cup': 'Messbecher leeren und das Schlauchende hineinhalten. Die Pumpe fördert dann etwa {ml} ml.',
    'cal.dlg.ok': 'Gefüllt – starten', 'cal.target': 'Zielmenge',
    'cal.start': 'Kalibrierung starten', 'cal.running': 'Kalibrierung läuft … {done} ml',
    'cal.measured': 'Gemessene Menge', 'cal.ask': 'Die Pumpe sollte {ml} ml fördern. Wie viel ist im Becher?',
    'cal.apply': 'Übernehmen', 'cal.discard': 'Verwerfen', 'cal.done': 'Neue Kalibrierung gespeichert',
    'use.title': 'Verbrauch', 'use.limit': 'Tageslimit',
    'use.last': 'Letzter Lauf', 'use.total': 'Insgesamt dosiert', 'use.tube': 'Schlauch', 'use.tube.v': '{h} von {lim} h',
    'use.tube.reset': 'Schlauch getauscht', 'use.tube.ask': 'Schlauchzähler von Pumpe {n} zurücksetzen? Ein neuer Schlauch fördert etwas anders: die Pumpe danach neu kalibrieren.',
    'use.tube.ok': 'Schlauchzähler zurückgesetzt – bitte neu kalibrieren',
    'use.none': '–', 'use.off': 'aus',
    'res.success': 'fertig', 'res.aborted': 'gestoppt', 'res.heartbeat_lost': 'gestoppt (Verbindung)',
    'res.max_duration': 'gestoppt (max. Dauer)', 'res.fault': 'Treiberfehler', 'res.locked': 'gesperrt (Update)',
    'act.dosing': 'Dosis', 'act.priming': 'Füllen', 'act.calibrating': 'Kalibrierung',
    'pset.title': 'Einstellungen', 'pset.name': 'Name', 'pset.name.ph': 'z. B. KH oder Calcium', 'pset.mode': 'Betrieb',
    'pset.recal': 'Kalibrier-Erinnerung', 'pset.recal.off': 'Aus', 'pset.recal.m': '{m} Mon.',
    'pset.recal.hint': 'Warnt, wenn die letzte Kalibrierung älter ist. Der Schlauch altert, die Menge pro Umdrehung verschiebt sich.',
    'mode.auto': 'Automatisch', 'mode.manual_only': 'Nur manuell', 'mode.disabled': 'Aus',
    'mode.hint.auto': 'Zeitplan und manuelle Dosen.', 'mode.hint.manual_only': 'Nur manuelle Dosen, der Zeitplan ruht.',
    'mode.hint.disabled': 'Die Pumpe läuft nicht.',
    'pset.pro': 'Feintuning', 'pset.tier': 'Geschwindigkeit', 'pset.retract': 'Anti-Tropf-Rückzug', 'pset.retract.def': 'Standard ({r} U)', 'pset.retract.off': 'Aus',
    'pset.retract.hint': 'Zieht die Flüssigkeit nach jeder Dosis etwas zurück, damit das Schlauchende nicht tropft.',
    'pset.retract.own': 'Eigener Wert', 'pset.rev': 'Umdrehungen', 'pset.limit': 'Tageslimit', 'pset.limit.hint': '0 = kein Limit',
    'pset.dir': 'Drehrichtung', 'pset.dir.cw': 'Rechts (CW)', 'pset.dir.ccw': 'Links (CCW)', 'pset.dir.hint': 'Nur im Stillstand änderbar. Danach die Förderrichtung prüfen (kurz entlüften).', 'pset.save': 'Speichern', 'pset.ok': 'Einstellungen gespeichert',
    'pset.thr': 'Behälter reicht weniger als', 'pset.warn': 'Warnung', 'pset.low': 'Alarm', 'pset.thr.def': 'Standard', 'pset.days': 'Tage',
    'pset.thr.hint': 'Reichweite laut Zeitplan. Leer = Standard aus dem Service-Menü. Der Alarm („fast leer“, gelbe LED, MQTT) darf nicht über der Warnung liegen.',
    'pset.prohint': 'Profi-Einstellungen aktiv: {list}',
    'ph.retract': 'Anti-Tropf', 'ph.unretract': 'Vorbereitung', 'ph.main': 'fördert', 'ph.none': '',
    'bo.title': 'Boost', 'bo.hint': 'Eine Zusatzmenge, z. B. um einen Wert anzuheben. Sie wird gleichmäßig auf alle geplanten Dosen ab jetzt für die gewählten Tage (Tage × 24 h) verteilt und endet von selbst.',
    'bo.ml': 'Zusatzmenge', 'bo.days': 'Verteilen auf', 'bo.dunit': 'Tage', 'bo.start': 'Boost starten',
    'bo.per': '≈ {ml} ml zusätzlich pro Tag · bis {end}', 'bo.started': 'Boost gestartet',
    'bo.active': 'Noch {rest} von {total} ml offen', 'bo.until': 'Bis {end} · noch {n} Dosen à {ml} ml',
    'bo.wait': 'Beginnt mit der nächsten geplanten Dosis.', 'bo.stop': 'Boost beenden',
    'bo.stop.ask': 'Boost von Pumpe {n} beenden? Die restlichen {ml} ml entfallen.', 'bo.stopped': 'Boost beendet',
    'bo.noplan': 'Ein Boost braucht einen Zeitplan – er wird auf dessen Dosen aufgeschlagen.',
    'bo.mode': 'Die Pumpe steht nicht auf „Automatisch“ – der Boost wird nur dosiert, wenn der Zeitplan läuft.',
    'bo.limit': 'Mit Boost etwa {p} ml pro Tag – mehr als das Tageslimit von {l} ml. Dosen über dem Limit werden ausgelassen.',
    'err.no_slots': 'In diesem Zeitraum sind keine Dosen geplant',
    'chip.boost': 'Boost',
  },
});

const refresh = () => { reloadPumps(); reloadSched(); };
const dec = v => String(v).replace(',', '.');

// ---------------------------------------------------------------------------

// Notices of the pump below the container card (which keeps its size for
// every pump): one card for alarms, one for warnings, only when there are any
function PumpAlerts({ p, sp }) {
  const al = alerts(p, sp);
  return ['err', 'warn'].map(cls => {
    const list = al.filter(a => a.cls === cls);
    return list.length > 0 && html`<section class=${'card notice ' + cls}>
      <h2><${Ic} n=${cls}/>${t(list.length > 1 ? 'ct.' + cls + 's' : 'ct.' + cls)}</h2>
      <ul>${list.map(a => html`<li>${a.text}</li>`)}</ul></section>`;
  });
}

// Container card, the same size for every pump
function ContainerCard({ p, sp, nowLocal }) {
  const c = p.container;
  const [edit, setEdit] = useState(false);
  const [cap, setCap] = useState('');
  const [rem, setRem] = useState('');
  const msg = useMsg();
  const open = () => {
    setCap(c.tracked ? String(Math.round(c.capacity_ml)) : '');
    setRem(c.tracked ? String(Math.round(c.remaining_ml)) : '');
    msg.clear();
    setEdit(true);
  };
  const refill = () => api('POST', '/api/v1/pumps/container?' + qs({ pump: p.pump, remaining_ml: c.capacity_ml }))
    .then(() => { msg.ok(t('ct.refilled')); refresh(); }).catch(msg.err);
  const save = e => {
    e.preventDefault();
    const capV = cap.trim() === '' ? 0 : parseNum(cap);
    const remV = rem.trim() === '' ? capV : parseNum(rem);
    if (Number.isNaN(capV) || Number.isNaN(remV) || remV > capV) { msg.err({ code: 'invalid_param' }); return; }
    api('POST', '/api/v1/pumps/container?' + qs({ pump: p.pump, capacity_ml: dec(capV), remaining_ml: dec(remV) }))
      .then(() => { msg.ok(t('ct.ok')); setEdit(false); refresh(); }).catch(msg.err);
  };
  return html`<section class="card hero" style=${{ '--pc': pumpColor(p.pump) }}>
    <div class="hero-in">
      <${Container} p=${p} sp=${sp} size="l"/>
      <div class="hero-text">
        <div class="hero-info">
          <h2>${t('ct.title')}</h2>
          <div class="hero-amt">${c.tracked ? html`
            <div class="big num">${num(c.remaining_ml, 0)} ml</div>
            <div class="mut">${t('ct.of', { cap: num(c.capacity_ml, 0) })}</div>`
          : html`<div class="mut">${t('ct.off')}</div><p class="mut small">${t('ct.off.hint')}</p>`}</div>
          <div class="pc-stats hero-stats"><${PumpStats} p=${p} sp=${sp} nowLocal=${nowLocal} boostNote/></div>
        </div>
        <div class="row">
          ${c.tracked && html`<button onClick=${refill}>${t('ct.refill')}</button>`}
          <button class="sec" onClick=${open}>${t('ct.edit')}</button>
        </div>
      </div>
    </div>
    ${edit && html`<form class="sub" onSubmit=${save}>
      <div class="form-row">
        <label>${t('ct.cap')}<${Amount} value=${cap} onInput=${setCap}/></label>
        <label>${t('ct.rem')}<${Amount} value=${rem} onInput=${setRem}/></label>
      </div>
      <div class="row"><button type="submit">${t('ct.save')}</button>
        <button type="button" class="sec" onClick=${() => setEdit(false)}>${t('ct.cancel')}</button></div>
    </form>`}
    ${msg.node}
  </section>`;
}

function Progress({ p }) {
  const pct = p.target_ml ? Math.min(100, (p.done_ml / p.target_ml) * 100) : null;
  const text = p.activity === 'priming'
    ? t('prime.run', { s: num(p.prime_s, 0), ml: num(p.done_ml, 2) })
    : t('dose.run', { done: num(p.done_ml, 2), target: num(p.target_ml, 2) });
  return html`<div class="running">
    <div class="row small"><b>${t('act.' + p.activity)}</b><span class="num">${text}</span>
      <span class="mut">${t('ph.' + p.phase)}</span></div>
    <div class=${'bar' + (pct === null ? ' indet' : '')}><i style=${{ width: (pct ?? 100) + '%' }}></i></div>
  </div>`;
}

function DoseCard({ p, pro, nowLocal }) {
  const [ml, setMl] = useState('1');
  const [tier, setTier] = useState(p.speed.tier);
  const msg = useMsg();
  const busy = p.activity !== 'idle';
  const dose = e => {
    e.preventDefault();
    const v = parseNum(ml);
    if (!(v > 0)) { msg.err({ code: 'invalid_param' }); return; }
    api('POST', '/api/v1/pumps/dose?' + qs({ pump: p.pump, ml: dec(v), tier: pro ? tier : undefined }))
      .then(j => { (j && j.status === 'queued') ? msg.ok(t('dose.queued')) : msg.ok(t('dose.ok', { ml: num(v, 2) })); refresh(); })
      .catch(err => {
        if (err.code === 'collision_lock') {
          const at = err.body && err.body.error && err.body.error.retry_local;
          msg.err(at ? t('dose.lock', { at: whenLocal(at, nowLocal) }) : t('dose.lock2'));
        } else msg.err(err);
      });
  };
  const stop = () => api('POST', '/api/v1/pumps/stop?pump=' + p.pump).then(refresh).catch(msg.err);
  return html`<section class="card"><h2>${t('dose.title')}</h2>
    <form onSubmit=${dose}>
      <div class="form-row">
        <label>${t('dose.amount')}<${Amount} value=${ml} onInput=${setMl}/></label>
        ${pro && html`<label>${t('tier.title')}<${Seg} value=${tier} onChange=${setTier}
          options=${p.speed.tiers.map((x, i) => [i, t('tier.' + i)])}/></label>`}
      </div>
      <div class="row">
        <button type="submit" disabled=${busy || p.mode === 'disabled'}>${t('dose.btn')}</button>
        ${p.activity === 'dosing' && html`<button type="button" class="danger" onClick=${stop}>■ ${t('dose.stop')}</button>`}
      </div>
    </form>
    ${p.activity === 'dosing' && html`<${Progress} p=${p}/>`}
    ${msg.node}
  </section>`;
}

// Fill button (hold) with progress: over the live connection with a dead
// man's beat, otherwise REST
function PrimeHold({ p, msg }) {
  const ws = liveOpen();
  const call = action => (ws
    ? liveCall({ type: 'prime', pump: p.pump, action })
    : api('POST', '/api/v1/pumps/prime?' + qs({ pump: p.pump, action })));
  const start = () => { msg.clear(); return call('start').catch(e => { msg.err(e); throw e; }); };
  const beat = () => call('beat').catch(e => { msg.err(e); throw e; });
  const stop = () => call('stop').catch(() => {});
  const other = p.activity !== 'idle' && p.activity !== 'priming';
  return html`<${HoldButton} cls="svc" label=${t('prime.btn')} activeLabel=${t('prime.held')}
      disabled=${other || p.mode === 'disabled'} onStart=${start} onBeat=${beat} onStop=${stop}/>
    ${p.activity === 'priming' && html`<${Progress} p=${p}/>`}
    ${!ws && html`<p class="mut small">${t('prime.rest')}</p>`}`;
}

function PrimeCard({ p }) {
  const msg = useMsg();
  return html`<section class="card"><h2>${t('prime.title')}</h2>
    <p class="mut small">${t('prime.hint')}</p>
    <${PrimeHold} p=${p} msg=${msg}/>
    ${msg.node}
  </section>`;
}

// Start of a calibration: is the tube filled (fill it right here), then the
// measuring cup
function CalibDialog({ p, ml, onStart, onClose }) {
  const msg = useMsg();
  const ok = useRef(null);
  useEffect(() => {
    const back = document.activeElement;
    if (ok.current) ok.current.focus();
    const esc = e => { if (e.key === 'Escape') onClose(); };
    addEventListener('keydown', esc);
    return () => { removeEventListener('keydown', esc); if (back && back.focus) back.focus(); };
  }, []);
  const priming = p.activity === 'priming';
  return html`<div class="overlay" onClick=${e => { if (e.target === e.currentTarget && !priming) onClose(); }}>
    <section class="card dialog" role="dialog" aria-modal="true" aria-labelledby="cal-dlg">
      <div class="row"><h2 id="cal-dlg">${t('cal.dlg.title', { n: p.pump })}</h2></div>
      <h3>${t('cal.dlg.q1')}</h3>
      <p class="mut small">${t('cal.dlg.fill')}</p>
      <${PrimeHold} p=${p} msg=${msg}/>
      <h3>${t('cal.dlg.q2')}</h3>
      <p class="mut small">${t('cal.dlg.cup', { ml: num(ml, 2) })}</p>
      ${msg.node}
      <div class="row end">
        <button type="button" class="sec" onClick=${onClose}>${t('ui.cancel')}</button>
        <button type="button" ref=${ok} disabled=${p.activity !== 'idle'} onClick=${onStart}>${t('cal.dlg.ok')}</button>
      </div>
    </section>
  </div>`;
}

function CalibCard({ p, pro }) {
  const cal = p.calibration;
  const [target, setTarget] = useState('');
  const [measured, setMeasured] = useState('');
  const [dlg, setDlg] = useState(null);  // target amount while the start dialog is open
  const msg = useMsg();
  const running = p.activity === 'calibrating';
  const open = () => {
    const v = target.trim() === '' ? undefined : parseNum(target);
    if (Number.isNaN(v) || v === 0) { msg.err({ code: 'invalid_param' }); return; }
    msg.clear();
    setDlg({ v });
  };
  const start = () => {
    const { v } = dlg;
    setDlg(null);
    api('POST', '/api/v1/pumps/calibrate?' + qs({ pump: p.pump, action: 'start', ml: v === undefined ? undefined : dec(v) }))
      .then(refresh).catch(msg.err);
  };
  const apply = e => {
    e.preventDefault();
    const v = parseNum(measured);
    if (!(v > 0)) { msg.err({ code: 'invalid_param' }); return; }
    api('POST', '/api/v1/pumps/calibrate?' + qs({ pump: p.pump, action: 'apply', measured_ml: dec(v) }))
      .then(() => { setMeasured(''); msg.ok(t('cal.done')); refresh(); }).catch(msg.err);
  };
  const discard = () => api('POST', '/api/v1/pumps/calibrate?' + qs({ pump: p.pump, action: 'cancel' }))
    .then(() => { msg.clear(); refresh(); }).catch(msg.err);
  const when = cal.calibrated_at > 0 ? new Date(cal.calibrated_at * 1000).toLocaleDateString(getLang()) : '';
  let body;
  if (running) {
    body = html`<div class="running"><div class="small">${t('cal.running', { done: num(p.done_ml, 2) })}</div>
      <div class="bar"><i style=${{ width: Math.min(100, (p.done_ml / (p.target_ml || 1)) * 100) + '%' }}></i></div></div>`;
  } else if (cal.pending) {
    body = html`<form onSubmit=${apply}>
      <p>${t('cal.ask', { ml: num(cal.pending_target_ml, 2) })}</p>
      <div class="form-row"><label>${t('cal.measured')}<${Amount} value=${measured} onInput=${setMeasured}/></label></div>
      <div class="row"><button type="submit">${t('cal.apply')}</button>
        <button type="button" class="sec" onClick=${discard}>${t('cal.discard')}</button></div>
    </form>`;
  } else {
    body = html`<ol class="steps small">
        <li>${t('cal.s0')}</li>
        <li>${t('cal.s1')}</li>
        <li>${t('cal.s2', { ml: target.trim() || num(cal.target_ml, 2) })}</li>
      </ol>
      ${pro && html`<div class="form-row"><label>${t('cal.target')}<${Amount} value=${target} onInput=${setTarget} placeholder=${num(cal.target_ml, 2)}/></label></div>`}
      <button class="sec" onClick=${open} disabled=${p.activity !== 'idle' || p.mode === 'disabled'}>${t('cal.start')}</button>`;
  }
  return html`<section class="card"><h2>${t('cal.title')}</h2>
    <p class=${cal.calibrated ? 'small' : 'small warn'}>${cal.calibrated
      ? t('cal.ok', { v: num(cal.ml_per_rev, 3) }) + (when ? ' ' + t('cal.when', { d: when }) : '') : t('cal.none')}</p>
    ${body}
    ${msg.node}
    ${dlg && html`<${CalibDialog} p=${p} ml=${dlg.v ?? cal.target_ml} onStart=${start} onClose=${() => setDlg(null)}/>`}
  </section>`;
}

function UsageCard({ p, pro }) {
  const msg = useMsg();
  const d = p.daily, tb = p.tube, last = p.last || {};
  const resetTube = async () => {
    if (!(await ask(t('use.tube.ask', { n: p.pump }), { ok: t('use.tube.reset') }))) return;
    api('POST', '/api/v1/pumps/tube?' + qs({ pump: p.pump, hours: 0 })).then(() => { msg.ok(t('use.tube.ok')); refresh(); }).catch(msg.err);
  };
  const none = html`<span class="mut">${t('use.none')}</span>`;
  const lastText = last.result
    ? `${t('act.' + last.activity)} · ${num(last.done_ml, 2)} ml · ${t('res.' + last.result)}` : none;
  return html`<section class="card usage"><h2>${t('use.title')}</h2>
    <dl class="stats">
      <dt>${t('use.limit')}</dt><dd class="num">${d.limit_ml ? num(d.limit_ml, 2) + ' ml' : t('use.off')}</dd>
      <dt>${t('use.last')}</dt><dd>${lastText}</dd>
      <dt>${t('use.total')}</dt><dd class="num">${num(p.total_ml, 2)} ml</dd>
      <dt>${t('use.tube')}</dt><dd class="num">${t('use.tube.v', { h: num(tb.hours, 1), lim: tb.limit_h })}
        <div class="bar"><i style=${{ width: Math.min(100, tb.percent) + '%', background: tb.percent >= 100 ? 'var(--err)' : undefined }}></i></div></dd>
    </dl>
    ${pro && html`<div class="row"><button class="sec" onClick=${resetTube}>${t('use.tube.reset')}</button></div>`}
    ${msg.node}
  </section>`;
}

// "YYYY-MM-DD" plus n days -> "12.11."
function dayText(date, plus = 0) {
  const d = new Date(date + 'T12:00:00Z');
  d.setUTCDate(d.getUTCDate() + plus);
  return `${d.getUTCDate()}.${d.getUTCMonth() + 1}.`;
}

function BoostCard({ p, sp, nowLocal }) {
  const [ml, setMl] = useState('');
  const [days, setDays] = useState('3');
  const [busy, setBusy] = useState(false);
  const msg = useMsg();
  const b = sp && sp.boost;
  const today = nowLocal ? nowLocal.slice(0, 10) : null;
  const run = (q, ok) => {
    setBusy(true);
    return api('POST', '/api/v1/pumps/boost?' + qs({ pump: p.pump, ...q }))
      .then(() => { msg.ok(ok); refresh(); }).catch(msg.err).finally(() => setBusy(false));
  };
  let body;
  if (b) {
    const done = Math.max(0, b.total_ml - b.remaining_ml);
    const stop = () => ask(t('bo.stop.ask', { n: p.pump, ml: num(b.remaining_ml, 2) }), { ok: t('bo.stop') })
      .then(yes => yes && run({ action: 'stop' }, t('bo.stopped')));
    body = html`<p><b class="num">${t('bo.active', { rest: num(b.remaining_ml, 2), total: num(b.total_ml, 2) })}</b></p>
      <div class="bar"><i style=${{ width: Math.min(100, (done / (b.total_ml || 1)) * 100) + '%' }}></i></div>
      <p class="small mut">${t('bo.until', { end: whenLocal(b.end_local, nowLocal), n: b.doses_left, ml: num(b.per_dose_ml, 2) })}</p>
      ${!b.active && html`<p class="small mut">${t('bo.wait')}</p>`}
      <div class="row"><button type="button" class="sec" onClick=${stop} disabled=${busy}>${t('bo.stop')}</button></div>`;
  } else {
    const v = parseNum(ml), n = parseInt(days, 10);
    const ok = v > 0 && n >= 1 && n <= 30;
    const planned = sp ? sp.planned_today_ml : 0;
    const noPlan = sp && !planned && !sp.next;
    const lim = p.daily.limit_ml;
    const submit = e => {
      e.preventDefault();
      if (!ok) { msg.err({ code: 'invalid_param' }); return; }
      run({ ml: dec(v), days: n }, t('bo.started')).then(() => setMl(''));
    };
    body = html`<p class="small mut">${t('bo.hint')}</p>
      <form novalidate onSubmit=${submit}>
        <div class="form-row">
          <label>${t('bo.ml')}<${Amount} value=${ml} onInput=${setMl}/></label>
          <label>${t('bo.days')}<${Amount} value=${days} onInput=${setDays} unit=${t('bo.dunit')}/></label>
        </div>
        ${ok && today && html`<p class="small">${t('bo.per', { ml: num(v / n, 2), end: dayText(today, n) + ' ' + hm(nowLocal.slice(11, 16)) })}</p>`}
        ${ok && lim > 0 && planned + v / n > lim && html`<p class="small warn">${t('bo.limit', { p: num(planned + v / n, 2), l: num(lim, 2) })}</p>`}
        ${noPlan && html`<p class="small warn">${t('bo.noplan')}</p>`}
        <div class="row"><button type="submit" disabled=${busy || !ok}>${t('bo.start')}</button></div>
      </form>`;
  }
  return html`<section class="card boost"><h2>${t('bo.title')}</h2>
    ${p.mode !== 'auto' && html`<p class="small warn">${t('bo.mode')}</p>`}
    ${body}
    ${msg.node}
  </section>`;
}

// Pro values that differ from the defaults (shown as a hint in the simple view)
function proActive(p) {
  const out = [];
  if (p.speed.tier !== 1) out.push(t('pset.tier') + ': ' + t('tier.' + p.speed.tier));
  if (p.retract.setting !== 'off') out.push(t('pset.retract') + ': ' + num(p.retract.rev, 2) + ' ' + t('pset.rev'));
  if (p.daily.limit_ml) out.push(t('pset.limit') + ': ' + num(p.daily.limit_ml, 2) + ' ml');
  const c = p.container;
  if (c.own_warn_days || c.own_low_days) out.push(t('pset.thr') + ': ' + c.warn_days + ' / ' + c.low_days + ' ' + t('pset.days'));
  if (p.direction === 'ccw') out.push(t('pset.dir') + ': ' + t('pset.dir.ccw'));
  return out;
}

// Off, 3, 6, 9, 12 months (and a value set elsewhere, up to 24)
function recalOptions(cur) {
  const ms = [0, 3, 6, 9, 12];
  if (!ms.includes(cur)) ms.push(cur);
  return ms.map(m => [m, m ? t('pset.recal.m', { m }) : t('pset.recal.off')]);
}

function SettingsCard({ p, pro }) {
  const [f, setF] = useState(null);
  const msg = useMsg();
  // Form values from the device; again after switching pump
  useEffect(() => {
    setF({
      name: p.name, mode: p.mode, tier: p.speed.tier, retract: p.retract.setting,
      retractRev: String(p.retract.setting === 'own' ? p.retract.rev : p.retract.global_rev), limit: p.daily.limit_ml ? String(p.daily.limit_ml) : '0',
      dir: p.direction, recal: p.calibration.remind_months,
      warn: p.container.own_warn_days ? String(p.container.own_warn_days) : '',
      low: p.container.own_low_days ? String(p.container.own_low_days) : '',
    });
    msg.clear();
  }, [p.pump]);
  if (!f) return null;
  const set = patch => setF(prev => ({ ...prev, ...patch }));
  const save = e => {
    e.preventDefault();
    const q = { pump: p.pump, name: f.name.trim(), mode: f.mode, remind_months: f.recal };
    if (pro) {
      const lim = f.limit.trim() === '' ? 0 : parseNum(f.limit);
      const rr = f.retract === 'own' ? parseNum(f.retractRev) : 0;
      const pc = v => (v.trim() === '' ? 0 : parseNum(v));
      const w = pc(f.warn), l = pc(f.low);
      const pcBad = v => !(Number.isInteger(v) && v >= 0 && v <= 60);
      if (Number.isNaN(lim) || Number.isNaN(rr) || rr > 2 || (f.retract === 'own' && !(rr > 0)) || pcBad(w) || pcBad(l)) { msg.err({ code: 'invalid_param' }); return; }
      Object.assign(q, { tier: f.tier, daily_limit_ml: dec(lim), retract_rev: f.retract === 'own' ? dec(rr) : f.retract, direction: f.dir,
        warn_days: w, low_days: l });
    }
    api('POST', '/api/v1/pumps/settings?' + qs(q)).then(() => { msg.ok(t('pset.ok')); refresh(); }).catch(msg.err);
  };
  const hint = !pro && proActive(p);
  return html`<section class="card"><h2>${t('pset.title')}</h2>
    <form onSubmit=${save}>
      <label class="field">${t('pset.name')}
        <input type="text" maxlength="23" value=${f.name} placeholder=${t('pset.name.ph')} onInput=${e => set({ name: e.target.value })}/></label>
      <div class="field"><span class="lbl">${t('pset.mode')}</span>
        <${Seg} value=${f.mode} onChange=${v => set({ mode: v })} options=${['auto', 'manual_only', 'disabled'].map(m => [m, t('mode.' + m)])}/>
        <span class="mut small">${t('mode.hint.' + f.mode)}</span></div>
      <div class="field"><span class="lbl">${t('pset.recal')}</span>
        <${Seg} value=${f.recal} onChange=${v => set({ recal: v })} options=${recalOptions(f.recal)}/>
        <span class="mut small">${t('pset.recal.hint')}</span></div>
      ${pro && html`<fieldset class="pro"><legend>${t('pset.pro')}</legend>
        <div class="field"><span class="lbl">${t('pset.tier')}</span>
          <${Seg} value=${f.tier} onChange=${v => set({ tier: v })}
            options=${p.speed.tiers.map((x, i) => [i, `${t('tier.' + i)} · ${num(x.ml_min, 0)} ml/min`])}/></div>
        <div class="field"><span class="lbl">${t('pset.retract')}</span>
          <${Seg} value=${f.retract} onChange=${v => set({ retract: v })} options=${[['off', t('pset.retract.off')],
            ['global', t('pset.retract.def', { r: num(p.retract.global_rev, 2) })], ['own', t('pset.retract.own')]]}/>
          ${f.retract === 'own' && html`<${Amount} value=${f.retractRev} onInput=${v => set({ retractRev: v })} unit=${t('pset.rev')}/>`}
          <span class="mut small">${t('pset.retract.hint')}</span></div>
        <label class="field">${t('pset.limit')}<${Amount} value=${f.limit} onInput=${v => set({ limit: v })}/>
          <span class="mut small">${t('pset.limit.hint')}</span></label>
        <div class="field"><span class="lbl">${t('pset.thr')}</span>
          <div class="form-row">
            <label>${t('pset.warn')}<${Amount} value=${f.warn} onInput=${v => set({ warn: v })} unit=${t('pset.days')}
              placeholder=${p.container.own_warn_days ? t('pset.thr.def') : String(p.container.warn_days)}/></label>
            <label>${t('pset.low')}<${Amount} value=${f.low} onInput=${v => set({ low: v })} unit=${t('pset.days')}
              placeholder=${p.container.own_low_days ? t('pset.thr.def') : String(p.container.low_days)}/></label>
          </div>
          <span class="mut small">${t('pset.thr.hint')}</span></div>
        <div class="field"><span class="lbl">${t('pset.dir')}</span>
          <${Seg} value=${f.dir} onChange=${v => set({ dir: v })} options=${[['cw', t('pset.dir.cw')], ['ccw', t('pset.dir.ccw')]]}/>
          <span class="mut small">${t('pset.dir.hint')}</span></div>
      </fieldset>`}
      <div class="row"><button type="submit">${t('pset.save')}</button></div>
    </form>
    ${hint && hint.length > 0 && html`<p class="prohint small">${t('pset.prohint', { list: hint.join(' · ') })}</p>`}
    ${msg.node}
  </section>`;
}

export function PumpView({ pump }) {
  const s = useStore();
  const prefs = usePrefs();
  if (!s.pumps) return html`<div class="empty">…</div>`;
  const p = s.pumps[pump - 1];
  if (!p) return html`<div class="empty">${t('pump.none', { n: pump })}</div>`;
  const sp = s.sched ? s.sched.pumps[pump - 1] : null;
  const nowLocal = s.sched && s.sched.now_local;
  const pro = prefs.mode === 'pro';
  const st = status(p);
  return html`<${PumpTabs} current=${pump} pumps=${s.pumps}/>
    <div class="page-head" style=${{ '--pc': pumpColor(p.pump) }}>
      <span class="badge big-badge">${p.pump}</span><h1>${pumpName(p)}</h1><span class="spacer"></span>
      <span class=${'chip ' + st.cls}>${t(st.key)}</span>
      ${sp && sp.boost && html`<span class="chip acc">${t('chip.boost')} +${num(sp.boost.remaining_ml, 2)} ml</span>`}
    </div>
    <div class="detail" key=${pump} style=${{ '--pc': pumpColor(p.pump) }}>
      <div class="col">
        <${ContainerCard} p=${p} sp=${sp} nowLocal=${nowLocal}/>
        <${PumpAlerts} p=${p} sp=${sp}/>
        <${ScheduleCard} p=${p} pro=${pro} nowLocal=${nowLocal}/>
        <${BoostCard} p=${p} sp=${sp} nowLocal=${nowLocal}/>
        <${UsageCard} p=${p} pro=${pro}/>
      </div>
      <div class="col">
        <${DoseCard} p=${p} pro=${pro} nowLocal=${nowLocal}/>
        <${PrimeCard} p=${p}/>
        <${CalibCard} p=${p} pro=${pro}/>
        <${SettingsCard} p=${p} pro=${pro}/>
      </div>
    </div>`;
}
