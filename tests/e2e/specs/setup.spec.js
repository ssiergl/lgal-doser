// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, labelPassword, test } from './fixtures.js';

// Factory state (second mock, MOCK_FACTORY=1): runs through once, the mock then is set up
test.use({ baseURL: 'http://127.0.0.1:8772', signedIn: false });

test('@factory setup wizard from the factory state', async ({ page }) => {
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

  // Wi-Fi: search, pick a network, a wrong and then a right password (the mock refuses "wrongpass1")
  await expect(page.locator('#scan')).toBeHidden();  // LAN is the default
  await page.getByRole('radio', { name: 'Wi-Fi' }).check();
  await page.locator('#scan').click();
  await page.locator('#nets').getByRole('button', { name: /HomeNet-Guest/ }).click();
  await expect(page.locator('#ssid')).toHaveValue('HomeNet-Guest');
  await expect(page.locator('#nets button')).toHaveCount(0);
  await page.locator('#wpw').fill('wrongpass1');
  await page.locator('#wtest').click();
  await expect(page.locator('#net_msg')).toHaveText('Connection failed: wrong password', { timeout: 10_000 });
  await page.locator('#wpw').fill('guest-pass-1');
  await page.locator('#wtest').click();
  await expect(page.locator('#net_msg')).toHaveText('Connected and saved.', { timeout: 10_000 });
  expect((await (await page.request.get('/api/v1/net/settings')).json()).wlan).toMatchObject({ ssid: 'HomeNet-Guest', password_set: true });
  await page.locator('#next').click();

  // time: an own POSIX zone and time server
  await expect(page.locator('#stepno')).toHaveText('Step 4 of 5');
  await expect(page.locator('#timest')).toContainText('Device time');  // loaded, it no longer resets the list
  await expect(page.locator('#tzc')).toBeHidden();
  await page.locator('#tz').selectOption({ label: 'Other (POSIX string) …' });
  await page.locator('#tzc').fill('EST5EDT,M3.2.0,M11.1.0');
  await page.locator('#ntp').fill('time.example.org');
  await page.locator('#next').click();
  await expect(page.locator('#stepno')).toHaveText('Step 5 of 5');
  expect(await (await page.request.get('/api/v1/system/time')).json())
    .toMatchObject({ timezone: 'EST5EDT,M3.2.0,M11.1.0', ntp_server: 'time.example.org' });
  await page.locator('#next').click();
  await expect(page.locator('#done_ok')).toBeVisible();

  await page.goto('/');
  await expect(page).not.toHaveURL(/\/setup|\/login/);
  await expect(page.getByRole('heading', { name: 'Overview', level: 1 })).toBeVisible();
});
