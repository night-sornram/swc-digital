# SWC Digital 3.0.0 — Plan 4: CI, Docs, Release

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Author the GitHub Actions release workflow for the `v3.0.0` tag (builds `smalltv_ultra` + `smalltv_ultra_loader`, attaches the spec-named artifacts with SHA-256 sidecars, no ESP32 artifacts), delete the Astro `docs/` site (per user decision: it is upstream `smalltv-mod` content unrelated to this fork), and rewrite the top-level docs (`README.md`, `AGENTS.md`, `CLAUDE.md`, `USB_CLOCK.md`, `SECURITY.md`) to reflect the Wi-Fi push + SmallTV-ultra-only reality.

**Architecture:** One workflow file `.github/workflows/release.yml` triggered on `v*` tags. It builds two firmware images, renames them to the spec asset names, computes SHA-256, and attaches everything to the GitHub Release via `softprops/action-gh-release`. Docs become four flat Markdown files at the repo root. `docs/` and `n8n/` (already deleted in Plan 1) stay gone.

**Tech Stack:** GitHub Actions (`ubuntu-latest`), `uvx --from platformio platformio`, `uvx --from esptool esptool`, `softprops/action-gh-release@v2`, plain Markdown.

**Branch:** `feature/v3-usage-display`.

**Depends on:** Plan 1 (envs renamed, ESP32 stuff gone), Plan 2 (firmware identity 3.0.0), Plan 3 (Mac service + plist). Plan 4 can run in parallel with the *writing* of Plans 2/3 but the release step at the end (Task 5) requires physical acceptance from Plan 3 to be complete.

**Spec source:** `pasted-text-20260721-155906-89253a9e.txt` §Firmware identity, OTA and release + §Test plan/build gates (final acceptance), and the user decision "delete docs/".

---

## Pre-flight

- [ ] **P1: Confirm Plans 1–3 are merged or present on the branch.**

```sh
git log --oneline -30 | grep -E "v3:" | head -20
ls .github/                       # only scripts/ (empty after Plan 1) — no workflows/ yet
ls docs/ 2>/dev/null && echo "docs/ still present" || echo "docs/ already gone"
```

Expected: many `v3:` commits; `.github/workflows/` does not exist; `docs/` is still present (this plan deletes it).

- [ ] **P2: Confirm the release asset names match what Plan 2 set in `config.h`.**

```sh
grep -E "FW_NAME|FW_VERSION|UPDATE_ASSET|REPO_OWNER|REPO_NAME" src/config.h
```

Expected: `FW_NAME "swc-digital"`, `FW_VERSION "3.0.0"`, `UPDATE_ASSET "swc-digital-smalltv-ultra.bin"`, `REPO_OWNER "night-sornram"`, `REPO_NAME "swc-digital"`.

---

## File Structure

**Created:**
- `.github/workflows/release.yml` — tag-triggered release workflow.

**Deleted:**
- `docs/` (whole Astro site — upstream `smalltv-mod` content unrelated to this fork).
- `REMINDER_HANDOFF.md` — session hand-off doc, no longer accurate post-3.0.0 (USB-only policy language is stale). **Verify with the user before deleting in Task 4.**

**Rewritten:**
- `README.md` — strip upstream `smalltv-mod` content; describe SWC Digital 3.0.0 (SmallTV-ultra + USB clock + Wi-Fi usage push).
- `AGENTS.md` — update the "verified reference state" to 3.0.0; add the Wi-Fi usage push path; keep the USB clock + flashing rules; drop the "USB-only, ever" language that the Wi-Fi push contradicts.
- `CLAUDE.md` — align with AGENTS.md (drop Claude usage references, update the Wi-Fi policy).
- `USB_CLOCK.md` — light touch-ups (drop the BTC/Claude usage sections that reference removed tooling paths — verify they are still accurate first).
- `SECURITY.md` — scope the Wi-Fi policy (Wi-Fi is now allowed for the LAN push path; secrets-handling rules unchanged).

**Untouched:** all `src/`, `tools/`, `tests/`, `platformio.ini`, `.gitignore`.

---

## Task 1: Author the release workflow

**Files:**
- Create: `.github/workflows/release.yml`

**Spec rules (§Firmware identity, OTA and release):**
- Build `smalltv_ultra` and `smalltv_ultra_loader`.
- Check image-info/checksum.
- Name artifacts: `swc-digital-smalltv-ultra.bin`, `swc-digital-smalltv-ultra.bin.sha256`, `swc-digital-smalltv-ultra-loader.bin`, `swc-digital-smalltv-ultra-loader.bin.sha256`.
- Generate SHA-256.
- Attach artifacts to the release.
- Do NOT build ESP32 artifacts.
- Keep manual Web OTA upload; point one-click self-update at the user's fork (already done via `REPO_OWNER`/`REPO_NAME` in `config.h`).

- [ ] **Step 1.1: Create `.github/workflows/release.yml`.**

```yaml
# SWC Digital — release workflow.
# Triggered by a v* tag (e.g. v3.0.0). Builds the SmallTV-ultra production
# firmware and the recovery loader, names them per spec, computes SHA-256
# sidecars, and attaches all four artifacts to the GitHub Release.
#
# No ESP32 artifacts are produced: 3.0.0 dropped ESP32-C2 and NM-TV-154 support.
name: release

on:
  push:
    tags:
      - "v*"

permissions:
  contents: write   # required by softprops/action-gh-release to upload assets

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.11"

      - name: Install PlatformIO + esptool via uvx
        run: |
          python -m pip install --upgrade pip
          pip install uv

      - name: Build smalltv_ultra (production)
        run: uvx --from platformio platformio run -e smalltv_ultra

      - name: Build smalltv_ultra_loader (recovery)
        run: uvx --from platformio platformio run -e smalltv_ultra_loader

      - name: Verify images (image-info + checksum)
        run: |
          uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin
          uvx --from esptool esptool image-info .pio/build/smalltv_ultra_loader/firmware.bin

      - name: Stage artifacts with spec names
        run: |
          mkdir -p artifacts
          cp .pio/build/smalltv_ultra/firmware.bin        artifacts/swc-digital-smalltv-ultra.bin
          cp .pio/build/smalltv_ultra_loader/firmware.bin artifacts/swc-digital-smalltv-ultra-loader.bin

      - name: Generate SHA-256 sidecars
        working-directory: artifacts
        run: |
          sha256sum swc-digital-smalltv-ultra.bin        > swc-digital-smalltv-ultra.bin.sha256
          sha256sum swc-digital-smalltv-ultra-loader.bin > swc-digital-smalltv-ultra-loader.bin.sha256

      - name: Attach artifacts to the release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            artifacts/swc-digital-smalltv-ultra.bin
            artifacts/swc-digital-smalltv-ultra.bin.sha256
            artifacts/swc-digital-smalltv-ultra-loader.bin
            artifacts/swc-digital-smalltv-ultra-loader.bin.sha256
          generate_release_notes: true
          fail_on_unmatched_files: true
```

- [ ] **Step 1.2: Lint the workflow locally (syntax check via `actionlint` if available; otherwise YAML parse).**

```sh
python -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml'))" && echo "YAML OK"
```

Expected: `YAML OK`.

- [ ] **Step 1.3: Commit.**

```sh
git add .github/workflows/release.yml
git commit -m "ci(v3): tag-triggered release workflow for SmallTV-ultra (+loader) with SHA-256"
```

---

## Task 2: Delete the Astro docs site

**Files:**
- Delete: `docs/` (whole directory)

Per the user's earlier decision ("delete docs/"), the Astro site is upstream `smalltv-mod` content (ticker/radar/Claude/ESP32 variants) unrelated to this fork. README + USB_CLOCK.md cover this fork's users.

- [ ] **Step 2.1: Delete the directory.**

```sh
git rm -r docs
```

- [ ] **Step 2.2: Confirm no remaining reference to the docs site.**

```sh
grep -nR "astro.config\|smalltv-mod/" README.md AGENTS.md CLAUDE.md USB_CLOCK.md 2>/dev/null || echo "clean"
```

Expected: `clean` (or matches only inside historical context that Task 4 rewrites). Any live link to the Astro site in those docs gets fixed in Task 4.

- [ ] **Step 2.3: Commit.**

```sh
git commit -m "docs(v3): delete upstream smalltv-mod Astro site (unrelated to this fork)"
```

---

## Task 3: Rewrite `README.md` for SWC Digital 3.0.0

**Files:**
- Rewrite: `README.md`

- [ ] **Step 3.1: Read the current README to preserve the USB-clock half.**

```sh
sed -n '1,60p' README.md        # top half = upstream smalltv-mod
grep -n "PHUD USB\|USB Smart Weather" README.md   # find the USB clock section start
```

The README currently has two halves: upstream `smalltv-mod` (ESP32 firmware with ticker/radar/Claude) on top, and `## PHUD USB Smart Weather Clock` (the USB clock) on the bottom. The bottom half is still accurate for `clock_usb`. The top half is entirely stale post-3.0.0.

- [ ] **Step 3.2: Rewrite `README.md` end-to-end.**

Structure the new README as:

```markdown
# SWC Digital

Custom firmware for the GeekMagic SmallTV-ultra (ESP-12F / ESP8266, 1.54" 240×240 ST7789).

Two firmware targets, both for the same hardware:

- **`smalltv_ultra`** — Wi-Fi firmware that displays live Codex and z.ai usage.
  Three screen modes: `CODEX`, `Z.AI`, and `AUTO` (alternates every 30 s).
  A Mac service pushes usage data over the LAN every 60 s.
- **`clock_usb`** — USB-only recovery / reference firmware. No Wi-Fi, no cloud,
  configured entirely over the onboard CH340 serial bridge. See `USB_CLOCK.md`.

The recovery loader (`smalltv_ultra_loader`) is a tiny Wi-Fi + web-OTA image used
to slip a full firmware past a stock updater that rejects the full image for
lack of space.

## Hardware

- MCU: ESP8266EX, 26 MHz crystal, 4 MB flash (DIO, 40 MHz).
- USB bridge: CH340/CH341.
- Display: ST7789, 240×240, SPI mode 3.
- Pin map: see `src/board_smalltv_ultra.h` (SCLK=14, MOSI=13, DC=0, RST=2, CS=15, BL=5).

Only SmallTV-ultra is supported. ESP32-C2 and NM-TV-154 support was removed in 3.0.0.

## Build

```sh
uvx --from platformio platformio run -e smalltv_ultra         # production
uvx --from platformio platformio run -e smalltv_ultra_loader  # recovery loader
uvx --from platformio platformio run -e clock_usb             # USB-only reference
```

## Flash

```sh
uvx --from esptool esptool \
  --port "$(uv run --with pyserial tools/clockctl.py find-port)" --baud 115200 \
  write-flash 0x0 .pio/build/smalltv_ultra/firmware.bin
```

Always use 115200 baud. The CH340 auto-reset wiring resets the ESP8266 each time
a serial session opens.

## Usage display (the Wi-Fi firmware)

The device exposes:

- `POST /api/usage` — the Mac service pushes one body per provider here.
- `GET /api/usage` — read both providers' snapshots.
- `_aiusage._tcp` mDNS service for discovery.
- A web UI on port 80 for mode / brightness / Wi-Fi / OTA settings.

See `docs/superpowers/plans/` for the design and `tools/wifi-usage.toml.example`
for the Mac service config.

## Mac usage service

`tools/wifi_usage_service.py` polls Codex (`~/.codex/auth.json`) and z.ai
(`~/.claude/settings.json`) every 60 s and pushes each provider to every
SmallTV-ultra it discovers on the LAN. Per-provider backoff on 429/5xx. Never
logs tokens, headers, or full responses.

Install as a LaunchAgent using `tools/com.night.swc-digital-wifi-usage.plist.example`.

## USB clock (`clock_usb`)

See `USB_CLOCK.md` for the full build / flash / restore / CLI guide. The USB
clock is a separate firmware line and does not share the Wi-Fi usage code.

## Recovery

Before first flashing a device, make a 4 MB stock-flash backup and store it
outside the repo. Never commit it — it may contain saved Wi-Fi credentials.

## License

WTFPL (inherited from `giovi321/smalltv-mod`). The custom USB firmware and the
Wi-Fi usage display are isolated behind the `clock_usb` and `smalltv_ultra`
PlatformIO targets respectively.
```

Preserve any badges only if they still point at live URLs (the old CI/docs badges were deleted upstream — do not re-add dead badges).

- [ ] **Step 3.3: Commit.**

```sh
git add README.md
git commit -m "docs(v3): rewrite README for SWC Digital 3.0.0 (SmallTV-ultra + USB clock)"
```

---

## Task 4: Update `AGENTS.md`, `CLAUDE.md`, `USB_CLOCK.md`, `SECURITY.md`

**Files:**
- Modify: `AGENTS.md`
- Modify: `CLAUDE.md`
- Modify: `USB_CLOCK.md` (light)
- Modify: `SECURITY.md`
- Delete (with user confirmation): `REMINDER_HANDOFF.md`

- [ ] **Step 4.1: `AGENTS.md` — update the verified reference state + scope the Wi-Fi policy.**

Read the current `AGENTS.md` (it is the file in your context). The big edits:

1. **Update "Verified reference state"** to a 3.0.0 entry. Add a dated section like:

```markdown
## Verified reference state (2026-07-21, 3.0.0)

- `smalltv_ultra` Wi-Fi firmware is flashed and running on the physical clock.
- Three modes — CODEX, Z.AI, AUTO — render the spec layout. AUTO flips every
  30 s. Codex shows Weekly (5H is N/A for this account); z.ai shows both
  5H and Weekly.
- The Mac Wi-Fi usage service (`tools/wifi_usage_service.py`) pushes both
  providers every 60 s via `_aiusage._tcp` mDNS.
- After 180 s without a push, the active provider shows STALE (dimmed, last
  value retained); a successful push restores LIVE within one poll.
- `clock_usb` remains the USB-only recovery firmware, unchanged.
- The user visually confirmed orientation, colour, brightness, readability,
  and smoothness (no flicker, no full-screen redraw per second).
```

Keep all the older verified-state entries below it (they are historical truth about the USB clock line).

2. **Update "Project goal"** to acknowledge the dual firmware line:

```markdown
The repository now carries two firmware lines for the same hardware:
the Wi-Fi usage display (`smalltv_ultra`) and the USB-only clock
(`clock_usb`). The USB clock's baseline rules below still apply to the
`clock_usb` target; the Wi-Fi firmware follows the spec in
`docs/superpowers/plans/`.
```

3. **Drop the "USB-only, no Wi-Fi, ever" language** from the top of the file. The Wi-Fi push is now an explicit, supported feature of `smalltv_ultra`. The USB-only rule still applies to `clock_usb` specifically — say so.

4. **Update "Important files"** to add `tools/wifi_usage_service.py`, `tools/codex_wifi_adapter.py`, `tools/zai_wifi_adapter.py`, `tools/aiusage_mdns.py`, and the new `src/features/usage/` files. Drop references to deleted features.

5. **Update "Likely next improvements"** to drop items already done in 3.0.0.

- [ ] **Step 4.2: `CLAUDE.md` — mirror AGENTS.md changes.**

Open `CLAUDE.md`. It is a shorter session hand-off. Apply the same scope changes: dual firmware line, Wi-Fi push is supported for `smalltv_ultra`, USB-only rule is `clock_usb`-specific, drop Claude-usage-tooling references (the USB collector still has Claude slots — do NOT delete those, just stop referencing them as a feature).

- [ ] **Step 4.3: `USB_CLOCK.md` — verify and lightly touch up.**

```sh
grep -nE "Ticker|Radar|mascot|claude_usage|claude_profile_vault|usageUrl" USB_CLOCK.md || echo "clean"
```

Expected: `clean` (USB_CLOCK.md should already be USB-clock-scoped). If any matches appear, rewrite those paragraphs to reflect the current USB clock tooling only. Do not otherwise change this file — it is accurate for `clock_usb`.

- [ ] **Step 4.4: `SECURITY.md` — scope the Wi-Fi policy.**

Open `SECURITY.md` (24 lines). It currently hard-asserts "keep the clock USB-only. Do not add Wi-Fi credentials, cloud tokens, or account credentials to the firmware." Scope this to `clock_usb`:

```markdown
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
```

- [ ] **Step 4.5: `REMINDER_HANDOFF.md` — confirm with the user before deleting.**

```sh
head -20 REMINDER_HANDOFF.md
```

`REMINDER_HANDOFF.md` is a session hand-off doc from an earlier USB-clock session. Its specifics are likely stale post-3.0.0. **Ask the user** whether to delete it, archive it under `docs/superpowers/`, or rewrite it. Do not delete unilaterally — it may contain USB-clock context the user wants to keep.

- [ ] **Step 4.6: Commit.**

```sh
git add AGENTS.md CLAUDE.md USB_CLOCK.md SECURITY.md
git commit -m "docs(v3): scope Wi-Fi policy to smalltv_ultra; update agent guides for 3.0.0"
```

---

## Task 5: Final build-gate + spec acceptance

**Files:** none modified — final verification.

- [ ] **Step 5.1: All build gates (spec §build gates).**

```sh
uvx --from platformio platformio run -e smalltv_ultra         2>&1 | tail -6
uvx --from platformio platformio run -e smalltv_ultra_loader  2>&1 | tail -6
uvx --from platformio platformio run -e clock_usb             2>&1 | tail -6
git diff --check
uvx --from esptool esptool image-info .pio/build/smalltv_ultra/firmware.bin         2>&1 | tail -15
uvx --from esptool esptool image-info .pio/build/smalltv_ultra_loader/firmware.bin  2>&1 | tail -15
```

Expected: all `[SUCCESS]`; `git diff --check` clean; both image-info report ESP8266 / 4 MB / DIO / valid checksum.

- [ ] **Step 5.2: All host tests.**

```sh
cd tests && python -m unittest test_usage_store test_settings_migration test_wifi_adapters test_clock_tools 2>&1 | tail -5 && cd ..
```

Expected: all suites pass. (`test_clock_tools.py` is the existing USB tools test — it must still pass because the USB tools are untouched.)

- [ ] **Step 5.3: No project-code compiler warnings.**

```sh
uvx --from platformio platformio run -e smalltv_ultra 2>&1 | grep -i "warning:" | grep -v "elf2bin\|SyntaxWarning\|platformio/packages" || echo "no project warnings"
```

Expected: `no project warnings`.

- [ ] **Step 5.4: No ESP32 remnants anywhere in the repo.**

```sh
grep -nR "smalltv_c2\|smalltv_esp32\|board_esp32\|NM-TV-154\|pioarduino\|esp32-c2\|esp32c2\|esp32dev" . --include="*.ini" --include="*.h" --include="*.cpp" --include="*.md" --include="*.yml" 2>/dev/null | grep -v "\.git/" || echo "clean"
```

Expected: `clean` (historical mentions inside `.git/` are fine; ignore those).

- [ ] **Step 5.5: Footprint check (spec: "smaller than ~682 KB baseline").**

```sh
ls -la .pio/build/smalltv_ultra/firmware.bin
```

Expected: well under 682 KB. Record the exact size.

- [ ] **Step 5.6: Confirm the release workflow file is syntactically valid and references only surviving envs.**

```sh
grep -E "smalltv_ultra|smalltv_ultra_loader|clock_usb|esp32" .github/workflows/release.yml
```

Expected: only `smalltv_ultra` and `smalltv_ultra_loader` appear; no `esp32`, no `clock_usb` (the release workflow ships only the Wi-Fi firmware + loader).

- [ ] **Step 5.7: Physical re-acceptance (quick smoke).**

If the device is still running the Plan 3 candidate:

```sh
curl -s http://<hostname>.local/api/status | python -m json.tool | grep -E "fw|version|hardware"
```

Expected: `swc-digital` / `3.0.0` / `SmallTV-ultra`. No need to re-flash unless the firmware changed since Plan 3 Task 7.

- [ ] **Step 5.8: Do NOT tag or publish.** The spec is explicit: tag/release `v3.0.0` only after physical acceptance passes. The user does that manually.

---

## Task 6: Manual tag + release (user-driven, after sign-off)

**This task is for the user, not the implementer.** It runs only after Plan 3's physical acceptance and Plan 4's build gates both pass.

- [ ] **Step 6.1: (User) Merge `feature/v3-usage-display` to `main`.**

```sh
git checkout main
git merge --no-ff feature/v3-usage-display
```

- [ ] **Step 6.2: (User) Tag `v3.0.0`.**

```sh
git tag -a v3.0.0 -m "SWC Digital 3.0.0 — SmallTV-ultra usage display (CODEX/ZAI/AUTO) + Wi-Fi push"
git push origin main
git push origin v3.0.0
```

- [ ] **Step 6.3: (User) Watch the release workflow.**

```sh
gh run watch
gh release view v3.0.0
```

Expected: the workflow builds both images, computes SHA-256 sidecars, and attaches four assets:
- `swc-digital-smalltv-ultra.bin`
- `swc-digital-smalltv-ultra.bin.sha256`
- `swc-digital-smalltv-ultra-loader.bin`
- `swc-digital-smalltv-ultra-loader.bin.sha256`

- [ ] **Step 6.4: No commit (user-driven task).**

---

## Self-Review

- [ ] **Coverage of spec §Firmware identity, OTA and release:**
  - `FW_NAME/FW_VERSION/REPO_OWNER/REPO_NAME/UPDATE_ASSET` set in `config.h`? — Plan 2 Task 4.3. ✓
  - Release artifacts named per spec? ✓ Task 1 (`stage artifacts` step).
  - SHA-256 sidecars generated? ✓ Task 1.
  - Attached to the release? ✓ Task 1 (`softprops/action-gh-release`).
  - No ESP32 artifacts? ✓ Task 1 (only two envs built).
  - Manual Web OTA kept? — Untouched (WebPortal `/update` route unchanged through Plans 1–3).
  - One-click self-update points at user's fork? — `REPO_OWNER=night-sornram` in `config.h` (Plan 2 Task 4.3). ✓

- [ ] **Coverage of the user's "delete docs/" decision:** ✓ Task 2.

- [ ] **Coverage of spec §Test plan / build gates:** ✓ Task 5 (every line of the build-gate block runs).

- [ ] **Placeholder scan:** the README content in Task 3.2 is a complete draft, not a placeholder. The `REMINDER_HANDOFF.md` decision in Task 4.5 is explicitly deferred to the user (not a plan placeholder — it is a documented user-decision point). Every other step has concrete commands.

- [ ] **Type consistency:** no types cross between this plan and the firmware plans. The release asset names (`swc-digital-smalltv-ultra{,-loader}.bin`) match `UPDATE_ASSET` in `config.h` (Plan 2 Task 4.3) and the OTA self-update code reads `UPDATE_ASSET` to find the asset in a release — if you change one, change both.

---

## Done criteria for Plan 4

1. `.github/workflows/release.yml` exists, parses as YAML, references only `smalltv_ultra` and `smalltv_ultra_loader`.
2. `docs/` is gone.
3. `README.md` describes SWC Digital 3.0.0 (dual firmware line, Wi-Fi usage display, USB clock).
4. `AGENTS.md`/`CLAUDE.md`/`SECURITY.md` scope the Wi-Fi policy correctly.
5. All build gates pass; all host tests pass; no project-code warnings; no ESP32 remnants.
6. The user has signed off on physical acceptance. The tag and release are the user's to push (Task 6).

---

## End of SWC Digital 3.0.0 plan set

- Plan 1: `2026-07-21-v3-plan1-consolidation.md` — repo consolidation.
- Plan 2: `2026-07-21-v3-plan2-usage-firmware.md` — usage display firmware.
- Plan 3: `2026-07-21-v3-plan3-mac-service.md` — Mac Wi-Fi push service.
- Plan 4: `2026-07-21-v3-plan4-ci-docs-release.md` — this file.

Execution order: 1 → 2 → 3 → 4 (Plans 2 and 3 can be drafted in parallel but 3's physical acceptance needs 2's firmware). Each plan ends with a hand-off to the next.
