#!/usr/bin/env python3

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BIN_CANDIDATES = [
    ROOT / "build" / "i8085-trace",
    ROOT / "i8085-trace",
]


def find_binary() -> Path:
    for path in BIN_CANDIDATES:
        if path.exists() and os.access(path, os.X_OK):
            return path
    return None


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def write_bytes(path: Path, data: bytes):
    path.write_bytes(data)


def parse_summary(stdout: str):
    line = stdout.strip().splitlines()[-1]
    return json.loads(line)


def parse_dump_byte(stderr: str, addr: int, count: int):
    hex_addr = f"{addr:04X}"
    for line in stderr.splitlines():
        marker = f"{hex_addr}:"
        if marker in line:
            after = line.split(marker, 1)[1]
            if "|" in after:
                after = after.split("|", 1)[0]
            parts = [p for p in after.strip().split() if re.fullmatch(r"[0-9A-Fa-f]{2}", p)]
            if len(parts) >= count:
                return [int(p, 16) for p in parts[:count]]
    return None


def test_rim_bits(bin_path: Path):
    # Program: RIM; HLT
    program = bytes([0x20, 0x76])
    with tempfile.TemporaryDirectory() as tmpdir:
        prog_path = Path(tmpdir) / "rim.bin"
        write_bytes(prog_path, program)
        cmd = [
            str(bin_path),
            "-S",
            "--irq=55@0",
            "--irq=65@0",
            "--irq=75@0",
            str(prog_path),
        ]
        res = run(cmd)
        if res.returncode != 0:
            raise AssertionError(f"rim test failed: {res.stderr}")
        summary = parse_summary(res.stdout)
        a = summary["r"][0]
        if a != "70":
            raise AssertionError(f"rim test: expected A=70, got {a}")


def test_ei_delay(bin_path: Path):
    # Program:
    #   LXI SP,0x4000
    #   EI
    #   MVI A,0x42
    #   HLT
    # Interrupt vector 0x002C contains RET.
    program = bytearray([0x31, 0x00, 0x40, 0xFB, 0x3E, 0x42, 0x76])
    size = 0x2D
    if len(program) < size:
        program.extend([0x00] * (size - len(program)))
    program[0x2C] = 0xC9  # RET

    with tempfile.TemporaryDirectory() as tmpdir:
        prog_path = Path(tmpdir) / "ei.bin"
        write_bytes(prog_path, bytes(program))
        cmd = [
            str(bin_path),
            "-S",
            "--irq=55@1",
            "--dump=0x3FFE:2",
            str(prog_path),
        ]
        res = run(cmd)
        if res.returncode != 0:
            raise AssertionError(f"ei test failed: {res.stderr}")
        dump = parse_dump_byte(res.stderr, 0x3FFE, 2)
        if dump != [0x06, 0x00]:
            raise AssertionError(f"ei delay test: expected [06 00], got {dump}")


def main():
    bin_path = find_binary()
    if not bin_path:
        print("Error: i8085-trace binary not found. Build with cmake first.", file=sys.stderr)
        return 1

    test_rim_bits(bin_path)
    test_ei_delay(bin_path)
    print("All tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
