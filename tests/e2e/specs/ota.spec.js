// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Firmware updates in the web UI: the only way to update a device in its enclosure. The mock
// restarts for 6 s after an update; /mock/firmware puts version, update source and crash report
// back after each test.
import { expect, open, proMode, test, unlockEngineering } from './fixtures.js';

const card = (page, title) => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: title, level: 2, exact: true }) });
const firmware = page => card(page, 'Device').locator('dt', { hasText: /^Firmware$/ }).locator('xpath=following-sibling::dd[1]');
const reset = (page, cleanup) => cleanup(() => page.request.post('/mock/firmware'));

// While the device restarts, requests fail and the live connection drops
const restarting = 'ERR_|WebSocket|network connection was lost|Connection terminated unexpectedly|Could not connect|Load failed|access control checks';

test.describe('with a restart', () => {
  test.use({ allowErrors: new RegExp(restarting + '|status of 40[03] .*/api/v1/(ota/firmware|system/factory_reset)') });

  test('firmware file: wrong files are refused, a good one is installed', async ({ page, cleanup }) => {
    test.slow();
    reset(page, cleanup);
    await open(page, '#/maintenance');
    await expect(firmware(page)).toHaveText('0.5.0-dev');
    const c = card(page, 'Firmware update');
    const file = c.locator('input[type=file]');
    const install = c.getByRole('button', { name: 'Install update' });
    await expect(install).toBeDisabled();

    await file.setInputFiles({ name: 'notes.txt', mimeType: 'text/plain', buffer: Buffer.from('not firmware') });
    await install.click();
    await expect(c.getByRole('status')).toHaveText('This is not a .bin file.');
    await file.setInputFiles({ name: 'other.bin', mimeType: 'application/octet-stream', buffer: Buffer.alloc(64, 0x41) });
    await install.click();
    await expect(c.getByRole('status')).toHaveText('This is not a firmware file');

    // an ESP image starts with 0xE9
    await file.setInputFiles({ name: 'lgal_doser.bin', mimeType: 'application/octet-stream',
      buffer: Buffer.concat([Buffer.from([0xe9]), Buffer.alloc(63)]) });
    await install.click();
    await expect(c.getByRole('status')).toHaveText(/^Waiting for the device … \(\d+ s\)$/, { timeout: 15_000 });
    await expect(c.getByRole('status')).toBeHidden({ timeout: 30_000 });  // the page reloaded
    await expect(firmware(page)).toHaveText('0.5.1-dev');
  });

  test('release: check, confirm, download, restart', async ({ page, cleanup }) => {
    test.slow();
    reset(page, cleanup);
    await open(page, '#/maintenance');
    const c = card(page, 'Updates');
    await expect(c.getByText('New: 0.6.0')).toBeVisible();
    await c.getByRole('button', { name: 'Check for updates' }).click();
    await expect(c.getByText('Checking …')).toBeVisible();
    await expect(c.getByText('New: 0.6.0')).toBeVisible();

    const ask = page.getByRole('alertdialog');
    await c.getByRole('button', { name: 'Install 0.6.0' }).click();
    await expect(ask).toContainText('Install firmware 0.6.0?');
    await ask.getByRole('button', { name: 'Cancel' }).click();
    await expect(ask).toBeHidden();
    await c.getByRole('button', { name: 'Install 0.6.0' }).click();
    await ask.getByRole('button', { name: 'Install 0.6.0' }).click();
    await expect(c.getByText(/^Downloading … \d+ % \(/)).toBeVisible();
    await expect(c.getByRole('status')).toHaveText(/^(Update installed|Waiting for the device)/, { timeout: 15_000 });
    await expect(c.getByRole('status')).toBeHidden({ timeout: 30_000 });  // the page reloaded
    await expect(firmware(page)).toHaveText('0.6.0');
    await expect(card(page, 'Updates').getByText('Up to date')).toBeVisible();
  });

  test('restart asks first, the page comes back', async ({ page, cleanup }) => {
    test.slow();
    reset(page, cleanup);
    await open(page, '#/maintenance');
    const c = card(page, 'Restart');
    await c.getByRole('button', { name: 'Restart' }).click();
    const ask = page.getByRole('alertdialog');
    await expect(ask).toContainText('Restart the device now?');
    await ask.getByRole('button', { name: 'Restart' }).click();
    await expect(c.getByRole('status')).toHaveText(/^Waiting for the device/, { timeout: 15_000 });
    await expect(c.getByRole('status')).toBeHidden({ timeout: 30_000 });  // reloaded
    await expect(page.locator('header .live.on')).toHaveCount(1);
  });

  test('factory reset needs the admin password', async ({ page }) => {
    await open(page, '#/maintenance');
    const c = card(page, 'Factory reset');
    await c.getByRole('button', { name: 'Reset to factory settings' }).click();
    const go = c.getByRole('button', { name: 'Delete everything now' });
    await expect(go).toBeDisabled();
    await c.getByLabel('Admin password to confirm').fill('not-the-password');
    await go.click();
    const ask = page.getByRole('alertdialog');
    await expect(ask).toContainText('Really delete everything? This cannot be undone.');
    await expect(ask.getByRole('button', { name: 'Cancel' })).toBeFocused();  // Enter does not delete
    await ask.getByRole('button', { name: 'Reset to factory settings' }).click();
    await expect(c.getByRole('status')).toHaveText('Wrong password');
    await c.getByRole('button', { name: 'Cancel' }).click();
    await expect(go).toBeHidden();
  });
});

test('update channel and daily check (pro view)', async ({ page, cleanup }) => {
  reset(page, cleanup);
  await open(page, '#/maintenance');
  await proMode(page);
  const c = card(page, 'Updates');
  const ask = page.getByRole('alertdialog');
  await c.getByRole('radio', { name: 'Beta' }).click();
  await expect(ask.getByRole('heading', { name: 'Switch to the beta channel?' })).toBeVisible();
  await expect(ask.getByRole('listitem')).toHaveCount(4);
  await ask.getByRole('button', { name: 'Cancel' }).click();
  await expect(c.getByRole('radio', { name: 'Stable' })).toHaveAttribute('aria-checked', 'true');

  await c.getByRole('radio', { name: 'Beta' }).click();
  await ask.getByRole('button', { name: 'Switch to beta' }).click();
  await expect(c.getByText(/^Beta channel: release candidates may contain bugs/)).toBeVisible();
  await expect(c.getByText('New: 0.7.0-rc.1')).toBeVisible();
  await c.getByRole('radio', { name: 'Stable' }).click();  // back without asking
  await expect(c.getByText('New: 0.6.0')).toBeVisible();

  await c.getByRole('switch', { name: /^Check daily/ }).uncheck();
  await expect.poll(async () => (await (await page.request.get('/api/v1/ota/settings')).json()).auto_check).toBe(false);
});

test.describe('engineering', () => {
  test.use({ allowErrors: /status of 40[03] .*\/api\/v1\/ota\// });

  test('update source, http and update from a URL', async ({ page, cleanup }) => {
    reset(page, cleanup);
    await unlockEngineering(page, cleanup);
    const src = card(page, 'Update source');
    const field = src.getByRole('textbox', { name: /^Address of the release list/ });
    await field.fill('https://example.com/releases.txt');  // not a release list
    await src.getByRole('button', { name: 'Save' }).click();
    await expect(src.getByRole('status')).toHaveText('Saved – checking now.');

    // a URL update: http is blocked until allowed, a failed download is reported
    const url = card(page, 'Update from a URL');
    const addr = url.getByRole('textbox', { name: /^Address of the firmware file/ });
    const start = url.getByRole('button', { name: 'Download and install' });
    await addr.fill('http://192.168.1.10/firmware.bin');
    await expect(url.getByText(/^http:\/\/ addresses are blocked/)).toBeVisible();
    await expect(start).toBeDisabled();
    await addr.fill('https://example.com/firmware.txt');
    await start.click();
    await expect(url.getByRole('status')).toHaveText('Update failed: The server refused the download (HTTP 404)');

    const http = src.getByRole('switch', { name: /^Allow unencrypted http/ });
    const ask = page.getByRole('alertdialog');
    await http.click();
    await expect(ask).toContainText('Allow firmware downloads over unencrypted http?');
    await ask.getByRole('button', { name: 'Cancel' }).click();
    await expect(http).not.toBeChecked();
    await http.click();
    await ask.getByRole('button', { name: 'Confirm' }).click();
    await expect(src.getByRole('status')).toHaveText('Saved.');
    await expect(http).toBeChecked();
    // the dialog gives the focus back a frame after it closed; text typed before that can be lost
    // when the focus moves away under it (seen in Firefox), so type until it is in the field
    await expect(async () => {
      await addr.fill('http://192.168.1.10/firmware.bin');
      await expect(addr).toHaveValue('http://192.168.1.10/firmware.bin', { timeout: 500 });
    }).toPass();
    await expect(url.getByText(/^Unencrypted \(http\): the file can be altered/)).toBeVisible();
    await start.click();
    await expect(ask).toContainText('Download the firmware over unencrypted http?');
    await ask.getByRole('button', { name: 'Cancel' }).click();

    // maintenance: the failed check of the source and the http warning (pro view)
    await open(page, '#/maintenance');
    await proMode(page);
    await expect(page.getByText(/^http is allowed: firmware downloads over http are not protected/).first()).toBeVisible();
    await expect(card(page, 'Updates').getByText('Check failed: The server refused the download (HTTP 404)')).toBeVisible();

    // no update source: maintenance has no update card
    await open(page, '#/engineering');
    await src.getByRole('textbox', { name: /^Address of the release list/ }).fill('');
    await src.getByRole('button', { name: 'Save' }).click();
    await expect(src.getByRole('status')).toHaveText('Update source removed.');
    await open(page, '#/maintenance');
    await expect(card(page, 'Firmware update')).toBeVisible();
    await expect(card(page, 'Updates')).toHaveCount(0);
  });
});

test('a failed update says that the device went back', async ({ page, cleanup }) => {
  reset(page, cleanup);
  await page.request.post('/mock/firmware?rolledback=1');
  await open(page, '#/maintenance');
  await expect(card(page, 'Device').getByText(
    'The last update did not start – the device went back to the previous firmware (ota_1 is marked invalid).')).toBeVisible();
});

test('diagnostics (pro view): crash report and log', async ({ page, cleanup }) => {
  reset(page, cleanup);
  await open(page, '#/maintenance');
  await proMode(page);
  const c = card(page, 'Diagnostics');
  await expect(c.getByText('A crash report (coredump) is stored.')).toBeVisible();
  await expect(c.getByRole('link', { name: 'Download' })).toHaveAttribute('href', '/api/v1/system/coredump');
  await c.getByRole('button', { name: 'Delete' }).click();
  await expect(c.getByRole('status')).toHaveText('Crash report deleted.');
  await expect(c.getByText('No crash report stored.')).toBeVisible();
  await expect(c.getByRole('link', { name: 'Download' })).toHaveCount(0);

  await c.getByRole('button', { name: 'Show log' }).click();
  await expect(c.locator('pre.log')).toContainText('net: LAN up 192.168.1.50');
  await c.getByRole('button', { name: 'Hide log' }).click();
  await expect(c.locator('pre.log')).toHaveCount(0);
});
