#!/usr/bin/env python3
"""Generate a tiny 8085 binary that reads hello.txt from disk and prints it."""

import sys

code = []
addr = 0

# Filename "hello.txt" stored at 0x0100
filename = b"hello.txt"
fname_addr = 0x0100

# Program starts at 0x0000
# 1. Set address register to filename location
# OUT 0xF2, lo(fname_addr)
code += [0x3E, fname_addr & 0xFF]  # MVI A, lo
code += [0xD3, 0xF2]               # OUT 0xF2

# OUT 0xF3, hi(fname_addr)
code += [0x3E, (fname_addr >> 8) & 0xFF]  # MVI A, hi
code += [0xD3, 0xF3]               # OUT 0xF3

# 2. Set data port to filename length
code += [0x3E, len(filename)]      # MVI A, len
code += [0xD3, 0xF1]               # OUT 0xF1

# 3. Issue OPEN_READ command
code += [0x3E, 0x01]               # MVI A, 0x01 (OPEN_READ)
code += [0xD3, 0xF0]               # OUT 0xF0

# 4. Check status
code += [0xDB, 0xF0]               # IN 0xF0
code += [0xFE, 0x00]               # CPI 0x00
code += [0xC2]                     # JNZ error (will patch)
error_patch = len(code)
code += [0x00, 0x00]               # placeholder

# 5. Read loop
loop_addr = len(code)
# Issue READ_BYTE command
code += [0x3E, 0x03]               # MVI A, 0x03 (READ_BYTE)
code += [0xD3, 0xF0]               # OUT 0xF0

# Check status
code += [0xDB, 0xF0]               # IN 0xF0
code += [0xFE, 0x01]               # CPI 0x01 (EOF?)
code += [0xCA]                     # JZ done (will patch)
done_patch = len(code)
code += [0x00, 0x00]               # placeholder

# Read the byte
code += [0xDB, 0xF1]               # IN 0xF1

# Output to putchar port (port 1)
code += [0xD3, 0x01]               # OUT 0x01

# Loop back
code += [0xC3, loop_addr & 0xFF, (loop_addr >> 8) & 0xFF]  # JMP loop

# Done: CLOSE and HLT
done_addr = len(code)
code += [0x3E, 0x05]               # MVI A, 0x05 (CLOSE)
code += [0xD3, 0xF0]               # OUT 0xF0
code += [0x76]                     # HLT

# Error: HLT with A=0xFF
error_addr = len(code)
code += [0x3E, 0xFF]               # MVI A, 0xFF
code += [0x76]                     # HLT

# Patch jump targets
code[error_patch] = error_addr & 0xFF
code[error_patch + 1] = (error_addr >> 8) & 0xFF
code[done_patch] = done_addr & 0xFF
code[done_patch + 1] = (done_addr >> 8) & 0xFF

# Pad to filename location and add filename
while len(code) < fname_addr:
    code.append(0x00)
code += list(filename)

with open("disk_read_test.bin", "wb") as f:
    f.write(bytes(code))

print(f"Generated disk_read_test.bin ({len(code)} bytes)")
print(f"  Filename at 0x{fname_addr:04X}: {filename.decode()}")
print(f"  Loop at 0x{loop_addr:04X}")
print(f"  Done at 0x{done_addr:04X}")
print(f"  Error at 0x{error_addr:04X}")
