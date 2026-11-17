// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { adminPassword, expect, open, test } from './fixtures.js';

// Safari reports the dropped connection in the console
test.use({ allowErrors: /WebSocket connection to .* failed/ });

// /mock/kill drops every WebSocket connection like a lost network
const kill = page => page.request.post('/mock/kill');
const activity = async (page, n) => (await (await page.request.get('/api/v1/pumps')).json()).pumps[n - 1].activity;

test('live connection drops and comes back by itself', async ({ page }) => {
  await open(page);
  const live = page.locator('header .live');
  await expect(live).toHaveClass(/on/);
  await kill(page);
  await expect(live).not.toHaveClass(/on/);
  await expect(live).toHaveText('Connecting …');
  await expect(live).toHaveClass(/on/, { timeout: 5000 });  // first retry after 1 s
  await expect(live).toHaveText('Live');

  // live values arrive again: a dose shows its progress on the overview
  await page.request.post('/api/v1/pumps/dose?pump=1&ml=2');
  await expect(page.locator('a.pump-card').first().locator('.running')).toBeVisible();
});

// Dead man: priming must stop when the browser is gone
test('priming stops when the connection is lost', async ({ page }) => {
  await open(page, '#/pump/5');
  const hold = page.locator('main button.hold');
  await hold.hover();
  await page.mouse.down();
  await expect.poll(() => activity(page, 5)).toBe('priming');
  await kill(page);
  await expect.poll(() => activity(page, 5)).toBe('idle');
  await expect(hold).not.toHaveClass(/held/);
  await page.mouse.up();
  await expect(page.locator('header .live')).toHaveClass(/on/);
  expect(await activity(page, 5)).toBe('idle');
});

// The session ends (sign-out elsewhere, restart): the next action leads to the sign-in and back
test.describe('expired session', () => {
  test.use({ allowErrors: /status of 401|WebSocket connection to .* failed/ });
  test('leads to the sign-in and back to the same page', async ({ page }) => {
    await open(page, '#/pump/2');
    await page.request.post('/api/v1/auth/logout');
    await page.getByRole('button', { name: 'Dose', exact: true }).click();
    await expect(page).toHaveURL(/\/login\?next=/);
    await expect(page.locator('#l_hint')).toBeVisible();
    await page.locator('#l_pw').fill(adminPassword);
    await page.locator('#f_login button').click();
    await expect(page).toHaveURL(/\/#\/pump\/2$/);
    await expect(page.getByRole('heading', { name: 'Calcium', level: 1 })).toBeVisible();
    expect(await activity(page, 2)).toBe('idle');  // the dose was not run without a session
  });
});

// Data shows at once after loading, also when the device answers faster than the first paint
test.describe('fast reloads', () => {
  // WebKit reports a request that a reload cuts off as "cannot load … due to access control
  // checks", although the page catches it
  test.use({ allowErrors: /due to access control checks/ });
  test('data is there right after every reload', async ({ page }) => {
    await open(page, '#/pump/1');
    for (let i = 0; i < 8; i++) {
      await page.reload();
      await expect(page.getByRole('heading', { name: 'KH', level: 1 })).toBeVisible({ timeout: 2000 });
      await expect(page.locator('header .live')).toHaveClass(/on/, { timeout: 2000 });
    }
  });
});
