// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Full backup in the engineering area: the file without secrets carries no passwords, the one
// with secrets says so in its name; restoring asks first and leaves network and passwords out
// unless chosen. (The schedule export of one pump: schedule.spec.js.)
import { readFileSync, writeFileSync } from 'node:fs';
import { expect, test, unlockEngineering } from './fixtures.js';

const card = page => page.locator('section.card.backup');
const today = () => new Date().toISOString().slice(0, 10);

async function download(page, c) {
  const dl = page.waitForEvent('download');
  await c.getByRole('button', { name: 'Download backup' }).click();
  const d = await dl;
  return { name: d.suggestedFilename(), file: await d.path() };
}

async function load(c, file) {
  await c.locator('input[type=file]').setInputFiles(file);
}

const parts = c => c.locator('.bk-restore label.check');
const part = (c, name) => c.locator('.bk-restore').getByRole('checkbox', { name });

test('export with and without secrets', async ({ page, cleanup }) => {
  await unlockEngineering(page, cleanup);
  const c = card(page);
  const plain = await download(page, c);
  await expect(c.getByRole('status')).toHaveText('Backup downloaded');
  expect(plain.name).toBe(`lgal-doser-e4f5-${today()}.json`);
  const doc = JSON.parse(readFileSync(plain.file, 'utf8'));
  expect(doc).toMatchObject({ format: 'lgal-doser-backup', version: 1, device: 'lgal-doser-e4f5' });
  expect(doc.pumps).toHaveLength(5);
  expect(doc.auth).toBeUndefined();
  expect(doc.network.wlan_password).toBeUndefined();

  await c.getByRole('checkbox', { name: /^Include Wi-Fi and MQTT password/ }).check();
  await expect(c.getByText(/^Admin password and API keys are only included as hashes/)).toBeVisible();
  const secret = await download(page, c);
  expect(secret.name).toBe(`lgal-doser-e4f5-${today()}-secrets.json`);
  const full = JSON.parse(readFileSync(secret.file, 'utf8'));
  expect(Object.keys(full.auth)).toEqual(['password', 'keys']);
  expect(full.network.wlan_password).toBeTruthy();
});

test('restore: wrong files, chosen parts, asks first', async ({ page, cleanup }, info) => {
  await unlockEngineering(page, cleanup);
  const c = card(page);
  const restore = c.getByRole('button', { name: 'Restore' });

  const notJson = info.outputPath('notes.json');
  writeFileSync(notJson, 'not json');
  await load(c, notJson);
  await expect(c.getByRole('status')).toHaveText('This file is not a backup of this device type.');
  const other = info.outputPath('other.json');
  writeFileSync(other, JSON.stringify({ format: 'lgal-doser-schedule', version: 1 }));
  await load(c, other);
  await expect(c.getByRole('status')).toHaveText('This file is not a backup of this device type.');

  const { file } = await download(page, c);
  const doc = JSON.parse(readFileSync(file, 'utf8'));
  const newer = info.outputPath('newer.json');
  writeFileSync(newer, JSON.stringify({ ...doc, version: 2 }));
  await load(c, newer);
  await expect(c.getByRole('status')).toHaveText('This backup is from a newer firmware and cannot be read.');

  // without secrets: no passwords, network and passwords not chosen by default
  await load(c, file);
  await expect(c.getByRole('status')).toHaveCount(0);
  await expect(c.getByText(/^lgal-doser-e4f5 · firmware 0\.5\.0-dev · /)).toBeVisible();
  await expect(parts(c)).toHaveText([
    'Device name, language, time zone, time server, LED night mode', 'Pumps, schedules and lock times',
    'Service values and hardware', /^MQTT \/ Home Assistant/,
    'Network (LAN/Wi-Fi) (without Wi-Fi password – the stored one stays for the same network)']);
  for (const p of ['Device name', 'Pumps', 'Service values', 'MQTT']) await expect(part(c, new RegExp('^' + p))).toBeChecked();
  const network = part(c, /^Network/);
  await expect(network).not.toBeChecked();
  await network.check();
  await expect(c.getByText('The device may get another address and the page loses the connection.')).toBeVisible();
  await network.uncheck();
  await expect(c.getByText(/^The device may get another address/)).toBeHidden();

  // nothing chosen: nothing to restore
  for (const p of ['Device name', 'Pumps', 'Service values', 'MQTT']) await part(c, new RegExp('^' + p)).uncheck();
  await expect(restore).toBeDisabled();
  await part(c, /^Device name/).check();
  await part(c, /^Pumps/).check();

  const ask = page.getByRole('alertdialog');
  await restore.click();
  await expect(ask).toContainText('Restore the chosen parts now? Current values are overwritten.');
  await ask.getByRole('button', { name: 'Cancel' }).click();
  await expect(restore).toBeVisible();
  const sent = page.waitForRequest(r => r.url().includes('/api/v1/backup?') && r.method() === 'POST');
  await restore.click();
  await ask.getByRole('button', { name: 'Restore' }).click();
  expect((await sent).url()).toMatch(/\?parts=system,pumps$/);
  await expect(c.getByRole('status')).toHaveText(
    'Restored: Device name, language, time zone, time server, LED night mode, Pumps, schedules and lock times.');
  await expect(restore).toBeHidden();

  // Cancel closes the restore without asking
  await load(c, file);
  await c.getByRole('button', { name: 'Cancel' }).click();
  await expect(restore).toBeHidden();
});

test.describe('signed out', () => {
  // WebKit reports the requests that the move to the sign-in page cuts off
  test.use({ allowErrors: /access control checks|Load failed|NetworkError when attempting|network connection was lost/ });

  test('restoring the passwords signs everyone out', async ({ page, cleanup }) => {
    await unlockEngineering(page, cleanup);
    const c = card(page);
    await c.getByRole('checkbox', { name: /^Include Wi-Fi and MQTT password/ }).check();
    const { file } = await download(page, c);
    await load(c, file);
    await expect(parts(c).last()).toHaveText('Admin password and API keys');
    await expect(parts(c).filter({ hasText: /^Network/ })).toHaveText('Network (LAN/Wi-Fi)');  // with its password
    for (const p of ['Device name', 'Pumps', 'Service values', 'MQTT']) await part(c, new RegExp('^' + p)).uncheck();
    await part(c, 'Admin password and API keys').check();
    await expect(c.getByText(/^Everyone is signed out, including you/)).toBeVisible();
    await c.getByRole('button', { name: 'Restore' }).click();
    await page.getByRole('alertdialog').getByRole('button', { name: 'Restore' }).click();
    await expect(c.getByRole('status')).toHaveText('Restored: Admin password and API keys. Please sign in again.');
    await page.waitForURL(/\/login$/, { waitUntil: 'commit' });
  });
});
