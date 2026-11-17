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
