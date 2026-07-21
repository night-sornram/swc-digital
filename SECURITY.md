# Security and privacy

## Wi-Fi policy (3.0.0)

- `smalltv_ultra` (the Wi-Fi usage display firmware) connects to the user's
  LAN. Wi-Fi credentials live in `/config.json` on the device and never leave
  the LAN. The firmware stores NO provider token: Codex/z.ai credentials stay
  on the Mac (in `~/.codex/auth.json` and `~/.claude/settings.json`) and only
  the resulting usage percentages cross the LAN to the device.
- `clock_usb` (the USB-only reference firmware) has no Wi-Fi code at all.
  Never add any.

## Wi-Fi portal auth (3.1+)

The `smalltv_ultra` Wi-Fi portal uses HTTP Digest auth (RFC 2617 qop=auth)
with username `admin` and a 16-char Crockford-Base32 pairkey the owner
generates during pairing. The device stores only `MD5(admin:realm:pairkey)`
(the H1), never the plaintext pairkey.

What this stops:
- A friend/roommate on the same Wi-Fi controlling the device or pushing
  fake usage values.
- The pairkey crossing the wire on every request (Digest sends an H1 of
  the request + a nonce, not the secret).

What this does NOT stop:
- A passive network sniffer CAN still read the (unencrypted) usage
  percentages and config GET responses. The threat model is casual
  same-network abuse, not a targeted attacker. Do not enable remote
  forwarding of port 80.
- A Wi-Fi administrator can ARP-spoof and MITM the device.

The pairkey lives in macOS Keychain under
`com.night.swc-digital.device-<id>`, account `pairkey`. Never write it to a
file, never paste it into a chat or issue.

`clock_usb` (the USB-only firmware) has no Wi-Fi code at all and is
unaffected.

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
