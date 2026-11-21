// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { adminPassword, expect, open, test } from './fixtures.js';

// WebKit and Firefox report the requests that signing out cuts off
test.use({ allowErrors: /due to access control checks|Load failed|NetworkError when attempting/ });

test('sign out, wrong password, sign in again', async ({ page }) => {
  await open(page, '#/settings');
  await page.getByRole('button', { name: 'Sign out', exact: true }).click();
  await expect(page).toHaveURL(/\/login/);

  // without a session the API answers 401 and pages lead to the sign-in
  expect((await page.request.get('/api/v1/pumps')).status()).toBe(401);
  await page.goto('/#/history');
  await expect(page).toHaveURL(/\/login/);

  await page.locator('#l_pw').fill('wrong-password');
  await page.locator('#f_login button').click();
  await expect(page.locator('#l_msg')).not.toBeEmpty();
  await expect(page).toHaveURL(/\/login/);

  await page.locator('#l_pw').fill(adminPassword);
  await page.locator('#f_login button').click();
  await expect(page).not.toHaveURL(/\/login/);
  await expect(page.getByRole('heading', { level: 1 })).toBeVisible();
});

test('too many wrong passwords: the sign-in asks to wait', async ({ page }) => {
  await page.request.post('/api/v1/auth/logout');
  await page.goto('/login');
  const pw = page.locator('#l_pw');
  const send = page.locator('#f_login button');
  const msg = page.locator('#l_msg');
  // five wrong passwords are free, the sixth starts a wait of 1 s (like the firmware)
  for (let i = 0; i < 6; i++) {
    await pw.fill('wrong-password-' + i);
    await send.click();
    await expect(msg).toHaveText('Wrong password');
    await msg.evaluate(e => { e.textContent = ''; });
  }
  await pw.fill(adminPassword);
  await send.click();
  await expect(msg).toHaveText(/^Too many attempts – please wait [12] s\.$/);
  await expect(page).toHaveURL(/\/login/);

  // after the wait the right password works and resets the count
  await page.waitForTimeout(2100);
  await send.click();
  await expect(page).not.toHaveURL(/\/login/);
});

test('stay signed in, then sign out everywhere', async ({ page }) => {
  await page.request.post('/api/v1/auth/logout');
  await page.goto('/login');
  const keep = page.getByRole('checkbox', { name: 'Stay signed in' });
  await expect(keep).not.toBeChecked();  // off unless chosen
  await keep.check();
  await page.locator('#l_pw').fill(adminPassword);
  await page.locator('#f_login button').click();
  await expect(page).not.toHaveURL(/\/login/);
  expect((await (await page.request.get('/api/v1/auth/status')).json()).remember).toBe(true);

  await open(page, '#/settings');
  await expect(page.getByText('This browser stays signed in (90 days after the last visit).')).toBeVisible();
  await page.getByRole('button', { name: 'Sign out everywhere' }).click();
  await page.getByRole('alertdialog').getByRole('button', { name: 'Sign out all' }).click();
  await expect(page).toHaveURL(/\/login/);
  expect((await page.request.get('/api/v1/pumps')).status()).toBe(401);
  await expect(keep).toBeChecked();  // the browser remembers the choice
});
