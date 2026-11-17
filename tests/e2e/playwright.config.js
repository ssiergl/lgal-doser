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
  webServer: [mock(8771), mock(8772, 'MOCK_FACTORY=1')],
  projects: [
    { name: 'desktop', use: { ...devices['Desktop Chrome'], baseURL: 'http://127.0.0.1:8771' }, grepInvert: /@phone/ },
    { name: 'phone', use: { ...devices['Pixel 7'], baseURL: 'http://127.0.0.1:8771' }, grep: /@phone/ },
  ],
});
