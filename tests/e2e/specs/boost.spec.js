// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

const card = page => page.locator('section.card').filter({ has: page.getByRole('heading', { name: 'Boost', level: 2 }) });

const boostOf = async (page, n) => (await (await page.request.get('/api/v1/schedule/status')).json()).pumps[n - 1].boost;

// Ends a running boost through the card (it always asks first)
async function endBoost(page) {
  const c = card(page);
  const end = c.getByRole('button', { name: 'End boost' });
  await expect(c.getByRole('button', { name: /^(End|Start) boost$/ })).toBeVisible();
  if (!(await end.count())) return;
  await end.click();
  const ask = page.getByRole('alertdialog');
  await expect(ask).toContainText('The remaining');
  await ask.getByRole('button', { name: 'End boost' }).click();
  await expect(c.getByRole('status')).toHaveText('Boost ended');
  await expect(c.getByRole('button', { name: 'Start boost' })).toBeVisible();
}

test('boost over days: preview, start, overview, end', async ({ page, cleanup }) => {
  await open(page, '#/pump/1');
  const c = card(page);
  const start = c.getByRole('button', { name: 'Start boost' });
  await expect(start).toBeDisabled();
  await c.getByRole('textbox', { name: 'Extra amount ml' }).fill('6');
  await c.getByRole('textbox', { name: 'Spread over days' }).fill('3');
  await expect(c.getByText(/≈ 2\.00 ml extra per day/)).toBeVisible();
  cleanup(() => page.request.post('/api/v1/pumps/boost?pump=1&action=stop'));  // safety net when the test stops before the end
  await start.click();
  await expect(c.getByRole('status')).toHaveText('Boost started');
  await expect(c.getByText('6.00 of 6.00 ml still to dose')).toBeVisible();
  await expect(c.getByRole('button', { name: 'End boost' })).toBeVisible();

  await page.getByRole('banner').getByRole('link', { name: 'Overview' }).click();
  await expect(page.locator('a.pump-card').first().getByLabel('Boost running')).toBeVisible();
  await page.locator('a.pump-card').first().click();
  await endBoost(page);
  expect(await boostOf(page, 1)).toBeNull();
  await page.getByRole('banner').getByRole('link', { name: 'Overview' }).click();
  await expect(page.locator('a.pump-card').first().locator('.chip')).not.toHaveCount(0);
  await expect(page.locator('a.pump-card').first().getByLabel('Boost running')).toHaveCount(0);
});

test('boost over doses (pro view)', async ({ page, cleanup }) => {
  await open(page, '#/pump/2');
  await proMode(page);
  const c = card(page);
  await c.getByRole('radio', { name: 'Doses' }).click();
  await c.getByRole('textbox', { name: 'Extra amount ml' }).fill('3');
  const doses = c.getByRole('textbox', { name: /Spread over/ });
  await doses.fill('6');
  await expect(c.getByText(/0\.50 ml extra per dose/)).toBeVisible();
  cleanup(() => page.request.post('/api/v1/pumps/boost?pump=2&action=stop'));  // safety net when the test stops before the end
  await c.getByRole('button', { name: 'Start boost' }).click();
  await expect(c.getByRole('status')).toHaveText('Boost started');
  await expect(c.getByText(/6 doses left/)).toBeVisible();
  expect((await boostOf(page, 2)).doses).toBe(6);
  await endBoost(page);
  expect(await boostOf(page, 2)).toBeNull();
});

test('boost needs a valid amount', async ({ page }) => {
  await open(page, '#/pump/1');
  const c = card(page);
  await c.getByRole('textbox', { name: 'Extra amount ml' }).fill('6');
  await c.getByRole('textbox', { name: 'Spread over days' }).fill('0');
  await expect(c.getByRole('button', { name: 'Start boost' })).toBeDisabled();
});
