// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

const card = (page, title) => page.locator('section.card').filter({ has: page.getByRole('heading', { name: title, level: 2 }) });
const pumpOf = async (page, n) => (await (await page.request.get('/api/v1/pumps')).json()).pumps[n - 1];

// Pump settings as the device has them, to put them back after a test
async function keep(page, n) {
  const p = await pumpOf(page, n);
  return () => page.request.post('/api/v1/pumps/settings?' + new URLSearchParams({
    pump: n, name: p.name, mode: p.mode, remind_months: p.calibration.remind_months, tier: p.speed.tier,
    daily_limit_ml: p.daily.limit_ml, retract_rev: p.retract.setting === 'own' ? p.retract.rev : p.retract.setting,
    direction: p.direction, warn_days: p.container.own_warn_days || 0, low_days: p.container.own_low_days || 0,
  }));
}

async function saveSettings(page) {
  const c = card(page, 'Settings');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('Settings saved');
}

test('container: refill, change, no tracking', async ({ page, cleanup }) => {
  await open(page, '#/pump/2');
  const c = page.locator('section.card.hero');
  const before = await pumpOf(page, 2);
  const restore = cleanup(() => page.request.post(`/api/v1/pumps/container?pump=2&capacity_ml=${before.container.capacity_ml}&remaining_ml=${before.container.remaining_ml}`));
  await expect(c.getByText('186 ml', { exact: true })).toBeVisible();
  await c.getByRole('button', { name: 'Refilled' }).click();
  await expect(c.getByRole('status')).toHaveText('Container set to full');
  await expect(c.getByText('2,000 ml', { exact: true })).toBeVisible();

  await c.getByRole('button', { name: 'Change' }).click();
  await c.getByRole('textbox', { name: 'Container size' }).fill('1000');
  await c.getByRole('textbox', { name: 'Content now' }).fill('1200');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.locator('.msg.err')).toBeVisible();  // more than fits
  await c.getByRole('textbox', { name: 'Content now' }).fill('750');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByRole('status')).toHaveText('Container saved');
  await expect(c.getByText('of 1,000 ml')).toBeVisible();
  expect((await pumpOf(page, 2)).container).toMatchObject({ tracked: true, capacity_ml: 1000, remaining_ml: 750 });

  // size 0: no tracking, no "Refilled"
  await c.getByRole('button', { name: 'Change' }).click();
  await c.getByRole('textbox', { name: 'Container size' }).fill('0');
  await c.getByRole('textbox', { name: 'Content now' }).fill('');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(c.getByText('No container tracking')).toBeVisible();
  await expect(c.getByRole('button', { name: 'Refilled' })).toHaveCount(0);
  await restore();
});

test('name and operation: off greys the pump out', async ({ page, cleanup }) => {
  await open(page, '#/pump/1');
  const restore = cleanup(await keep(page, 1));
  const s = card(page, 'Settings');
  await s.getByRole('textbox', { name: 'Name' }).fill('Alkalinity');
  await s.getByRole('radio', { name: 'Off' }).first().click();
  await expect(s.getByText('The pump does not run.')).toBeVisible();
  await saveSettings(page);
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Alkalinity');
  await expect(card(page, 'Dose now').getByRole('button', { name: 'Dose', exact: true })).toBeDisabled();
  await expect(card(page, 'Calibration').getByRole('button', { name: 'Start calibration' })).toBeDisabled();
  expect((await pumpOf(page, 1)).mode).toBe('disabled');

  await page.getByRole('banner').getByRole('link', { name: 'Overview' }).click();
  const first = page.locator('a.pump-card').first();
  await expect(first).toHaveClass(/dim/);
  await expect(first.getByRole('heading', { level: 2 })).toHaveText('Alkalinity');
  await expect(first.locator('.chip').last()).toHaveText('Off');
  await restore();
  expect((await pumpOf(page, 1)).mode).toBe('auto');
});

test('fine-tuning (pro view) and the hint in the simple view', async ({ page, cleanup }) => {
  await open(page, '#/pump/3');
  const restore = cleanup(await keep(page, 3));
  await proMode(page);
  const s = card(page, 'Settings');
  await s.getByRole('radio', { name: /^Fast/ }).click();
  await s.getByRole('radio', { name: 'Own value' }).click();
  await s.getByRole('textbox', { name: 'Anti-drip retraction' }).fill('3');  // more than 2 revolutions
  await s.getByRole('button', { name: 'Save' }).click();
  await expect(s.locator('.msg.err')).toBeVisible();
  await s.getByRole('textbox', { name: 'Anti-drip retraction' }).fill('0.5');
  await s.getByRole('textbox', { name: /^Daily limit/ }).fill('15');
  await s.getByRole('textbox', { name: 'Warning days' }).fill('10');
  await s.getByRole('textbox', { name: 'Alarm days' }).fill('5');
  await s.getByRole('radio', { name: 'Counter-clockwise (CCW)' }).click();
  await saveSettings(page);
  const p = await pumpOf(page, 3);
  expect(p.speed.tier).toBe(2);
  expect(p.retract).toMatchObject({ setting: 'own', rev: 0.5 });
  expect(p.daily.limit_ml).toBe(15);
  expect(p.container).toMatchObject({ own_warn_days: 10, own_low_days: 5 });
  expect(p.direction).toBe('ccw');

  // the simple view hides the fine-tuning but says what is active
  await page.locator('header .seg button').first().click();
  await expect(s.getByRole('group', { name: 'Fine-tuning' })).toHaveCount(0);
  const hint = s.locator('.prohint');
  await expect(hint).toContainText('Speed: Fast');
  await expect(hint).toContainText('Anti-drip retraction: 0.50 revolutions');
  await expect(hint).toContainText('Daily limit: 15.00 ml');
  await expect(hint).toContainText('Container lasts fewer than: 10 / 5 days');
  await expect(hint).toContainText('Direction of rotation: Counter-clockwise (CCW)');
  await restore();
});
