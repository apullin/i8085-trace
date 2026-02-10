# I8085 Trace

Standalone Intel 8085 CPU simulator with per-instruction trace output.

## Purpose

This tool runs 8085 machine code binaries and outputs a detailed trace of each instruction executed in NDJSON format. It's designed for testing compilers and assemblers targeting the 8085 processor.

Unlike full system emulators, this tool:
- Runs programs from any address (no ROM requirements)
- Uses flat 64K memory and simple 256-byte I/O space
- Outputs machine-readable trace data
- Supports scheduled interrupt injection for testing ISR code

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Testing

```bash
python3 test/run_tests.py
```

## Usage

```
i8085-trace [options] <binary.bin>

Memory Options:
  -l, --load=ADDR       Load address (hex, default: 0x0000)
  -e, --entry=ADDR      Entry point (hex, default: same as load)
  -p, --sp=ADDR         Initial stack pointer (hex, default: 0xFFFF)

Execution Options:
  -n, --max-steps=N     Max instructions (default: 1000000)
  -s, --stop-at=ADDR    Stop at address (hex, can repeat)
  --irq=CODE@STEP       Trigger interrupt at step (can repeat)
  --timer=CODE:PERIOD   Periodic interrupt every PERIOD T-states (can repeat)

Output Options:
  -o, --output=FILE     Output file (default: stdout)
  -q, --quiet           Only output trace, no status messages
  -S, --summary         Output only final state as JSON (no per-step trace)
  -d, --dump=START:LEN  Dump memory range at exit (hex, can repeat)
  --cov=FILE            Write coverage JSON (pc/opcode hit counts)

Tracepoint Options (require -S):
  -t, --tracepoint=ADDR       Trace only this address (hex, can repeat)
  -T, --tracepoint-file=FILE  Load tracepoint addresses from file
  --tracepoint-max=N          Stop after N total tracepoint hits
  --tracepoint-stop           Stop when all tracepoints hit at least once

Debugging:
  --gdb=PORT            Start GDB RSP server on PORT (e.g., --gdb=1234)

Other:
  -h, --help            Show help
```

Interrupt codes:
- `45` (TRAP / RST 4.5)
- `55` (RST 5.5)
- `65` (RST 6.5)
- `75` (RST 7.5)

Examples:
```bash
./i8085-trace test.bin
./i8085-trace -l 0x2000 -e 0x2000 -p 0xFF00 program.bin
./i8085-trace -n 100 -o trace.ndjson test.bin
./i8085-trace --irq=55@500 -s 0x2100 program.bin
```

GDB remote debugging (RSP) mode:
```bash
./i8085-trace --gdb=1234 program.bin
gdb -ex 'target remote localhost:1234'
```

## Output Format

Each line is a JSON object with all values in hex:

```json
{"step":0,"pc":"2000","sp":"FF00","f":"02","clk":0,"op":"LXI","asm":"LXI    H,#$1234","r":["00","00","00","00","00","12","34"]}
```

Fields:
- `step`: Instruction number (0-indexed)
- `pc`: Program counter before execution (hex)
- `sp`: Stack pointer before execution (hex)
- `f`: Flags register (hex, 8085 PSW layout)
- `clk`: T-states consumed so far (rough estimate)
- `op`: Instruction mnemonic
- `asm`: Full disassembly
- `r`: Register array in order `[A,B,C,D,E,H,L]`

## Summary Mode

The `-S, --summary` option runs silently and outputs a single JSON line with final CPU state:

```json
{"pc":"2010","sp":"FEF0","f":"82","clk":76,"steps":5,"halt":"hlt","r":["01","00","00","00","00","00","00"]}
```

`halt` reasons: `hlt`, `stop`, `loop`, `max`, `tracepoint-max`, `tracepoint-stop`.

## Tracepoints (Filtered Tracing)

Tracepoints provide selective tracing - instead of tracing every instruction, only trace when PC matches a tracepoint address. This is useful for profiling specific functions without the overhead of full instruction tracing.

Tracepoints require `-S` mode. Without `-S`, all instructions are traced anyway.

Example:
```
./i8085-trace -q -S --tracepoint 0x2000 --tracepoint 0x2100 --tracepoint-stop program.bin
```

Each tracepoint hit outputs a full trace line. The final line is the summary JSON. To measure cycles, compute `clk` deltas between entry/exit trace lines.

## Memory Dumps

The `-d, --dump=START:LEN` option dumps memory ranges at exit, useful for verifying program results without implementing I/O. Output goes to stderr in hex+ASCII format:

```
Memory dump 0x2000 - 0x201F (32 bytes):
  2000: 3E 01 32 00 20 C3 00 20 00 00 00 00 00 00 00 00  |>.2. ...........|
```

Multiple dump ranges can be specified by repeating the `-d` option.

## Coverage Output

The `--cov=FILE` option writes a JSON summary of executed PC and opcode hit
counts. This is intended for test harnesses to validate instruction coverage.

## Interrupt Simulation

The `--irq=CODE@STEP` option triggers an interrupt at a specific instruction step:
- `45` (TRAP / RST 4.5) vector 0x0024
- `55` (RST 5.5) vector 0x002C
- `65` (RST 6.5) vector 0x0034
- `75` (RST 7.5) vector 0x003C

The interrupt is recognized only if interrupts are enabled and the corresponding mask allows it. The simulator models the 8085 latch/mask behavior used by RIM/SIM.

The `--timer=CODE:PERIOD` option triggers an interrupt periodically every PERIOD
T-states (based on the internal cycle counter). This is useful for simulating
periodic hardware interrupts.

## Termination Conditions

The simulator stops when:
1. Maximum steps reached (default 1M)
2. Stop address is reached (`-s` option)
3. HLT instruction executed
4. Infinite loop detected (PC unchanged after instruction)
5. Tracepoint max hits reached (`--tracepoint-max`)
6. All tracepoints hit at least once (`--tracepoint-stop`)

## Cycle Timing (Rough Estimate)

The `clk` field is a rough cycle count based on per-opcode timings. It does not include external wait states or accurate memory access timing.

## Limitations

- I/O ports are modeled as a 256-byte array; `OUT` is a no-op hook by default.
- No memory-mapped peripherals; memory is a flat 64K RAM image.
- Cycle counts are approximate (per-opcode base timings only).

## Acknowledgements

Core 8085 emulation code is derived from sim8085 by Debjit Biswas:
https://github.com/debjitbis08/sim8085

## License

Core 8085 emulation code is derived from sim8085 (BSD 3-Clause). See `LICENSE`.
