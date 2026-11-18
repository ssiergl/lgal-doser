# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

# Firefox for the end-to-end tests in a Debian container, as in the CI job. For hosts where
# Playwright's Firefox does not start (macOS 27). Build and run from the repository root:
#   docker build -t lgal-e2e-firefox -f tests/e2e/firefox.Dockerfile tests/e2e
#   docker run --rm -v "$PWD":/repo -w /repo lgal-e2e-firefox \
#     node tests/e2e/node_modules/@playwright/test/cli.js test -c tests/e2e --project firefox
FROM debian:trixie
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    ca-certificates python3 nodejs npm && rm -rf /var/lib/apt/lists/*
ENV PLAYWRIGHT_BROWSERS_PATH=/ms-playwright
WORKDIR /opt/e2e
COPY package.json package-lock.json ./
RUN npm ci --ignore-scripts --no-audit --no-fund && npx playwright install --with-deps firefox \
    && rm -rf /var/lib/apt/lists/*
