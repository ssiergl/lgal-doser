// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// Emergency page (/recovery): plain HTML without the app, the way back when the web interface
// does not start. In the enclosure it is the last way to load a firmware. /mock/firmware puts the
// firmware back after each test.
import { expect, test } from './fixtures.js';

// While the device restarts, requests fail
test.use({ allowErrors: /ERR_|network connection was lost|Connection terminated unexpectedly|Could not connect|Load failed|access control checks|status of 400 .*\/api\/v1\/ota\/firmware/ });

const reset = (page, cleanup) => cleanup(() => page.request.post('/mock/firmware'));
const info = (page, name) => page.locator('#info tr').filter({ has: page.locator('td:first-child', { hasText: new RegExp(`^${name}$`) }) }).locator('td').nth(1);

test('device information, pages, log and crash report', async ({ page, cleanup }) => {
  reset(page, cleanup);
  await page.request.post('/mock/firmware?rolledback=1');
  await page.goto('/recovery');
  await expect(page.getByRole('heading', { name: 'Maintenance', level: 1 })).toBeVisible();
  await expect(info(page, 'Firmware')).toHaveText('0.5.0-dev (ota_0, VALID)');
  await expect(info(page, 'Rolled back')).toHaveText('ota_1 is invalid');
  await expect(info(page, 'Hostname')).toHaveText('lgal-doser-e4f5.local');
  await expect(info(page, 'Ethernet')).toHaveText('192.168.1.50');
  await expect(info(page, 'Coredump')).toHaveText('present');
  await expect(page.getByRole('link', { name: 'Download coredump' })).toHaveAttribute('href', '/api/v1/system/coredump');

  const pages = page.locator('#links a');
  await expect(pages).toHaveText(['Web interface', 'Setup wizard']);
  await expect(pages.first()).toHaveAttribute('href', '/');

  await page.getByRole('button', { name: 'Show log' }).click();
  await expect(page.locator('#log')).toContainText('net: LAN up 192.168.1.50');

  // the page follows the device language
  await page.locator('#lang button').first().click();
  await page.locator('#lang').getByText('Deutsch').click();
  await expect(page.getByRole('heading', { name: 'Wartung', level: 1 })).toBeVisible();
  await expect(info(page, 'Zurückgerollt')).toHaveText('ota_1 ist ungültig');
  await page.request.post('/api/v1/system/language?lang=en');
});

test('firmware upload: no file, wrong file, good file', async ({ page, cleanup }) => {
  test.slow();
  reset(page, cleanup);
  await page.goto('/recovery');
  await expect(info(page, 'Firmware')).toHaveText('0.5.0-dev (ota_0, VALID)');
  const upload = page.getByRole('button', { name: 'Upload & flash' });
  const msg = page.locator('#upmsg');
  await upload.click();
  await expect(msg).toHaveText('Please select a .bin file first.');

  const file = page.locator('#fw');
  await file.setInputFiles({ name: 'other.bin', mimeType: 'application/octet-stream', buffer: Buffer.alloc(64, 0x41) });
  await upload.click();
  await expect(msg).toHaveText('Error 400: This is not a firmware file');
  await expect(upload).toBeEnabled();

  // an ESP image starts with 0xE9; the page reloads 20 s after a good upload
  await file.setInputFiles({ name: 'lgal_doser.bin', mimeType: 'application/octet-stream',
    buffer: Buffer.concat([Buffer.from([0xe9]), Buffer.alloc(63)]) });
  await upload.click();
  await expect(msg).toHaveText('Update successful. The device is rebooting – the page reloads in 20 s.', { timeout: 10_000 });
  await expect(upload).toBeDisabled();
  await expect(msg).toBeEmpty({ timeout: 40_000 });  // reloaded
  await expect(info(page, 'Firmware')).toHaveText('0.5.1-dev (ota_1, VALID)');
});

test('reboot asks first', async ({ page, cleanup }) => {
  reset(page, cleanup);
  await page.goto('/recovery');
  await expect(info(page, 'Firmware')).toBeVisible();
  const asked = [];
  page.once('dialog', d => { asked.push(d.message()); d.dismiss(); });
  await page.getByRole('button', { name: 'Reboot' }).click();
  await expect.poll(() => asked).toEqual(['Reboot the device?']);
  await expect(page.locator('#upmsg')).toBeEmpty();

  page.once('dialog', d => d.accept());
  await page.getByRole('button', { name: 'Reboot' }).click();
  await expect(page.locator('#upmsg')).toHaveText('Rebooting …');
});
