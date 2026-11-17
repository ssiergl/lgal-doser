// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared test set-up: every test starts signed in, with the device language English, and fails
// when the page logs an error. The mock test passwords are read from the mock itself, so they
// live in one place only.
import { readFileSync } from 'node:fs';
import { test as base, expect } from '@playwright/test';

const sysMock = readFileSync(new URL('../../../tools/mock/sys_mock.py', import.meta.url), 'utf8');
const value = re => {
  const m = sysMock.match(re);
  if (!m) throw new Error(`tools/mock/sys_mock.py no longer matches ${re}`);
  return m[1];
};
export const adminPassword = value(/AUTH = \{"password": "([^"]+)"/);
export const labelPassword = value(/LABEL_PW = "([^"]+)"/);

// Errors the device answers on purpose: the engineering API without a key, a wrong password
const expected = url => /\/api\/v1\/engineering\/|\/api\/v1\/auth\/(login|claim|engineering)/.test(url);

export const test = base.extend({
  // "signedIn: false" for pages without the app (factory mock)
  signedIn: [true, { option: true }],
  page: async ({ page, request, signedIn }, use, info) => {
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => {
      if (m.type() === 'error' && !expected(m.location().url)) errors.push(m.text() + ' ' + m.location().url);
    });
    if (signedIn) {
      expect((await request.post('/api/v1/auth/login', { data: { password: adminPassword } })).ok()).toBeTruthy();
      await request.post('/api/v1/system/language?lang=en');
      await request.post('/api/v1/pumps/stop?pump=0');
    }
    await use(page);
    expect(errors, `errors on the page (${info.title})`).toEqual([]);
  },
});
export { expect };

// Opens a page of the app and waits until the live data is there
export async function open(page, hash = '#/') {
  await page.goto('/' + hash);
  await expect(page.locator('header .live.on')).toHaveCount(1);
  await expect(page.locator('main h1').first()).toBeVisible();
}

// The second button of the view switch in the header (its label depends on the language)
export async function proMode(page) {
  const pro = page.locator('header .seg button').nth(1);
  await pro.click();
  await expect(pro).toHaveClass(/on/);
}
