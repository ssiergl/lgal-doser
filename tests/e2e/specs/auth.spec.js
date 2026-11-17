// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { adminPassword, expect, open, test } from './fixtures.js';

// WebKit reports the requests that signing out cuts off
test.use({ allowErrors: /due to access control checks|Load failed/ });

test('sign out, wrong password, sign in again', async ({ page }) => {
  await open(page, '#/settings');
  await page.getByRole('button', { name: 'Sign out' }).click();
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
