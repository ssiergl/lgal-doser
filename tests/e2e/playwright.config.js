// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: GPL-3.0-or-later

// End-to-end tests of the web UI in a real browser (Chromium) against the device mock
// (tools/mock/webui_server.py). Each run starts its own mocks on ports of their own, so a mock
// running for development on 8767 is not touched. From the repository root:
//   npm ci --prefix tests/e2e
//   npx --prefix tests/e2e playwright install chromium     (once)
//   npx --prefix tests/e2e playwright test -c tests/e2e
// The mock keeps one shared state, so the tests run one after another.
import { defineConfig, devices } from '@playwright/test';

const root = new URL('../..', import.meta.url).pathname;
const mock = (port, env = '') => ({
  command: `${env} python3 tools/mock/webui_server.py . ${port}`.trim(),
  cwd: root,
  url: `http://127.0.0.1:${port}/login`,
  reuseExistingServer: false,
  stdout: 'ignore',
});

// WebKit without a graphics card (Linux CI) cannot keep up with the endless animations of the
// page (reef, bubbles, waves); with less motion asked for, the page stops them. It still draws a
// frame in about a second, and every click waits for two still frames, hence more time per test.
const webkit = { baseURL: 'http://127.0.0.1:8771', reducedMotion: 'reduce' };
const webkitTimeout = 90_000;

export default defineConfig({
  testDir: './specs',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: process.env.CI ? [['list'], ['html', { open: 'never', outputFolder: 'report' }]] : 'list',
  outputDir: 'results',
  use: {
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    locale: 'en-GB',
  },
  // 8773/8774: board v1.1 without 24 V supply (USB power only) and with a supply that is too low
  webServer: [mock(8771), mock(8772, 'MOCK_FACTORY=1'),
    mock(8773, 'MOCK_BOARD_REV=1.1 MOCK_SUPPLY=8'), mock(8774, 'MOCK_BOARD_REV=1.1 MOCK_SUPPLY=18')],
  // Chromium runs everything; WebKit (Safari, and every browser on an iPhone) runs the same flows
  // except the axe checks (they do not depend on the engine) and the one-time factory setup.
  // @phone tests run only at phone size.
  projects: [
    { name: 'desktop', use: { ...devices['Desktop Chrome'], baseURL: 'http://127.0.0.1:8771' }, grepInvert: /@phone/ },
    { name: 'phone', use: { ...devices['Pixel 7'], baseURL: 'http://127.0.0.1:8771' }, grep: /@phone/ },
    { name: 'safari', use: { ...devices['Desktop Safari'], ...webkit }, grepInvert: /@phone|@axe|@factory/, timeout: webkitTimeout },
    { name: 'iphone', use: { ...devices['iPhone 15'], ...webkit }, grep: /@phone/, timeout: webkitTimeout },
  ],
});
