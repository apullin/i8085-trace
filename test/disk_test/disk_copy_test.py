#!/usr/bin/env python3
"""Generate an 8085 binary that copies hello.txt to copy.txt via disk I/O."""

code = []

src_name = b"hello.txt"
dst_name = b"copy.txt"
src_addr = 0x0100
dst_addr = 0x0110

def mvi_a(val):
    return [0x3E, val & 0xFF]

def out_port(port):
    return [0xD3, port]

def in_port(port):
    return [0xDB, port]

# --- Open source for reading ---
code += mvi_a(src_addr & 0xFF) + out_port(0xF2)
code += mvi_a((src_addr >> 8) & 0xFF) + out_port(0xF3)
code += mvi_a(len(src_name)) + out_port(0xF1)
code += mvi_a(0x01) + out_port(0xF0)  # OPEN_READ

# Check status
code += in_port(0xF0)
code += [0xFE, 0x00]  # CPI 0
jnz_error1 = len(code)
code += [0xC2, 0x00, 0x00]  # JNZ error (patch later)

# --- Open dest for writing ---
code += mvi_a(dst_addr & 0xFF) + out_port(0xF2)
code += mvi_a((dst_addr >> 8) & 0xFF) + out_port(0xF3)
code += mvi_a(len(dst_name)) + out_port(0xF1)
code += mvi_a(0x02) + out_port(0xF0)  # OPEN_WRITE

# Check status
code += in_port(0xF0)
code += [0xFE, 0x00]  # CPI 0
jnz_error2 = len(code)
code += [0xC2, 0x00, 0x00]  # JNZ error (patch later)

# --- Copy loop ---
loop_addr = len(code)

# READ_BYTE
code += mvi_a(0x03) + out_port(0xF0)
code += in_port(0xF0)  # status
code += [0xFE, 0x01]   # CPI 1 (EOF?)
jz_done = len(code)
code += [0xCA, 0x00, 0x00]  # JZ done (patch later)

# Read data byte
code += in_port(0xF1)

# Write it: OUT data port, then WRITE_BYTE command
code += out_port(0xF1)
code += mvi_a(0x04) + out_port(0xF0)  # WRITE_BYTE

# Loop
code += [0xC3, loop_addr & 0xFF, (loop_addr >> 8) & 0xFF]

# --- Done ---
done_addr = len(code)
code += mvi_a(0x05) + out_port(0xF0)  # CLOSE
code += [0x76]  # HLT

# --- Error ---
error_addr = len(code)
code += mvi_a(0xFF)
code += [0x76]  # HLT

# Patch jumps
code[jnz_error1 + 1] = error_addr & 0xFF
code[jnz_error1 + 2] = (error_addr >> 8) & 0xFF
code[jnz_error2 + 1] = error_addr & 0xFF
code[jnz_error2 + 2] = (error_addr >> 8) & 0xFF
code[jz_done + 1] = done_addr & 0xFF
code[jz_done + 2] = (done_addr >> 8) & 0xFF

# Pad to data area
while len(code) < src_addr:
    code.append(0x00)
code += list(src_name)
while len(code) < dst_addr:
    code.append(0x00)
code += list(dst_name)

with open("disk_copy_test.bin", "wb") as f:
    f.write(bytes(code))

print(f"Generated disk_copy_test.bin ({len(code)} bytes)")
