// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

const card = page => page.locator('form.locks');
const locksOf = async page => (await (await page.request.get('/api/v1/schedule/locks')).json());

test('lock time rules: add, refuse invalid ones, save, delete', async ({ page, cleanup }) => {
  await open(page, '#/settings');
  await proMode(page);
  const before = await locksOf(page);
  const c = card(page);
  const restore = cleanup(() => page.request.put('/api/v1/schedule/locks', { data: before }));
  await expect(c.getByRole('combobox', { name: 'Rule 1: first pump' })).toHaveValue('1');
  await c.getByRole('button', { name: '+ Add rule' }).click();
  const a = c.getByRole('combobox', { name: 'Rule 2: first pump' });
  const b = c.getByRole('combobox', { name: 'Rule 2: second pump' });
  const gap = c.getByRole('textbox', { name: 'Rule 2: lock time in minutes' });
  await expect(a).toBeVisible();

  // the same pump twice
  await a.selectOption('3');
  await b.selectOption('3');
  await expect(c.locator('.msg.err')).toHaveText('Rule 2: Choose two different pumps');
  // the pair of rule 1 again
  await a.selectOption('2');
  await b.selectOption('1');
  await expect(c.locator('.msg.err')).toHaveText('Rule 2: These pumps already have a rule');
  // lock time out of range
  await b.selectOption('5');
  await gap.fill('2000');
  await expect(c.locator('.msg.err')).toHaveText('Rule 2: Lock time: 1 to 1440 minutes');
  await gap.fill('30');
  await expect(c.locator('.msg.err')).toHaveCount(0);

  await c.getByRole('radio', { name: 'Wait' }).click();
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('Lock times saved');
  const saved = await locksOf(page);
  expect(saved.manual).toBe('queue');
  expect(saved.rules).toContainEqual({ pumps: [2, 5], gap_min: 30 });

  await page.reload();
  await expect(c.getByRole('textbox', { name: 'Rule 2: lock time in minutes' })).toHaveValue('30');
  await c.getByRole('button', { name: 'Delete rule' }).last().click();
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('Lock times saved');
  expect((await locksOf(page)).rules).toHaveLength(before.rules.length);
  await restore();
});

// The mock answers a manual dose of 13 ml with a running lock time, 14 ml with "queued"
test.describe('manual dose during a lock time', () => {
  test.use({ allowErrors: /status of 409 .*\/api\/v1\/pumps\/dose/ });
  test('is refused with the time or waits', async ({ page }) => {
    await open(page, '#/pump/2');
    const c = page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Dose now', level: 2 }) });
    const amount = c.getByRole('textbox', { name: 'Amount ml', exact: true });
    await amount.fill('13');
    await c.getByRole('button', { name: 'Dose', exact: true }).click();
    await expect(c.locator('.msg.err')).toHaveText(/^Lock time to another pump until (tomorrow )?\d{1,2}:\d\d/);  // before midnight: "tomorrow 00:02"
    await amount.fill('14');
    await c.getByRole('button', { name: 'Dose', exact: true }).click();
    await expect(c.getByRole('status')).toHaveText('Lock time running – the dose starts afterwards');
  });

  test('"Ask": cancel, wait or dose anyway', async ({ page, cleanup }) => {
    await open(page, '#/settings');
    await proMode(page);
    const before = await locksOf(page);
    cleanup(() => page.request.put('/api/v1/schedule/locks', { data: before }));
    const lc = card(page);
    await lc.getByRole('radio', { name: 'Ask' }).click();
    await expect(lc.getByText('When dosing you choose whether to cancel, wait or dose anyway.', { exact: false })).toBeVisible();
    await lc.getByRole('button', { name: 'Save' }).click();
    await expect(lc.getByRole('status')).toHaveText('Lock times saved');
    expect((await locksOf(page)).manual).toBe('ask');

    await open(page, '#/pump/1');
    const c = page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Dose now', level: 2 }) });
    await c.getByRole('textbox', { name: 'Amount ml', exact: true }).fill('13');
    const dialog = page.getByRole('alertdialog', { name: 'Lock time running' });
    const doses = [];
    page.on('request', r => { if (r.url().includes('/api/v1/pumps/dose')) doses.push(new URL(r.url()).searchParams.get('lock')); });

    // cancel: nothing is sent again
    await c.getByRole('button', { name: 'Dose', exact: true }).click();
    await expect(dialog).toContainText(/The lock time to Calcium runs until (tomorrow )?\d{1,2}:\d\d\./);
    await expect(dialog.getByRole('button', { name: 'Cancel' })).toBeFocused();  // Enter cannot dose by accident
    await dialog.getByRole('button', { name: 'Cancel' }).click();
    await expect(dialog).toHaveCount(0);
    expect(doses).toEqual([null]);

    // wait: queued until the lock time ends
    await c.getByRole('button', { name: 'Dose', exact: true }).click();
    await dialog.getByRole('button', { name: /^Dose at / }).click();
    await expect(c.getByRole('status')).toHaveText('Lock time running – the dose starts afterwards');

    // dose anyway: started, and the history names the lock time passed over
    await c.getByRole('button', { name: 'Dose', exact: true }).click();
    await dialog.getByRole('button', { name: 'Dose anyway' }).click();
    await expect(c.getByRole('status')).toHaveText('13.00 ml started');
    expect(doses).toEqual([null, null, 'queue', null, 'override']);
    await proMode(page);
    await open(page, '#/history/1');
    const ev = page.locator('.hi-list > li').first();
    await expect(ev.locator('.hi-d')).toHaveText('13.00 ml · lock time to Calcium passed over');

    // a choice is refused with any other policy
    await page.request.put('/api/v1/schedule/locks', { data: before });
    expect((await page.request.post('/api/v1/pumps/dose?pump=1&ml=13&lock=override')).status()).toBe(400);
  });
});

// The device answers a rule that collides with the schedules with 409
test.describe('collisions', () => {
  test.use({ allowErrors: /status of 409 .*\/api\/v1\/schedule\/locks/ });

  test('a rule that collides with the schedules is refused with the doses', async ({ page, cleanup }) => {
    await open(page, '#/settings');
    await proMode(page);
    const before = await locksOf(page);
    cleanup(() => page.request.put('/api/v1/schedule/locks', { data: before }));
    const c = card(page);
    await c.getByRole('button', { name: '+ Add rule' }).click();
    const n = before.rules.length + 1;
    await c.getByRole('combobox', { name: `Rule ${n}: first pump` }).selectOption('1');
    await c.getByRole('combobox', { name: `Rule ${n}: second pump` }).selectOption('5');
    await c.getByRole('textbox', { name: `Rule ${n}: lock time in minutes` }).fill('1440');  // a whole day: every dose collides
    await c.getByRole('button', { name: 'Save' }).click();
    await expect(c.locator('.msg.err')).toContainText(/Pump \d \d{1,2}:\d\d and pump \d \d{1,2}:\d\d: lock time 1440 min/);
    expect(await locksOf(page)).toEqual(before);
  });
});
