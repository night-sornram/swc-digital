#!/usr/bin/env python3
"""PlatformIO pre-build: inject FW_VERSION from `git describe`.

Runs at build start. Derives the firmware version string from the most recent
reachable git tag and adds it to build flags as -DFW_VERSION='"x.y.z"'.
Removes the need to hand-edit config.h every release — just
`git tag v3.0.3 && pio run` and the binary reports the right version.

Resolution order (first non-empty wins):
  1. FW_VERSION env var (CI overrides, manual testing).
  2. `git describe --tags --abbrev=0` (latest reachable tag), leading 'v' stripped.
  3. Fallback "0.0.0-dev" so a fresh checkout with no tags still builds.

If config.h still defines FW_VERSION, the compiler will error out because the
macro gets defined twice — having both is exactly the bug this script exists
to prevent.
"""
from __future__ import annotations

import os
import subprocess
import sys

FALLBACK = "0.0.0-dev"

# Resolve version string.
def _resolve() -> str:
    env_override = os.environ.get("FW_VERSION", "").strip()
    if env_override:
        return env_override
    # Repo root via PROJECT_DIR if PlatformIO set it, else cwd. (SCons exec
    # of this script doesn't define __file__, so don't rely on it.)
    repo_dir = os.environ.get("PLATFORMIO_CORE_DIR") or os.getcwd()
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"],
            stderr=subprocess.DEVNULL,
            cwd=repo_dir,
            text=True,
        ).strip()
        if out.startswith("v"):
            out = out[1:]
        return out or FALLBACK
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return FALLBACK

version = _resolve()

# Validate: digits and dots only so a stray quote/semicolon can never escape
# into the C string literal.
if not version or not all(c.isdigit() or c == "." for c in version):
    print(f"gen_version: refusing version {version!r} (must be digits and dots)",
          file=sys.stderr)
    sys.exit(1)

# PlatformIO pre-script: SCons injects `env` via the SConscript export chain.
# SCons's Import() injects into the caller's locals (it does NOT return the
# value), so the call must stand alone on its own line.
Import("env")  # noqa: F821  -- injects `env` into local scope

# -D with a C string literal value. The inner escaped double-quotes survive
# the PlatformIO flag parser and produce: -DFW_VERSION="3.0.2"
env.Append(BUILD_FLAGS=[f'-DFW_VERSION=\\"{version}\\"'])
print(f"gen_version: FW_VERSION={version}")
