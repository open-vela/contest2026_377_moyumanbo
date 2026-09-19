#!/usr/bin/env python3
"""Build the display-only firmware using the workspace's SiFli/LVGL ports."""
import os
import hashlib
from pathlib import Path
import subprocess

TEAM = Path(__file__).resolve().parents[1]
WORKSPACE = TEAM.parent
OUT = WORKSPACE / "cmake_out/velasense_ui_demo"
link = WORKSPACE / "packages/demos/contest2026_377_velasense_demo"
target = TEAM / "app/velasense_demo"
if link.is_symlink():
    if link.resolve() != target:
        raise SystemExit(f"Unexpected app mapping: {link}")
elif link.exists():
    raise SystemExit(f"App mapping is occupied: {link}")
else:
    link.symlink_to(os.path.relpath(target, link.parent))

# Re-expand Kconfig when the checked-in demo profile changes.
profile = TEAM / "board/velasense/configs/ui_demo/defconfig"
profile_hash = hashlib.sha256(profile.read_bytes()).hexdigest()
stamp = OUT / ".ui_demo_profile_sha256"
if not stamp.exists() or stamp.read_text().strip() != profile_hash:
    (OUT / ".config").unlink(missing_ok=True)

env = os.environ.copy()
paths = [
    WORKSPACE / "prebuilts/gcc/linux-x86_64/arm-none-eabi/bin",
    WORKSPACE / "prebuilts/build-tools/linux-x86_64/bin",
    Path.home() / ".local/bin",
]
env["PATH"] = os.pathsep.join(map(str, paths)) + os.pathsep + env["PATH"]
subprocess.run([
    "cmake", "-S", str(WORKSPACE / "nuttx"), "-B", str(OUT), "-GNinja",
    "-DBOARD_CONFIG=../contest2026_377_moyumanbo/board/velasense/configs/ui_demo",
    "-DEXTRA_FLAGS=-Wno-cpp -Wno-deprecated-declarations",
], env=env, check=True)
stamp.write_text(profile_hash + "\n")
subprocess.run(["cmake", "--build", str(OUT), "--parallel", "8"], env=env, check=True)
subprocess.run([str(paths[0] / "arm-none-eabi-objcopy"), "-O", "binary",
                str(OUT / "nuttx"), str(OUT / "nuttx.bin")], check=True)
print(f"Firmware: {OUT / 'nuttx.bin'}")
