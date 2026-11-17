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
export const engineeringKey = value(/ENGKEY = "([^"]+)"/);

// Errors the device answers on purpose: the engineering API without a key, a wrong password
const expected = url => /\/api\/v1\/(engineering|hw)\/|\/api\/v1\/auth\/(login|claim|engineering)/.test(url);

export const test = base.extend({
  // "signedIn: false" for pages without the app (factory mock)
  signedIn: [true, { option: true }],
  // console and page errors a test causes on purpose (e.g. /401|WebSocket/)
  allowErrors: [null, { option: true }],
  page: async ({ page, request, browserName, signedIn, allowErrors }, use, info) => {
    const errors = [];
    // WebKit on Linux without a graphics card blurs behind every glass card in software: the
    // unlocked engineering page (15 cards) then draws no frame for over a minute, and no click
    // gets through. Real devices blur on the graphics chip, so the tests drop only the blur.
    if (browserName === 'webkit' && process.platform === 'linux') {
      await page.addInitScript(() => addEventListener('DOMContentLoaded', () => {
        const s = document.createElement('style');
        s.textContent = '* { backdrop-filter: none !important; -webkit-backdrop-filter: none !important; }';
        document.head.appendChild(s);
      }));
    }
    // allowErrors also covers page errors: WebKit reports a request cut off by a reload as one,
    // even when the page catches it
    page.on('pageerror', e => { if (!(allowErrors && allowErrors.test(String(e)))) errors.push(String(e)); });
    page.on('console', m => {
      const text = m.text() + ' ' + m.location().url;
      if (m.type() === 'error' && !expected(m.location().url) && !(allowErrors && allowErrors.test(text))) errors.push(text);
    });
    if (signedIn) {
      expect((await request.post('/api/v1/auth/login', { data: { password: adminPassword } })).ok()).toBeTruthy();
      await request.post('/api/v1/system/language?lang=en');
      await request.post('/api/v1/pumps/stop?pump=0');
    }
    await use(page);
    expect(errors, `errors on the page (${info.title})`).toEqual([]);
  },
  // Puts the shared mock state back. cleanup(fn) returns fn to call at the end of the test (so the
  // test can check the result); a test that stops before (failure, timeout) gets it run after it.
  // try/finally gets no time left after a timeout, and the next tests would start from a wrong state.
  cleanup: async ({ page }, use) => {
    const pending = new Set();
    await use(fn => {
      const once = async () => { if (pending.delete(once)) await fn(); };
      pending.add(once);
      return once;
    });
    for (const f of [...pending].reverse()) await f();
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

// Unlocks the engineering area through the page like a technician; locks it again after the test
export async function unlockEngineering(page, cleanup) {
  cleanup(() => page.request.delete('/api/v1/auth/engineering'));
  await open(page, '#/engineering');
  await page.getByRole('textbox', { name: 'Engineering key' }).fill(engineeringKey);
  await page.getByRole('button', { name: 'Unlock' }).click();
  await expect(page.getByRole('heading', { name: 'A · Mechanics', level: 2 })).toBeVisible();
}
