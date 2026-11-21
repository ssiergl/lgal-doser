// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Login card and API keys (settings page): the admin password needs the current one, a new key
// is shown once, all slots in use stop new keys. (Sign out and sign in: auth.spec.js.)
import { adminPassword, expect, open, proMode, test } from './fixtures.js';

test.use({ allowErrors: /status of 403 .*\/api\/v1\/auth\/password/ });

const card = (page, title) => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: title, level: 2, exact: true }) });

test('change the admin password', async ({ page, cleanup }) => {
  const changed = 'e2e-changed-pw';
  cleanup(() => page.request.post('/api/v1/auth/password', { data: { current: changed, new: adminPassword } }));
  await open(page, '#/settings');
  const c = card(page, 'Login');
  await expect(c.getByText(/^Signed in at lgal-doser-e4f5\.$/)).toBeVisible();
  await expect(c.getByText('This browser is signed in until it is closed.')).toBeVisible();
  await expect(c.getByRole('heading', { name: 'Change password', level: 3 })).toBeVisible();
  const current = c.getByLabel('Current password');
  const pw = c.getByLabel('New password', { exact: true });
  const pw2 = c.getByLabel('Repeat new password');
  const save = c.getByRole('button', { name: 'Save password' });
  await expect(save).toBeDisabled();

  await current.fill('not-the-password');
  await pw.fill('short');
  await save.click();
  await expect(c.getByRole('status')).toHaveText('At least 8 characters');
  await pw.fill(changed);
  await pw2.fill(changed + 'x');
  await save.click();
  await expect(c.getByRole('status')).toHaveText('The passwords do not match.');
  await pw2.fill(changed);
  await save.click();
  await expect(c.getByRole('status')).toHaveText('Wrong password');

  await current.fill(adminPassword);
  await save.click();
  await expect(c.getByRole('status')).toHaveText('Password saved.');
  for (const f of [current, pw, pw2]) await expect(f).toHaveValue('');
  // the new one counts, the old one not any more
  expect((await page.request.post('/api/v1/auth/login', { data: { password: changed } })).ok()).toBe(true);
});

test('API keys (pro view): shown once, all slots, delete asks first', async ({ page, cleanup, browserName }) => {
  const before = (await (await page.request.get('/api/v1/auth/keys')).json()).keys.map(k => k.slot);
  cleanup(async () => {
    for (const k of (await (await page.request.get('/api/v1/auth/keys')).json()).keys) {
      if (!before.includes(k.slot)) await page.request.delete('/api/v1/auth/keys?slot=' + k.slot);
    }
  });
  if (browserName === 'chromium') await page.context().grantPermissions(['clipboard-read', 'clipboard-write']);
  await open(page, '#/settings');
  await expect(card(page, 'API keys')).toHaveCount(0);  // simple view
  await proMode(page);
  const c = card(page, 'API keys');
  const keys = c.locator('ul.keys > li');
  await expect(keys).toHaveCount(1);
  await expect(keys.first()).toContainText('Home Assistant');

  const name = c.getByRole('textbox', { name: 'Name of the new key' });
  const add = c.getByRole('button', { name: 'Create key' });
  await expect(add).toBeDisabled();
  await name.fill('Aquarium app');
  await add.click();
  const fresh = c.locator('.newkey');
  await expect(fresh).toContainText('Copy the key now – it is shown only once:');
  await expect(fresh.locator('code')).toHaveText(/^lgal_test_[0-9a-f]{32}$/);
  await expect(name).toHaveValue('');
  await expect(keys).toHaveCount(2);
  await fresh.getByRole('button', { name: 'Copy' }).click();
  await expect(fresh.getByRole('button', { name: 'Copied' })).toBeVisible();
  if (browserName === 'chromium') {
    expect(await page.evaluate(() => navigator.clipboard.readText())).toBe(await fresh.locator('code').textContent());
  }
  await fresh.getByRole('button', { name: 'Done' }).click();
  await expect(fresh).toHaveCount(0);

  // all slots in use: no more keys
  for (const n of ['Script', 'Backup job']) {
    await name.fill(n);
    await add.click();
    await expect(keys).toContainText([n]);
  }
  await expect(c.getByText('All 4 key slots are in use.')).toBeVisible();
  await expect(name).toBeDisabled();

  const ask = page.getByRole('alertdialog');
  const app = keys.filter({ hasText: 'Aquarium app' });
  await app.getByRole('button', { name: 'Delete' }).click();
  await expect(ask).toContainText('Delete key "Aquarium app"? Programs using it lose access.');
  await ask.getByRole('button', { name: 'Cancel' }).click();
  await expect(keys).toHaveCount(4);
  await app.getByRole('button', { name: 'Delete' }).click();
  await ask.getByRole('button', { name: 'Delete' }).click();
  await expect(c.locator('.msg')).toHaveText('Key "Aquarium app" deleted.');
  await expect(keys).toHaveCount(3);
  await expect(name).toBeEnabled();
});

// The device speaks plain HTTP: no secure context, no Clipboard API. The key is copied
// through a hidden text field; if the browser refuses, the card says so.
test('copy a new key over plain HTTP', async ({ page, cleanup, browserName }) => {
  const before = (await (await page.request.get('/api/v1/auth/keys')).json()).keys.map(k => k.slot);
  cleanup(async () => {
    for (const k of (await (await page.request.get('/api/v1/auth/keys')).json()).keys) {
      if (!before.includes(k.slot)) await page.request.delete('/api/v1/auth/keys?slot=' + k.slot);
    }
  });
  if (browserName === 'chromium') await page.context().grantPermissions(['clipboard-read', 'clipboard-write']);
  await page.addInitScript(() => {
    Object.defineProperty(window, 'isSecureContext', { get: () => false });
    const orig = document.execCommand.bind(document);
    window.copies = [];
    window.refuseCopy = false;
    document.execCommand = cmd => {
      if (cmd !== 'copy') return orig(cmd);
      window.copies.push(document.activeElement && document.activeElement.value);
      return window.refuseCopy ? false : orig(cmd);
    };
  });
  await open(page, '#/settings');
  await proMode(page);
  const c = card(page, 'API keys');
  await c.getByRole('textbox', { name: 'Name of the new key' }).fill('Plain HTTP');
  await c.getByRole('button', { name: 'Create key' }).click();
  const fresh = c.locator('.newkey');
  const key = await fresh.locator('code').textContent();
  expect(key).toMatch(/^lgal_test_[0-9a-f]{32}$/);

  await page.evaluate(() => { window.refuseCopy = true; });
  await fresh.getByRole('button', { name: 'Copy' }).click();
  await expect(c.locator('.msg.err')).toHaveText('Could not copy – please select the key and copy it by hand.');
  await expect(fresh.getByRole('button', { name: 'Copy' })).toBeVisible();

  await page.evaluate(() => { window.refuseCopy = false; });
  await fresh.getByRole('button', { name: 'Copy' }).click();
  await expect(fresh.getByRole('button', { name: 'Copied' })).toBeVisible();
  await expect(c.locator('.msg.err')).toHaveCount(0);
  expect(await page.evaluate(() => window.copies)).toEqual([key, key]);
  await expect(page.locator('body > textarea')).toHaveCount(0);   // the helper field is gone again
  if (browserName === 'chromium') {
    expect(await page.evaluate(() => navigator.clipboard.readText())).toBe(key);
  }
});
