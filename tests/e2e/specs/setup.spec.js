// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, labelPassword, test } from './fixtures.js';

// Factory state (second mock, MOCK_FACTORY=1): runs through once, the mock then is set up
test.use({ baseURL: 'http://127.0.0.1:8772', signedIn: false });

test('setup wizard from the factory state', async ({ page }) => {
  await page.goto('/');
  await expect(page).toHaveURL(/\/setup$/);
  await page.locator('#langs').getByRole('button', { name: 'English' }).click();
  await expect(page.locator('#stepno')).toHaveText('Step 2 of 5');

  const pw = 'e2e-admin-' + Date.now();
  await page.locator('#pw0').fill('wrong-label');
  await page.locator('#pw1').fill(pw);
  await page.locator('#pw2').fill(pw);
  await page.locator('#f_pw button').click();
  await expect(page.locator('#pw_msg')).not.toBeEmpty();

  await page.locator('#pw0').fill(labelPassword);
  await page.locator('#pw1').fill(pw);
  await page.locator('#pw2').fill(pw);
  await page.locator('#f_pw button').click();
  await expect(page).toHaveURL(/\/login/);
  await page.locator('#l_pw').fill(pw);
  await page.locator('#f_login button').click();

  await expect(page).toHaveURL(/\/setup/);
  await expect(page.locator('#stepno')).toHaveText('Step 3 of 5');
  await expect(page.locator('#next')).toBeEnabled();
  await page.locator('#next').click();
  await expect(page.locator('#stepno')).toHaveText('Step 4 of 5');
  await page.locator('#next').click();
  await expect(page.locator('#stepno')).toHaveText('Step 5 of 5');
  await page.locator('#next').click();
  await expect(page.locator('#done_ok')).toBeVisible();

  await page.goto('/');
  await expect(page).not.toHaveURL(/\/setup|\/login/);
  await expect(page.getByRole('heading', { name: 'Overview', level: 1 })).toBeVisible();
});
