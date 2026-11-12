# Security

## Supported versions

Security fixes are made for the latest released firmware version. Devices with an update
source offer it on the maintenance page and in Home Assistant.

## Reporting a vulnerability

Please **do not open a public issue**. Instead, file a confidential report in the repository
under **Security → Report a vulnerability** (GitHub private vulnerability reporting). Reports
in English or German are welcome.

Helpful details:

- firmware version and module variant (maintenance page),
- what an attacker needs (e.g. access to the home network, a sign-in, physical access),
- steps to reproduce and the impact.

Please do not send real passwords, API keys or engineering keys. The report is acknowledged,
checked and – if confirmed – fixed in a new version; the issue is then published in the
[changelog](CHANGELOG.md) and, with credit if you like, in an advisory.

## Security model

The doser is built for the **home network**. What it protects:

- **Access:** own admin password (PBKDF2 hash), sessions in RAM only, cookie with
  `SameSite=Strict`, changing browser requests only from the device's own pages, throttling of
  password guessing, API keys stored only as SHA-256.
- **Device secret per device** in the `fctry` partition; the device password (rating plate,
  setup Wi-Fi) and the engineering key are derived from it. Firmware images contain no secret
  and may be public.
- **Updates:** images are checked before writing (chip, project, flash size) and verified by
  hash after writing; from the update source only with a matching SHA-256 from the manifest and
  only over https with a verified certificate (http only after explicit permission in the
  engineering menu). A failed start leads to a rollback.
- **External input** (API, MQTT, backup files, manifest, DNS in the setup Wi-Fi) is fully
  validated; the parsers run regularly under fuzzers and sanitizers.

Known limits, deliberately accepted:

- **Web UI and API use plain http** (no TLS). Someone listening in the home network can see the
  password and session. **Do not expose the device to the internet** (no port forwarding); use
  a VPN for remote access.
- **No secure boot, no flash encryption.** Anyone holding the device can read out the flash
  (including the device secret) and install their own firmware.
- **Firmware images are not signed.** Whoever is signed in as admin or holds an API key can
  install any firmware – protect password and keys accordingly.
- **MQTT without TLS** sends credentials and commands in clear text. "Allow control via MQTT"
  lets anyone with write access to the topics start pumps – only enable it with a protected
  broker.

## Advice for owners

- Choose an admin password that is not used anywhere else.
- Delete unused API keys; treat backups "with passwords" like passwords.
- Keep the firmware up to date.
- Never publish passwords, API keys or engineering keys in issues or logs.
