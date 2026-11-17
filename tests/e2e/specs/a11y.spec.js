// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Accessibility (axe-core, WCAG 2.1 A/AA): contrast, names of buttons and fields, roles, in the
// light and the dark theme, on every page and in the dialogs.
import AxeBuilder from '@axe-core/playwright';
import { expect, open, proMode, test } from './fixtures.js';

async function check(page, what) {
  // a switched-off pump is shown greyed out on purpose (inactive, WCAG 1.4.3 exception)
  const r = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'])
    .exclude('.pump-card.dim').exclude('.card.off').analyze();
  const found = r.violations.map(v => `${what}: ${v.id} (${v.impact}) ${v.help}\n` +
    v.nodes.slice(0, 5).map(n => `    ${n.target.join(' ')}  ${n.failureSummary.split('\n').slice(1).join(' ').trim()}`).join('\n'));
  return found;
}

for (const theme of ['light', 'dark']) {
  test(`@axe pages are accessible (${theme})`, async ({ page }) => {
    await page.emulateMedia({ colorScheme: theme });
    await open(page);
    await proMode(page);
    const found = [];
    for (const hash of ['#/', '#/pump/1', '#/pump/2', '#/pump/3', '#/pump/4', '#/history', '#/settings', '#/maintenance']) {
      await page.goto('/' + hash);
      await expect(page.locator('main h1').first()).toBeVisible();
      found.push(...await check(page, hash));
    }
    await page.goto('/#/pump/1');
    await page.getByRole('button', { name: 'Start calibration' }).click();
    await expect(page.getByRole('dialog').getByRole('button', { name: 'Start', exact: true })).toBeFocused();
    found.push(...await check(page, 'calibration dialog'));
    await page.keyboard.press('Escape');
    await page.locator('main').getByRole('button', { name: 'Tube replaced' }).click();
    await expect(page.getByRole('alertdialog')).toBeVisible();
    found.push(...await check(page, 'confirmation'));
    await page.keyboard.press('Escape');
    expect(found.join('\n')).toBe('');
  });
}

// The pages outside the app: sign-in, and the setup wizard of the factory mock (first step only,
// the setup test runs through it later)
for (const theme of ['light', 'dark']) {
  test(`@axe @factory sign-in and setup are accessible (${theme})`, async ({ page }) => {
    await page.emulateMedia({ colorScheme: theme });
    await page.request.post('/api/v1/auth/logout');
    await page.goto('/login');
    await expect(page.locator('#l_pw')).toBeVisible();
    const found = await check(page, '/login');
    await page.goto('http://127.0.0.1:8772/setup');
    await expect(page.locator('#langs button').first()).toBeVisible();
    found.push(...await check(page, '/setup'));
    expect(found.join('\n')).toBe('');
  });
}
