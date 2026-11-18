// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// History: daily amounts for everyone; in the pro view the event list with filters, paging and
// CSV export. The mock generates 60 days of events; the tests here use kinds that no other test
// adds (alarms, container, system), so their counts are fixed.
import { readFileSync } from 'node:fs';
import { expect, open, proMode, test } from './fixtures.js';

test('history: period and chart type', async ({ page }) => {
  await open(page, '#/history');
  const range = page.getByRole('radio', { name: '14 days' });
  await expect(range).toHaveAttribute('aria-checked', 'true');
  const chart = page.getByRole('img', { name: 'KH' });
  // date labels on the axis: dd/mm or mm/dd
  const labels = async () => (await chart.textContent()).match(/\d\d\/\d\d/g)?.length ?? 0;
  const at14 = await labels();
  await page.getByRole('radio', { name: '7 days' }).click();
  await expect(page.getByRole('radio', { name: '7 days' })).toHaveAttribute('aria-checked', 'true');
  await expect.poll(labels).toBeLessThan(at14);
  await page.getByRole('radio', { name: 'Line' }).click();
  await expect(page.getByRole('radio', { name: 'Line' })).toHaveAttribute('aria-checked', 'true');
  await expect(chart).toBeVisible();

  await page.getByText('Show as table').click();
  await expect(page.getByRole('table').first()).toBeVisible();
});

const events = page => page.locator('section.card')
  .filter({ has: page.getByRole('heading', { name: 'Events', level: 2, exact: true }) });

test('events (pro view): paging, filters, empty period', async ({ page }) => {
  await open(page, '#/history');
  await expect(events(page)).toHaveCount(0);  // simple view
  await proMode(page);
  const c = events(page);
  const rows = c.locator('.hi-list > li');
  const pump = c.getByRole('combobox', { name: 'All pumps' });
  const kind = c.getByRole('combobox', { name: 'Events' });
  await expect(pump.locator('option:checked')).toHaveText('All pumps');
  await expect(kind.locator('option:checked')).toHaveText('All');
  await expect(c.getByRole('radio', { name: '7 days' })).toHaveAttribute('aria-checked', 'true');

  // 50 per page, newest first
  await expect(rows).toHaveCount(50);
  await c.getByRole('button', { name: 'Load more' }).click();
  await expect.poll(() => rows.count()).toBeGreaterThan(50);

  await kind.selectOption({ label: 'Alarm' });
  await expect(rows).toHaveCount(3);
  await expect(rows.locator('.hi-k')).toHaveText(['Alarm', 'Alarm', 'Alarm']);
  await expect(rows.locator('.hi-c')).toHaveText(Array(3).fill('container almost empty'));
  await expect(rows.locator('.hi-c').first()).toHaveClass(/warn/);
  await expect(c.getByRole('button', { name: 'Load more' })).toHaveCount(0);
  await pump.selectOption({ label: 'Calcium' });
  await expect(rows).toHaveCount(2);
  await expect(rows.locator('.hi-p')).toHaveText(['Calcium', 'Calcium']);

  // the container was refilled 12 days ago
  await pump.selectOption({ label: 'All pumps' });
  await kind.selectOption({ label: 'Container' });
  await expect(c.getByText('No events in this period.')).toBeVisible();
  await c.getByRole('radio', { name: '30 days' }).click();
  await expect(rows).toHaveCount(1);
  await expect(rows.locator('.hi-d')).toHaveText(/^2,?000 of 2,?000 ml$/);
  await expect(rows.locator('.hi-c')).toHaveText('refilled');

  // device events: restart, update with versions
  await kind.selectOption({ label: 'System' });
  await c.getByRole('radio', { name: '7 days' }).click();
  await expect(rows.first().locator('.hi-p')).toHaveText('Device');
  await expect(rows.first().locator('.hi-d')).toHaveText('restart');
  await expect(rows.filter({ hasText: 'firmware updated' })).toHaveCount(0);  // 22 days ago
  await c.getByRole('radio', { name: '30 days' }).click();
  await expect(rows.filter({ hasText: 'firmware updated' }).locator('.hi-d')).toHaveText('0.0.9 → 0.1.0');
});

test('events: a pump from the address, CSV export with the filter', async ({ page }) => {
  await page.goto('/');
  await proMode(page);
  await open(page, '#/history/2');
  const c = events(page);
  await expect(c.getByRole('combobox', { name: 'All pumps' }).locator('option:checked')).toHaveText('Calcium');
  await c.getByRole('combobox', { name: 'Events' }).selectOption({ label: 'Alarm' });
  const csv = c.getByRole('link', { name: 'CSV export' });
  await expect(csv).toHaveAttribute('href', '/api/v1/history.csv?pump=2&kind=alarm&days=7');

  const dl = page.waitForEvent('download');
  await csv.click();
  const lines = readFileSync(await (await dl).path(), 'utf8').trim().split('\r\n');
  expect(lines[0]).toBe('utc,local,kind,pump,source,code,target_ml,done_ml,value');
  expect(lines.slice(1)).toHaveLength(2);
  for (const l of lines.slice(1)) expect(l).toMatch(/^\d+,[\d-]+ [\d:]+,alarm,2,,container_low,/);
});

test('events: a late answer does not overwrite a newer filter', async ({ page }) => {
  await page.goto('/');
  await proMode(page);
  await open(page, '#/history');
  const c = events(page);
  await expect(c.locator('.hi-list > li')).toHaveCount(50);
  // the alarm list answers late, after the container list
  await page.route(/\/api\/v1\/history\?.*kind=alarm/, async route => {
    await new Promise(r => setTimeout(r, 1500));
    await route.continue();
  });
  const kind = c.getByRole('combobox', { name: 'Events' });
  const late = page.waitForResponse(/kind=alarm/);
  await kind.selectOption({ label: 'Alarm' });
  await kind.selectOption({ label: 'Container' });
  await expect(c.getByText('No events in this period.')).toBeVisible();
  await late;
  await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
  await expect(c.getByText('No events in this period.')).toBeVisible();
  await expect(c.locator('.hi-list > li')).toHaveCount(0);
});

// Every kind of event with its detail text and colour; the list comes in the firmware's format
test('events: the text of every kind', async ({ page }) => {
  const ev = (kind, pump, source, code, extra = {}) => ({ utc: 1794000000, local: '2026-11-07 08:30:00', kind, pump, source, code, ...extra });
  const rows = [
    [ev('dose', 1, 'schedule', 'success', { target_ml: 2.5, done_ml: 2.5 }), 'Dose · schedule', '2.50 ml', 'done', 'mut'],
    [ev('dose', 2, 'manual', 'aborted', { target_ml: 5, done_ml: 1.25 }), 'Dose · manual', '1.25 of 5.00 ml', 'stopped', 'err'],
    [ev('dose', 1, 'api', 'heartbeat_lost', { target_ml: 4, done_ml: 1 }), 'Dose · API', '1.00 of 4.00 ml', 'stopped (connection)', 'err'],
    [ev('prime', 3, 'manual', 'aborted', { target_ml: 10, done_ml: 3 }), 'Filling · manual', '3.00 ml', 'stopped', 'err'],
    [ev('calib_run', 1, 'manual', 'success', { target_ml: 10, done_ml: 10 }), 'Calibration run · manual', '10.00 ml', 'done', 'mut'],
    [ev('skip', 1, 'schedule', 'missed', { count: 3 }), 'Skipped', '3 doses', 'missed (device off)', 'warn'],
    [ev('skip', 2, 'schedule', 'daily_limit', { target_ml: 2, count: 1 }), 'Skipped', '2.00 ml', 'daily limit reached', 'warn'],
    [ev('container', 2, 'manual', 'set', { remaining_ml: 1500, capacity_ml: 2000 }), 'Container · manual', /^1,?500 of 2,?000 ml$/, 'changed', 'mut'],
    [ev('calibration', 1, 'manual', 'set', { ul_per_rev: 245, measured_ml: 9.87, target_ml: 10 }), 'Calibration · manual', '0.245 ml/rev. · measured 9.87 of 10.00 ml', 'changed', 'mut'],
    [ev('tube', 3, 'manual', 'set', { hours: 12 }), 'Tube · manual', '12 h', 'changed', 'mut'],
    [ev('boost', 1, 'manual', 'started', { total_ml: 6, days: 3 }), 'Boost · manual', '6.00 ml over 3 days', 'started', 'mut'],
    [ev('boost', 2, 'mqtt', 'started', { total_ml: 4, doses: 4 }), 'Boost · MQTT', '4.00 ml over 4 doses', 'started', 'mut'],
    [ev('boost', 1, 'manual', 'cancelled', { total_ml: 6, done_ml: 1 }), 'Boost · manual', '1.00 of 6.00 ml', 'cancelled', 'mut'],
    [ev('mode', 3, 'api', 'set', { mode: 'manual_only' }), 'Mode · API', 'Manual only', 'changed', 'mut'],
    [ev('schedule', 2, 'manual', 'set', { daily_ml: 12, doses: 6 }), 'Schedule · manual', '12.00 ml/day · 6 doses', 'changed', 'mut'],
    [ev('schedule', 2, 'manual', 'cleared'), 'Schedule · manual', '', 'deleted', 'mut'],
    [ev('locks', 0, 'manual', 'set', { rules: 2 }), 'Lock rules · manual', '2 rules', 'changed', 'mut'],
    [ev('settings', 0, 'api', 'key_added'), 'Settings · API', '', 'API key added', 'mut'],
    [ev('alarm', 0, '', 'supply_low'), 'Alarm', '', '24 V supply too low', 'err'],
    [ev('alarm_end', 2, '', 'container_low'), 'Alarm over', '', 'container almost empty', 'ok'],
    [ev('system', 0, '', 'boot', { reason: 'crash' }), 'System', 'after a crash', 'started', 'warn'],
    [ev('system', 0, '', 'boot', { reason: 'power_on' }), 'System', 'power on', 'started', 'mut'],
    [ev('system', 0, '', 'channels', { pumps: [1, 2, 3] }), 'System', 'pumps 1, 2, 3', 'fitted pumps changed', 'mut'],
    [ev('system', 0, '', 'updated', { version: '0.2.0' }), 'System', '0.2.0', 'firmware updated', 'mut'],
  ];
  await page.route(/\/api\/v1\/history\?/, route => route.fulfill({ json: { events: rows.map(r => r[0]), more: false } }));
  await page.goto('/');
  await proMode(page);
  await open(page, '#/history');
  const list = events(page).locator('.hi-list > li');
  await expect(list).toHaveCount(rows.length);
  for (const [i, [e, kind, detail, code, cls]] of rows.entries()) {
    const li = list.nth(i);
    await expect(li.locator('.hi-t')).toHaveText(/^07\.11\. /);
    await expect(li.locator('.hi-p')).toHaveText(e.pump ? /\S/ : 'Device');
    await expect(li.locator('.hi-k')).toHaveText(kind);
    await expect(li.locator('.hi-d')).toHaveText(detail);
    await expect(li.locator('.hi-c')).toHaveText(code);
    await expect(li.locator('.hi-c')).toHaveClass(new RegExp('\\b' + cls + '\\b'));
  }
});

// Point at a day: the line above the chart shows its values, the chart itself does not move
test('history: the values of a day under the pointer', async ({ page }) => {
  const days = ['01', '02', '03', '04', '05', '06', '07'].map((d, k) => ({
    date: '2026-11-' + d, ml: [[2, 4, 0, 6, 8, 3, 7.5][k], 0, 0, 0, 0], doses: [[2, 2, 0, 3, 4, 2, 6][k], 0, 0, 0, 0],
    skipped: [k === 2 ? 2 : 0, 0, 0, 0, 0],
  }));
  let daily = { keep_days: 400, today: '2026-11-07', days };
  await page.route(/\/api\/v1\/history\/daily\?/, route => route.fulfill({ json: daily }));
  await open(page, '#/history');
  await page.getByRole('radio', { name: '7 days' }).click();
  await page.getByRole('radio', { name: 'Bars' }).click();
  const hc = page.locator('.hc').filter({ has: page.locator('.hc-head', { hasText: /^KH$/ }) });
  const svg = hc.locator('svg');
  const read = hc.locator('.hc-read');
  await expect(svg).toHaveAttribute('aria-label', 'KH');
  await expect(read).toHaveText('');
  await expect(page.getByText('Dot under the axis: doses were skipped that day.')).toBeVisible();
  await expect(page.getByText('Kept for 400 days.')).toBeVisible();
  await expect(hc.locator('.hc-skip')).toHaveCount(1);

  const box = await svg.boundingBox();
  const slot = (box.width - 34 - 8) / 7;   // margins of the plot: 34 left, 8 right
  const before = await read.boundingBox();
  await svg.hover({ position: { x: 34 + slot * 6.5, y: 60 } });
  await expect(read).toHaveText('today: 7.50 ml · 6 doses');
  await expect(svg).toHaveAttribute('aria-label', 'KH: today: 7.50 ml · 6 doses');
  await expect(hc.locator('.hc-hl')).toHaveCount(1);
  expect(await read.boundingBox()).toEqual(before);
  await svg.hover({ position: { x: 34 + slot * 2.5, y: 60 } });
  await expect(read).toHaveText(/: 0\.00 ml · 0 doses · 2 skipped$/);
  await page.mouse.move(0, 0);
  await expect(read).toHaveText('');
  await expect(hc.locator('.hc-hl')).toHaveCount(0);

  // the line runs from axis to axis: the first day sits on the y axis
  await page.getByRole('radio', { name: 'Line' }).click();
  await svg.hover({ position: { x: 34 + 2, y: 60 } });
  await expect(read).toHaveText(/: 2\.00 ml · 2 doses$/);
  await expect(hc.locator('.hc-cross')).toHaveCount(1);

  await page.getByText('Show as table').click();
  const table = page.locator('details.hc-table table');
  await expect(table.locator('tbody tr')).toHaveCount(7);
  await expect(table.locator('tbody tr').first().locator('td').nth(1)).toHaveText('7.50');
  await expect(table.locator('tbody tr').nth(4).locator('td').nth(1)).toHaveText('0.00 (2×)');

  // no valid time on the device: no chart, a note instead
  daily = { keep_days: 400, today: '', days: [] };
  await page.getByRole('radio', { name: '14 days' }).click();
  await expect(page.getByText('The device has no valid time – daily values follow once the time is set.')).toBeVisible();
  await expect(page.locator('.hc')).toHaveCount(0);
});
