// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

import { expect, open, proMode, test } from './fixtures.js';

test('overview shows all pumps and leads to a pump', async ({ page }) => {
  await open(page);
  await expect(page.getByRole('heading', { name: 'Overview', level: 1 })).toBeVisible();
  const cards = page.locator('main a.pump-card');
  await expect(cards).toHaveCount(5);
  await expect(cards.nth(0).getByRole('heading', { level: 2 })).toHaveText('KH');
  await expect(page.locator('header .live')).toHaveClass(/on/);

  await cards.nth(1).click();
  await expect(page).toHaveURL(/#\/pump\/2$/);
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Calcium');
  await page.getByRole('main').getByRole('navigation').getByRole('link', { name: /^3 / }).click();
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Magnesium');
});

test('main navigation reaches every page', async ({ page }) => {
  await open(page);
  const nav = page.getByRole('banner').getByRole('navigation');
  for (const [link, heading] of [['History', 'History'], ['Overview', 'Overview'], ['Settings', 'Settings']]) {
    await nav.getByRole('link', { name: link }).click();
    await expect(page.getByRole('heading', { name: heading, level: 1 })).toBeVisible();
    await expect(nav.getByRole('link', { name: link })).toHaveClass(/on/);
  }
  await page.getByRole('link', { name: /Maintenance and firmware update/ }).click();
  await expect(page.getByRole('heading', { name: 'Maintenance', level: 1 })).toBeVisible();
  await page.goto('/#/nowhere');
  await expect(page.getByText('Page not found')).toBeVisible();
});

// Every page renders without errors in both views (the fixture fails on page errors)
for (const pro of [false, true]) {
  test(`all pages render (${pro ? 'pro' : 'simple'} view)`, async ({ page }) => {
    await open(page);
    if (pro) await proMode(page);
    for (const hash of ['#/', '#/pump/1', '#/pump/2', '#/pump/3', '#/pump/4', '#/pump/5', '#/history', '#/history/2',
      '#/settings', '#/maintenance']) {
      await page.goto('/' + hash);
      await expect(page.locator('main h1').first()).toBeVisible();
    }
  });
}
