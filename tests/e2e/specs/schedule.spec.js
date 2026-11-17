// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { readFileSync } from 'node:fs';
import { expect, open, proMode, test } from './fixtures.js';

const form = page => page.locator('form.plan');
const io = page => page.locator('.sched-io');
const scheduleOf = async (page, n) => (await (await page.request.get('/api/v1/pumps/schedule?pump=' + n)).json());

// Saves and waits for the answer: until then the form still holds the old draft
async function save(page) {
  const done = page.waitForResponse(r => r.url().includes('/api/v1/pumps/schedule') && r.request().method() === 'PUT');
  await form(page).getByRole('button', { name: 'Save' }).click();
  expect((await done).ok()).toBe(true);
  await expect(form(page).getByRole('button', { name: 'Save' })).toBeDisabled();
}

// Saves the schedule of pump n as a file and puts it back after the test
async function keep(page, n) {
  const dl = page.waitForEvent('download');
  await io(page).getByRole('button', { name: 'Export' }).click();
  const file = await (await dl).path();
  return async () => {
    await page.goto('/#/pump/' + n);
    await importFile(page, file, true);
  };
}

// The file input is disabled while a save runs; wait for it, or the file is dropped
async function pick(page, file) {
  const input = io(page).locator('input[type=file]');
  await expect(input).toBeEnabled();
  await input.setInputFiles(file);
}

async function importFile(page, file, yes) {
  await pick(page, file);
  const ask = page.getByRole('alertdialog');
  await expect(ask).toBeVisible();
  await ask.getByRole('button', { name: yes ? 'Import' : 'Cancel' }).click();
  if (yes) await expect(io(page).getByRole('status')).toHaveText('Schedule imported');
}

test('single times: add, change, remove, weekdays, catch-up', async ({ page, cleanup }) => {
  await open(page, '#/pump/5');
  await proMode(page);
  const restore = cleanup(await keep(page, 5));
  const f = form(page);
  await expect(f.getByRole('radio', { name: 'Single times' })).toHaveAttribute('aria-checked', 'true');
  await expect(f.getByRole('textbox', { name: 'Time 1' })).toHaveValue('12:00');
  await f.getByRole('button', { name: '+ Add time' }).click();
  await expect(f.getByRole('textbox', { name: 'Time 2' })).toHaveValue('13:00');  // one hour after the last
  await f.getByRole('textbox', { name: 'Amount 2 in ml' }).fill('1.5');
  await f.getByRole('textbox', { name: 'Time 2' }).fill('07:30');
  await expect(f.getByText('2 times · 3.50 ml per day')).toBeVisible();

  await f.getByRole('radio', { name: 'Weekdays' }).click();
  for (const day of ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']) {
    const b = f.getByRole('button', { name: day, exact: true });
    const want = day === 'Tue' || day === 'Sat' ? 'true' : 'false';
    if ((await b.getAttribute('aria-pressed')) !== want) await b.click();
    await expect(b).toHaveAttribute('aria-pressed', want);
  }
  await f.getByRole('radio', { name: 'Catch up' }).click();
  await f.getByRole('textbox', { name: 'Catch-up window in minutes' }).fill('45');
  await save(page);

  const s = await scheduleOf(page, 5);
  expect(s.kind).toBe('times');
  expect(s.times.map(x => [x.time, x.ml])).toEqual([['07:30', 1.5], ['12:00', 2]]);  // sorted by time
  expect(s.days).toMatchObject({ mode: 'weekdays', weekdays: ['tue', 'sat'] });
  expect(s).toMatchObject({ power_loss: 'catch_up_window', catch_up_min: 45 });

  await page.reload();
  await expect(form(page).getByRole('textbox', { name: 'Time 1' })).toHaveValue('07:30');
  await form(page).getByRole('button', { name: 'Remove time' }).first().click();
  await expect(form(page).getByText('1 times · 2.00 ml per day')).toBeVisible();
  await save(page);
  expect((await scheduleOf(page, 5)).times).toHaveLength(1);
  await restore();
  expect((await scheduleOf(page, 5)).times.map(x => x.time)).toEqual(['12:00']);
});

test('every N days and an invalid amount', async ({ page, cleanup }) => {
  await open(page, '#/pump/5');
  await proMode(page);
  const restore = cleanup(await keep(page, 5));
  const f = form(page);
  await f.getByRole('radio', { name: 'Every N days' }).click();
  await f.getByRole('textbox', { name: 'Every days' }).fill('3');
  await save(page);
  expect((await scheduleOf(page, 5)).days).toMatchObject({ mode: 'interval', every: 3 });

  // an amount of 0 is not saved
  await f.getByRole('textbox', { name: 'Amount 1 in ml' }).fill('0');
  await f.getByRole('button', { name: 'Save' }).click();
  await expect(f.locator('.msg.err, .bad').first()).toBeVisible();
  expect((await scheduleOf(page, 5)).times[0].ml).toBe(2);
  await f.getByRole('button', { name: 'Discard' }).click();
  await expect(f.getByRole('textbox', { name: 'Amount 1 in ml' })).toHaveValue('2');
  await restore();
  expect((await scheduleOf(page, 5)).days.mode).toBe('daily');
});

test('export a schedule and import it into another pump', async ({ page, cleanup }, info) => {
  await open(page, '#/pump/3');
  await proMode(page);  // export and import are part of the pro view
  const dl = page.waitForEvent('download');
  await io(page).getByRole('button', { name: 'Export' }).click();
  const download = await dl;
  expect(download.suggestedFilename()).toBe('doser-pump3-magnesium-schedule.json');
  const file = await download.path();
  const doc = JSON.parse(readFileSync(file, 'utf8'));
  expect(doc).toMatchObject({ format: 'lgal-doser-schedule', version: 1, pump: 3, name: 'Magnesium' });
  expect(doc.schedule.kind).toBe('times');

  // pump 5 allows 5 ml a day, the plan of pump 3 has 7.5 ml on its dosing days (Mon, Wed, Fri, Sat)
  const limit = (await (await page.request.get('/api/v1/pumps')).json()).pumps[4].daily.limit_ml;
  cleanup(() => page.request.post(`/api/v1/pumps/settings?pump=5&daily_limit_ml=${limit}`));
  await page.request.post('/api/v1/pumps/settings?pump=5&daily_limit_ml=0');
  await page.goto('/#/pump/5');
  const restore = cleanup(await keep(page, 5));
  // cancelling changes nothing
  await importFile(page, file, false);
  expect((await scheduleOf(page, 5)).times).toHaveLength(1);
  await importFile(page, file, true);
  const s = await scheduleOf(page, 5);
  expect(s.times).toEqual(doc.schedule.times);
  expect(s.days).toEqual(doc.schedule.days);
  await restore();

  // a file that is not an export is refused
  const bad = info.outputPath('bad.json');
  const { writeFileSync } = await import('node:fs');
  writeFileSync(bad, '{"format":"something-else"}');
  await pick(page, bad);
  await expect(io(page).locator('.msg.err')).toHaveText('This file is not a schedule export.');
});

test('spread to single times: the doses of the plan become the times', async ({ page }) => {
  await open(page, '#/pump/1');
  await proMode(page);
  const s = await scheduleOf(page, 1);
  expect(s.kind).toBe('spread');
  const f = form(page);
  await f.getByRole('radio', { name: 'Single times' }).click();
  const slots = s.today.slots;
  expect(slots.length).toBeGreaterThan(1);
  for (const [k, x] of slots.entries()) {
    await expect(f.getByRole('textbox', { name: `Time ${k + 1}`, exact: true })).toHaveValue(x.time);
    await expect(f.getByRole('textbox', { name: `Amount ${k + 1} in ml`, exact: true })).toHaveValue(String(x.ml));
  }
  await expect(f.getByText(`${slots.length} times · ${s.today.ml.toFixed(2)} ml per day`)).toBeVisible();
  // nothing saved until Save
  await f.getByRole('button', { name: 'Discard' }).click();
  await expect(f.getByRole('radio', { name: 'Spread' })).toHaveAttribute('aria-checked', 'true');
  expect((await scheduleOf(page, 1)).kind).toBe('spread');
});

// The device answers a plan above the daily limit with 409
test.describe('daily limit', () => {
  test.use({ allowErrors: /status of 409 .*\/api\/v1\/pumps\/schedule/ });

  test('a plan above the daily limit is shown and not saved', async ({ page, cleanup }) => {
    const p = (await (await page.request.get('/api/v1/pumps')).json()).pumps[1];
    cleanup(() => page.request.post(`/api/v1/pumps/settings?pump=2&daily_limit_ml=${p.daily.limit_ml}`));
    const s = await scheduleOf(page, 2);
    const limit = Math.floor(s.ml / 2);
    await page.request.post(`/api/v1/pumps/settings?pump=2&daily_limit_ml=${limit}`);
    await open(page, '#/pump/2');
    const f = page.locator('section, .card').filter({ has: page.getByRole('heading', { name: 'Schedule', level: 2 }) }).last();  // simple view
    await expect(f.getByText(`Daily limit ${limit.toFixed(2)} ml – planned ${s.ml.toFixed(2)} ml`)).toBeVisible();
    await f.getByRole('textbox', { name: 'Daily amount ml' }).fill(String(s.ml + 1));
    await f.getByRole('button', { name: 'Save' }).click();
    await expect(f.getByText('The planned amount exceeds the daily limit')).toBeVisible();
    expect((await scheduleOf(page, 2)).ml).toBe(s.ml);
  });
});
