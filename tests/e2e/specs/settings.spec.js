// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, test } from './fixtures.js';

test('language switch reaches the device and back', async ({ page, request }) => {
  await open(page, '#/settings');
  try {
    await page.getByRole('button', { name: 'English' }).click();
    await page.getByRole('option', { name: /Deutsch/ }).click();
    await expect(page.getByRole('heading', { name: 'Einstellungen', level: 1 })).toBeVisible();
    await expect(page.locator('html')).toHaveAttribute('lang', 'de');
    await expect.poll(async () => (await (await request.get('/api/v1/system/language')).json()).language).toBe('de');
    await page.reload();
    await expect(page.getByRole('banner').getByRole('link', { name: 'Übersicht' })).toBeVisible();
  } finally {
    await request.post('/api/v1/system/language?lang=en');
  }
});

test('theme and clock stay after a reload', async ({ page }) => {
  await open(page, '#/settings');
  await page.getByRole('button', { name: 'Dark', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
  const bg = await page.evaluate(() => getComputedStyle(document.body).backgroundColor);
  await page.getByRole('button', { name: 'Light', exact: true }).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  expect(await page.evaluate(() => getComputedStyle(document.body).backgroundColor)).not.toBe(bg);
  await page.getByRole('button', { name: 'Dark', exact: true }).click();
  await page.getByRole('button', { name: '12 hours (AM/PM)' }).click();
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
  await page.goto('/#/pump/1');
  await expect(page.getByText(/\d{1,2}:\d\d (AM|PM)/).first()).toBeVisible();
});
