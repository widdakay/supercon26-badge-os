"""Run: python3 tools/test_bbft.py. Requires a host C compiler, no ESP-IDF."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="badge-bbft-") as tmp:
    exe = str(Path(tmp) / "test_bbft")
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", str(root / "tools/test_bbft.c"), "-o", exe,
    ], check=True)
    actual = subprocess.check_output([exe])
    # Exact formats from widdakay/hackaday-2026-badge at 52400b3:
    # firmware/badge/lib/ranging.py, _BEACON_FMT and _RANGE_FMT.
    beacon = struct.pack("<4sBBI16s", b"BBFT", 2, 1, 0x12345678, b"0123456789abcdef")
    result = struct.pack("<4sBB6sBBbIIIIIII", b"BBFT", 2, 2, bytes(range(1, 7)),
                         0, 16, -71, 450, 30, 29, 30000, 30001, 25000, 35000)
    assert actual == beacon + result, "BBFT bytes differ from upstream Python formats"
    assert struct.unpack("<4sBB6sBBbIIIIIII", actual[len(beacon):])[6] == -71
    print("BBFT v2: wire fixtures, rejection, calibration, median, expiry and bounds passed")
