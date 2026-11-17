// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// MQTT / Home Assistant (settings page): status for everyone, broker, TLS with an own CA,
// discovery, remote control and moving the topics in the pro view. The mock connects 2 s after
// a save; a host with "auth" in its name is refused.
import { expect, open, proMode, test } from './fixtures.js';

test.use({ allowErrors: /status of 40[09] .*\/api\/v1\/mqtt/ });

const card = page => page.locator('form.card, section.card')
  .filter({ has: page.getByRole('heading', { name: 'MQTT / Home Assistant', level: 2, exact: true }) });
const state = c => c.locator('dt', { hasText: /^Connection$/ }).locator('xpath=following-sibling::dd[1]');
const message = c => c.locator(':scope > [role=status]');
const pem = n => Array.from({ length: n }, (_, i) =>
  `-----BEGIN CERTIFICATE-----\nMIIBtest${i}\n-----END CERTIFICATE-----\n`).join('');

// Settings and CA as at the start
async function keep(page, cleanup) {
  const { settings: { password_set, ...s } } = await (await page.request.get('/api/v1/mqtt')).json();
  cleanup(async () => {
    await page.request.put('/api/v1/mqtt', { data: { ...s, password: '' } });
    await page.request.delete('/api/v1/mqtt/ca');
  });
}

async function openPro(page) {
  await open(page, '#/settings');
  await proMode(page);
  const c = card(page);
  await expect(c.getByRole('textbox', { name: 'Broker' })).toBeVisible();
  return c;
}

test('the simple view shows the connection only', async ({ page }) => {
  await open(page, '#/settings');
  const c = card(page);
  await expect(state(c)).toHaveText('off');
  await expect(c.getByText('MQTT is set up in the Pro view.')).toBeVisible();
  await expect(c.getByRole('textbox')).toHaveCount(0);
});

test('broker: checked, connects, a refused login is shown', async ({ page, cleanup }) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  const host = c.getByRole('textbox', { name: 'Broker' });
  const port = c.getByRole('textbox', { name: 'Port' });
  const save = c.getByRole('button', { name: 'Save' });
  await c.getByRole('switch', { name: 'Connect to an MQTT broker' }).check();
  await save.click();
  await expect(message(c)).toHaveText('Please check: Broker');
  await expect(host).toBeFocused();
  await expect(host).toHaveClass(/bad/);
  await host.fill('not a host!');
  await save.click();
  await expect(message(c)).toHaveText('Please check: Broker');

  // the port follows the encryption; only digits go in
  await expect(port).toHaveAttribute('placeholder', '1883');
  await c.getByRole('combobox', { name: 'Encryption' }).selectOption('bundle');
  await expect(port).toHaveAttribute('placeholder', '8883');
  await expect(c.getByText(/^For brokers with a certificate from a public authority/)).toBeVisible();
  await port.fill('88a83');
  await expect(port).toHaveValue('8883');
  await port.fill('');

  // the password needs a user
  const password = c.getByLabel('Password', { exact: true });
  await expect(password).toBeDisabled();
  await c.getByRole('textbox', { name: 'User' }).fill('doser');
  await expect(password).toBeEnabled();
  await password.fill('mqtt-secret');

  await host.fill('broker.example.com');
  await save.click();
  await expect(message(c)).toHaveText('Saved. The connection is being re-established.');
  await expect(host).not.toHaveClass(/bad/);
  await expect(password).toHaveValue('');
  await expect(password).toHaveAttribute('placeholder', 'unchanged');
  await expect(state(c)).toHaveText('connected to broker.example.com:8883', { timeout: 10_000 });

  await host.fill('auth.example.com');
  await save.click();
  await expect(state(c)).toHaveText(/^connecting to auth\.example\.com:8883 …\s*last error: user or password rejected$/, { timeout: 10_000 });
});

test('own CA certificate: refused, uploaded, in use, removed', async ({ page, cleanup }, info) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  await c.getByRole('switch', { name: 'Connect to an MQTT broker' }).check();
  await c.getByRole('textbox', { name: 'Broker' }).fill('mqtt.lan');
  await c.getByRole('combobox', { name: 'Encryption' }).selectOption('custom');
  await expect(c.getByText(/^For your own broker with a self-signed certificate/)).toBeVisible();
  await expect(c.getByText('none uploaded')).toBeVisible();
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Upload a CA certificate first');

  const file = c.locator('input[type=file]');
  await file.setInputFiles({ name: 'ca.pem', mimeType: 'application/x-pem-file', buffer: Buffer.from('no certificate') });
  await expect(message(c)).toHaveText('Not a valid PEM certificate');
  await file.setInputFiles({ name: 'ca.pem', mimeType: 'application/x-pem-file', buffer: Buffer.from(pem(2)) });
  await expect(message(c)).toHaveText('Certificate saved.');
  await expect(c.getByText('Test Root CA, valid until 2036-11-07 (+1 more)')).toBeVisible();
  await expect(c.getByRole('button', { name: 'Replace …' })).toBeVisible();

  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Saved. The connection is being re-established.');
  await expect(state(c)).toHaveText('connected to mqtt.lan:8883', { timeout: 10_000 });
  await c.getByRole('button', { name: 'Remove' }).click();
  await expect(message(c)).toHaveText('The certificate is in use – choose another encryption first');

  await c.getByRole('combobox', { name: 'Encryption' }).selectOption('off');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(state(c)).toHaveText('connected to mqtt.lan:1883', { timeout: 10_000 });
  await c.getByRole('combobox', { name: 'Encryption' }).selectOption('custom');
  await c.getByRole('button', { name: 'Remove' }).click();
  await expect(message(c)).toHaveText('Certificate removed.');
  await expect(c.getByText('none uploaded')).toBeVisible();
  await c.getByRole('button', { name: 'Discard' }).click();
});

test('topics, discovery, remote control, moving to the device name', async ({ page, cleanup }) => {
  await keep(page, cleanup);
  const c = await openPro(page);
  const base = c.getByRole('textbox', { name: 'Base topic' });
  await expect(base).toHaveAttribute('placeholder', 'lg-aquatic-life/doser/lgal-doser-e4f5');
  await c.getByText('Topics', { exact: true }).click();
  const topics = c.locator('.mq-topics li');
  await expect(topics).toHaveCount(5);
  await expect(topics.first()).toHaveText('lg-aquatic-life/doser/lgal-doser-e4f5/status');

  // remote control adds the command topics and warns
  const control = c.getByRole('switch', { name: /^Allow control via MQTT/ });
  await control.check();
  await expect(topics).toHaveCount(10);
  await expect(c.getByText(/^Home Assistant – and anyone with access to the broker – can then start doses/)).toHaveClass(/warn/);
  await control.uncheck();

  const discovery = c.getByRole('switch', { name: /^Home Assistant discovery/ });
  const prefix = c.getByRole('textbox', { name: 'Discovery prefix' });
  await expect(prefix).toHaveValue('homeassistant');
  await prefix.fill('/bad/');
  await c.getByRole('switch', { name: 'Connect to an MQTT broker' }).check();
  await c.getByRole('textbox', { name: 'Broker' }).fill('mqtt.lan');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Please check: Discovery prefix');
  await expect(prefix).toBeFocused();
  await prefix.fill('homeassistant');
  await discovery.uncheck();
  await expect(prefix).toBeHidden();

  // topics still on an earlier device name: moving them asks first
  await base.fill('lg-aquatic-life/doser/old-name');
  await expect(topics.first()).toHaveText('lg-aquatic-life/doser/old-name/status');
  await c.getByRole('button', { name: 'Save' }).click();
  await expect(message(c)).toHaveText('Saved. The connection is being re-established.');
  await expect(c.getByText('lg-aquatic-life/doser/old-name → lg-aquatic-life/doser/lgal-doser-e4f5')).toBeVisible();
  const ask = page.getByRole('alertdialog');
  await c.getByRole('button', { name: 'Move topics to the device name' }).click();
  await expect(ask).toContainText('Move all topics to lg-aquatic-life/doser/lgal-doser-e4f5?');
  await ask.getByRole('button', { name: 'Cancel' }).click();
  await c.getByRole('button', { name: 'Move topics to the device name' }).click();
  await ask.getByRole('button', { name: 'Move topics to the device name' }).click();
  await expect(message(c)).toHaveText('Topics moved to lg-aquatic-life/doser/lgal-doser-e4f5.');
  await expect(base).toHaveValue('lg-aquatic-life/doser/lgal-doser-e4f5');
  await expect(c.getByRole('button', { name: 'Move topics to the device name' })).toHaveCount(0);
  const saved = (await (await page.request.get('/api/v1/mqtt')).json()).settings;
  expect(saved).toMatchObject({ discovery: false, control: false, base_topic: 'lg-aquatic-life/doser/lgal-doser-e4f5' });
});
