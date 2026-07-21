# Security and privacy

## Wi-Fi policy (3.0.0)

- `smalltv_ultra` (the Wi-Fi usage display firmware) connects to the user's
  LAN. Wi-Fi credentials live in `/config.json` on the device and never leave
  the LAN. The firmware stores NO provider token: Codex/z.ai credentials stay
  on the Mac (in `~/.codex/auth.json` and `~/.claude/settings.json`) and only
  the resulting usage percentages cross the LAN to the device.
- `clock_usb` (the USB-only reference firmware) has no Wi-Fi code at all.
  Never add any.

## Secrets handling (unchanged)

- `tools/wifi-usage.toml`, `tools/usage-collector.toml`,
  `tools/usage-collector-state.json`, `tools/.bin/`, firmware backups, and
  `.env*` are must-stay-local. `.gitignore` enforces this.
- Never log tokens, Authorization headers, account ids, or full provider
  responses. The Mac service's structured logs carry only provider name,
  timestamp, HTTP status, and error category.

Before contributing or publishing, run `git status --ignored --short` and make
sure these remain local only:

- `tools/usage-collector.toml` and `tools/usage-collector-state.json`
- `tools/.bin/` and any Keychain-derived helper
- firmware images, especially an original-device flash backup
- `.env*`, private keys, and generated build output

The provider adapters may read an existing local OAuth credential only in
process memory to obtain a percentage. Never commit, print, attach, or paste a
credential, private configuration, collector state, device backup, or log in an
issue. If a secret is committed, revoke it first, then remove it from Git
history before making the repository public.

The `clock_usb` browser control panel listens only on `127.0.0.1`. Keep it
loopback-only.

To report a vulnerability, contact the repository owner privately rather than
opening a public issue with exploit details or sensitive data.
