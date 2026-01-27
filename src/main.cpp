//----------------------------------------------------------------------------
// I8085 Trace - Standalone Intel 8085 CPU Simulator
//
// main.cpp - CLI entry point and trace loop
//----------------------------------------------------------------------------

#include "i8085_cpu.h"
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Scheduled interrupt
//----------------------------------------------------------------------------

struct ScheduledIRQ {
    int code;
    UINT64 atStep;
};

//----------------------------------------------------------------------------
// Configuration
//----------------------------------------------------------------------------

struct MemoryDump {
    UINT16 start;
    UINT16 length;
};

struct Tracepoint {
    UINT16 pc;
    UINT32 hits = 0;
};

struct Config {
    const char *inputFile = nullptr;
    const char *outputFile = nullptr;
    UINT16 loadAddr = 0x0000;
    UINT16 entryAddr = 0x0000;
    UINT16 spAddr = 0xFFFF;
    UINT64 maxSteps = 1000000;
    std::vector<UINT16> stopAddrs;
    std::vector<ScheduledIRQ> irqs;
    std::vector<MemoryDump> dumps;
    std::vector<Tracepoint> tracepoints;
    UINT64 tracepointMax = 0;
    bool tracepointStop = false;
    bool quiet = false;
    bool summary = false;
    bool entrySet = false;
};

//----------------------------------------------------------------------------
// Usage
//----------------------------------------------------------------------------

static void PrintUsage(const char *prog) {
    fprintf(stderr, "I8085 Trace - Standalone Intel 8085 CPU Simulator\n\n");
    fprintf(stderr, "Usage: %s [options] <binary.bin>\n\n", prog);
    fprintf(stderr, "Memory Options:\n");
    fprintf(stderr, "  -l, --load=ADDR       Load address (hex, default: 0x0000)\n");
    fprintf(stderr, "  -e, --entry=ADDR      Entry point (hex, default: same as load)\n");
    fprintf(stderr, "  -p, --sp=ADDR         Initial stack pointer (hex, default: 0xFFFF)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Execution Options:\n");
    fprintf(stderr, "  -n, --max-steps=N     Max instructions (default: 1000000)\n");
    fprintf(stderr, "  -s, --stop-at=ADDR    Stop at address (hex, can repeat)\n");
    fprintf(stderr, "  --irq=CODE@STEP       Trigger interrupt at step (can repeat)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Output Options:\n");
    fprintf(stderr, "  -o, --output=FILE     Output file (default: stdout)\n");
    fprintf(stderr, "  -q, --quiet           Only output trace, no status messages\n");
    fprintf(stderr, "  -S, --summary         Output only final state as JSON (no per-step trace)\n");
    fprintf(stderr, "  -d, --dump=START:LEN  Dump memory range at exit (hex, can repeat)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Tracepoint Options (require -S):\n");
    fprintf(stderr, "  -t, --tracepoint=ADDR       Trace only this address (hex, can repeat)\n");
    fprintf(stderr, "  -T, --tracepoint-file=FILE  Load tracepoint addresses from file\n");
    fprintf(stderr, "  --tracepoint-max=N          Stop after N total tracepoint hits\n");
    fprintf(stderr, "  --tracepoint-stop           Stop when all tracepoints hit at least once\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Other:\n");
    fprintf(stderr, "  -h, --help            Show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Interrupt codes: 45 (TRAP/RST 4.5), 55 (RST 5.5), 65 (RST 6.5), 75 (RST 7.5)\n");
}

//----------------------------------------------------------------------------
// Parse hex value (requires 0x prefix)
//----------------------------------------------------------------------------

static bool ParseHex(const char *str, UINT16 *value) {
    if (str[0] != '0' || (str[1] != 'x' && str[1] != 'X')) {
        return false;
    }
    char *end;
    unsigned long v = strtoul(str, &end, 16);
    if (*end != '\0' || v > 0xFFFF) {
        return false;
    }
    *value = (UINT16)v;
    return true;
}

//----------------------------------------------------------------------------
// Parse interrupt spec: CODE@STEP (e.g., "55@500")
//----------------------------------------------------------------------------

static bool ParseIRQ(const char *str, ScheduledIRQ *irq) {
    const char *at = strchr(str, '@');
    if (!at) {
        return false;
    }

    std::string codeStr(str, at - str);
    int code = 0;

    char *end = nullptr;
    long numeric = strtol(codeStr.c_str(), &end, 10);
    if (end && *end == '\0') {
        if (numeric == 45 || numeric == 55 || numeric == 65 || numeric == 75) {
            code = (int)numeric;
        } else {
            return false;
        }
    } else {
        for (auto &c : codeStr)
            c = (char)tolower(c);
        if (codeStr == "trap" || codeStr == "rst4.5" || codeStr == "4.5")
            code = 45;
        else if (codeStr == "rst5.5" || codeStr == "5.5" || codeStr == "r5.5")
            code = 55;
        else if (codeStr == "rst6.5" || codeStr == "6.5" || codeStr == "r6.5")
            code = 65;
        else if (codeStr == "rst7.5" || codeStr == "7.5" || codeStr == "r7.5")
            code = 75;
        else
            return false;
    }

    UINT64 step = strtoull(at + 1, &end, 10);
    if (*end != '\0') {
        return false;
    }

    irq->code = code;
    irq->atStep = step;
    return true;
}

//----------------------------------------------------------------------------
// Add tracepoint (with deduplication)
//----------------------------------------------------------------------------

static void AddTracepoint(std::vector<Tracepoint> &tracepoints, UINT16 addr) {
    for (const auto &tp : tracepoints) {
        if (tp.pc == addr)
            return;
    }
    Tracepoint tp;
    tp.pc = addr;
    tracepoints.push_back(tp);
}

//----------------------------------------------------------------------------
// Parse tracepoint file (one hex address per line, # comments, blank lines ok)
//----------------------------------------------------------------------------

static bool ParseTracepointFile(const char *path, std::vector<Tracepoint> &tracepoints) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '#' || *p == '\0' || *p == '\n')
            continue;

        char *end;
        unsigned long val;
        if (strncmp(p, "0x", 2) == 0 || strncmp(p, "0X", 2) == 0)
            val = strtoul(p + 2, &end, 16);
        else
            val = strtoul(p, &end, 16);

        if (val > 0xFFFF) {
            fclose(f);
            return false;
        }
        AddTracepoint(tracepoints, (UINT16)val);
    }
    fclose(f);
    return true;
}

//----------------------------------------------------------------------------
// Parse memory dump spec: START:LENGTH (e.g., "0x8300:32")
//----------------------------------------------------------------------------

static bool ParseDump(const char *str, MemoryDump *dump) {
    char *colon = (char *)strchr(str, ':');
    if (!colon) {
        return false;
    }

    *colon = '\0';
    UINT16 start;
    bool ok = ParseHex(str, &start);
    *colon = ':';

    if (!ok) {
        return false;
    }

    char *end;
    unsigned long length;
    if (colon[1] == '0' && (colon[2] == 'x' || colon[2] == 'X'))
        length = strtoul(colon + 1, &end, 16);
    else
        length = strtoul(colon + 1, &end, 10);

    if (*end != '\0' || length == 0 || length > 0x10000) {
        return false;
    }

    dump->start = start;
    dump->length = (UINT16)length;
    return true;
}

//----------------------------------------------------------------------------
// Load binary file
//----------------------------------------------------------------------------

static bool LoadBinary(State8085 *state, const char *filename, UINT16 loadAddr) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "Error: Failed to get file size\n");
        fclose(f);
        return false;
    }

    if (loadAddr + size > 0x10000) {
        fprintf(stderr, "Error: Program too large (0x%04X + %ld bytes)\n", loadAddr, size);
        fclose(f);
        return false;
    }

    size_t read = fread(&state->memory[loadAddr], 1, size, f);
    fclose(f);

    if ((long)read != size) {
        fprintf(stderr, "Error: Short read (%zu of %ld bytes)\n", read, size);
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------
// Trace output state
//----------------------------------------------------------------------------

struct TraceState {
    UINT64 step;
    UINT16 pc;
    UINT16 sp;
    UINT8 flags;
    UINT64 clocks;
    const char *mnemonic;
    const char *disasm;
};

static UINT8 PackFlags(const Flags &cc) {
    UINT8 f = 0;
    if (cc.s)
        f |= 0x80;
    if (cc.z)
        f |= 0x40;
    if (cc.ac)
        f |= 0x10;
    if (cc.p)
        f |= 0x04;
    f |= 0x02; // bit 1 is always 1
    if (cc.cy)
        f |= 0x01;
    return f;
}

static void ExtractMnemonic(const char *disasm, char *out, size_t outLen) {
    if (!out || outLen == 0)
        return;
    const char *p = disasm;
    while (*p && isspace((unsigned char)*p))
        p++;
    const char *space = strpbrk(p, " \t");
    size_t len = space ? (size_t)(space - p) : strlen(p);
    if (len >= outLen)
        len = outLen - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static void OutputTrace(FILE *out, const TraceState &t, const State8085 *state) {
    fprintf(out, "{\"step\":%" PRIu64 ",\"pc\":\"%04X\",\"sp\":\"%04X\",\"f\":\"%02X\",\"clk\":%" PRIu64 ",", t.step,
            t.pc, t.sp, t.flags, t.clocks);
    fprintf(out, "\"op\":\"%s\",\"asm\":\"%s\",\"r\":[", t.mnemonic, t.disasm);

    fprintf(out, "\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\"", state->a, state->b, state->c,
            state->d, state->e, state->h, state->l);
    fprintf(out, "]}\n");
}

static bool IsStopAddress(UINT16 pc, const std::vector<UINT16> &stopAddrs) {
    return std::find(stopAddrs.begin(), stopAddrs.end(), pc) != stopAddrs.end();
}

//----------------------------------------------------------------------------
// I/O hook (stub)
//----------------------------------------------------------------------------

extern "C" void io_write(int address, int value) {
    (void)address;
    (void)value;
}

//----------------------------------------------------------------------------
// Main
//----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    Config cfg;

    static struct option longOpts[] = {{"load", required_argument, nullptr, 'l'},
                                       {"entry", required_argument, nullptr, 'e'},
                                       {"sp", required_argument, nullptr, 'p'},
                                       {"max-steps", required_argument, nullptr, 'n'},
                                       {"stop-at", required_argument, nullptr, 's'},
                                       {"irq", required_argument, nullptr, 'i'},
                                       {"output", required_argument, nullptr, 'o'},
                                       {"dump", required_argument, nullptr, 'd'},
                                       {"tracepoint", required_argument, nullptr, 't'},
                                       {"tracepoint-file", required_argument, nullptr, 'T'},
                                       {"tracepoint-max", required_argument, nullptr, 'M'},
                                       {"tracepoint-stop", no_argument, nullptr, 'P'},
                                       {"quiet", no_argument, nullptr, 'q'},
                                       {"summary", no_argument, nullptr, 'S'},
                                       {"help", no_argument, nullptr, 'h'},
                                       {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "l:e:p:n:s:i:o:d:t:T:M:PqSh", longOpts, nullptr)) != -1) {
        switch (opt) {
        case 'l':
            if (!ParseHex(optarg, &cfg.loadAddr)) {
                fprintf(stderr, "Error: Invalid load address '%s'\n", optarg);
                return 1;
            }
            break;
        case 'e':
            if (!ParseHex(optarg, &cfg.entryAddr)) {
                fprintf(stderr, "Error: Invalid entry address '%s'\n", optarg);
                return 1;
            }
            cfg.entrySet = true;
            break;
        case 'p':
            if (!ParseHex(optarg, &cfg.spAddr)) {
                fprintf(stderr, "Error: Invalid SP address '%s'\n", optarg);
                return 1;
            }
            break;
        case 'n':
            cfg.maxSteps = strtoull(optarg, nullptr, 10);
            break;
        case 's': {
            UINT16 addr;
            if (!ParseHex(optarg, &addr)) {
                fprintf(stderr, "Error: Invalid stop address '%s'\n", optarg);
                return 1;
            }
            cfg.stopAddrs.push_back(addr);
            break;
        }
        case 'i': {
            ScheduledIRQ irq;
            if (!ParseIRQ(optarg, &irq)) {
                fprintf(stderr, "Error: Invalid IRQ spec '%s'\n", optarg);
                return 1;
            }
            cfg.irqs.push_back(irq);
            break;
        }
        case 'o':
            cfg.outputFile = optarg;
            break;
        case 'd': {
            MemoryDump dump;
            if (!ParseDump(optarg, &dump)) {
                fprintf(stderr, "Error: Invalid dump spec '%s' (use START:LENGTH, e.g., 0x2000:32)\n", optarg);
                return 1;
            }
            cfg.dumps.push_back(dump);
            break;
        }
        case 't': {
            UINT16 addr;
            if (!ParseHex(optarg, &addr)) {
                fprintf(stderr, "Error: Invalid tracepoint address '%s'\n", optarg);
                return 1;
            }
            AddTracepoint(cfg.tracepoints, addr);
            break;
        }
        case 'T':
            if (!ParseTracepointFile(optarg, cfg.tracepoints)) {
                fprintf(stderr, "Error: Failed to read tracepoint file '%s'\n", optarg);
                return 1;
            }
            break;
        case 'M':
            cfg.tracepointMax = strtoull(optarg, nullptr, 10);
            break;
        case 'P':
            cfg.tracepointStop = true;
            break;
        case 'q':
            cfg.quiet = true;
            break;
        case 'S':
            cfg.summary = true;
            break;
        case 'h':
            PrintUsage(argv[0]);
            return 0;
        default:
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n\n");
        PrintUsage(argv[0]);
        return 1;
    }
    cfg.inputFile = argv[optind];

    if (!cfg.entrySet) {
        cfg.entryAddr = cfg.loadAddr;
    }

    std::sort(cfg.irqs.begin(), cfg.irqs.end(),
              [](const ScheduledIRQ &a, const ScheduledIRQ &b) { return a.atStep < b.atStep; });

    State8085 *state = Init8085();
    if (!state) {
        fprintf(stderr, "Error: Failed to allocate CPU state\n");
        return 1;
    }

    memset(state->memory, 0, 0x10000);
    memset(state->io, 0, 0x100);

    if (!LoadBinary(state, cfg.inputFile, cfg.loadAddr)) {
        Free8085(state);
        return 1;
    }

    Reset8085(state, cfg.entryAddr, cfg.spAddr);

    FILE *out = stdout;
    if (cfg.outputFile) {
        out = fopen(cfg.outputFile, "w");
        if (!out) {
            fprintf(stderr, "Error: Cannot open output file '%s'\n", cfg.outputFile);
            Free8085(state);
            return 1;
        }
    }

    if (!cfg.quiet && !cfg.summary) {
        fprintf(stderr, "I8085 Trace\n");
        fprintf(stderr, "  Input:  %s\n", cfg.inputFile);
        fprintf(stderr, "  Load:   0x%04X\n", cfg.loadAddr);
        fprintf(stderr, "  Entry:  0x%04X\n", cfg.entryAddr);
        fprintf(stderr, "  SP:     0x%04X\n", cfg.spAddr);
        fprintf(stderr, "  Max:    %" PRIu64 " steps\n", cfg.maxSteps);
        if (!cfg.stopAddrs.empty()) {
            fprintf(stderr, "  Stop:");
            for (UINT16 addr : cfg.stopAddrs)
                fprintf(stderr, " 0x%04X", addr);
            fprintf(stderr, "\n");
        }
        if (!cfg.irqs.empty()) {
            fprintf(stderr, "  IRQs:");
            for (const auto &irq : cfg.irqs)
                fprintf(stderr, " %d@%" PRIu64, irq.code, irq.atStep);
            fprintf(stderr, "\n");
        }
        if (!cfg.tracepoints.empty()) {
            fprintf(stderr, "  Tracepoints:");
            for (const auto &tp : cfg.tracepoints)
                fprintf(stderr, " 0x%04X", tp.pc);
            fprintf(stderr, "\n");
            if (cfg.tracepointMax > 0)
                fprintf(stderr, "  Tracepoint max: %" PRIu64 "\n", cfg.tracepointMax);
            if (cfg.tracepointStop)
                fprintf(stderr, "  Tracepoint stop: enabled\n");
        }
        fprintf(stderr, "\n");
    }

    char disasmBuf[80];
    char mnemonicBuf[16];
    UINT64 step = 0;
    UINT16 lastPC = 0xFFFF;
    size_t nextIRQ = 0;
    bool halted = false;
    const char *haltReason = "max";
    UINT64 totalTracepointHits = 0;
    ExecutionStats8085 stats = {0};

    while (step < cfg.maxSteps && !halted) {
        while (nextIRQ < cfg.irqs.size() && cfg.irqs[nextIRQ].atStep <= step) {
            triggerInterrupt(state, cfg.irqs[nextIRQ].code, 1);
            if (!cfg.quiet && !cfg.summary) {
                fprintf(stderr, "[Step %" PRIu64 "] Triggered IRQ %d\n", step, cfg.irqs[nextIRQ].code);
            }
            nextIRQ++;
        }

        UINT16 pc = state->pc;
        UINT16 sp = state->sp;
        UINT8 flags = PackFlags(state->cc);
        UINT64 clocks = stats.total_tstates;

        Disassemble8085Op(state->memory, pc, disasmBuf, sizeof(disasmBuf));
        ExtractMnemonic(disasmBuf, mnemonicBuf, sizeof(mnemonicBuf));

        for (auto &tp : cfg.tracepoints) {
            if (pc == tp.pc) {
                tp.hits++;
                totalTracepointHits++;
                if (cfg.summary) {
                    TraceState trace = {step, pc, sp, flags, clocks, mnemonicBuf, disasmBuf};
                    OutputTrace(out, trace, state);
                }
                break;
            }
        }

        if (cfg.tracepointMax > 0 && totalTracepointHits >= cfg.tracepointMax) {
            if (!cfg.quiet)
                fprintf(stderr, "Tracepoint max hit (%" PRIu64 " total hits)\n", totalTracepointHits);
            haltReason = "tracepoint-max";
            break;
        }

        if (cfg.tracepointStop && !cfg.tracepoints.empty()) {
            bool allHit = true;
            for (const auto &tp : cfg.tracepoints) {
                if (tp.hits == 0) {
                    allHit = false;
                    break;
                }
            }
            if (allHit) {
                if (!cfg.quiet)
                    fprintf(stderr, "All tracepoints hit at least once\n");
                haltReason = "tracepoint-stop";
                break;
            }
        }

        if (!cfg.stopAddrs.empty() && IsStopAddress(pc, cfg.stopAddrs)) {
            if (!cfg.quiet && !cfg.summary)
                fprintf(stderr, "Stopped at address 0x%04X\n", pc);
            haltReason = "stop";
            break;
        }

        if (pc == lastPC) {
            if (!cfg.quiet && !cfg.summary)
                fprintf(stderr, "Infinite loop detected at 0x%04X\n", pc);
            haltReason = "loop";
            break;
        }
        lastPC = pc;

        halted = Emulate8085Op(state, &stats);
        if (halted) {
            haltReason = "hlt";
        }

        if (!cfg.summary) {
            TraceState trace = {step, pc, sp, flags, clocks, mnemonicBuf, disasmBuf};
            OutputTrace(out, trace, state);
        }

        step++;
    }

    if (!cfg.quiet && !cfg.summary) {
        fprintf(stderr, "\nExecution complete:\n");
        fprintf(stderr, "  Instructions: %" PRIu64 "\n", step);
        fprintf(stderr, "  Clocks:       %" PRIu64 " (rough estimate)\n", stats.total_tstates);
        fprintf(stderr, "  Final PC:     0x%04X\n", state->pc);
        fprintf(stderr, "  Final SP:     0x%04X\n", state->sp);
        if (halted)
            fprintf(stderr, "  Status:       HALTED (HLT instruction)\n");
        else if (step >= cfg.maxSteps)
            fprintf(stderr, "  Status:       MAX STEPS REACHED\n");
    }

    if (cfg.summary) {
        fprintf(out,
                "{\"pc\":\"%04X\",\"sp\":\"%04X\",\"f\":\"%02X\",\"clk\":%" PRIu64 ",\"steps\":%" PRIu64
                ",\"halt\":\"%s\",\"r\":[",
                state->pc, state->sp, PackFlags(state->cc), stats.total_tstates, step, haltReason);
        fprintf(out, "\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\",\"%02X\"", state->a, state->b, state->c,
                state->d, state->e, state->h, state->l);
        fprintf(out, "]}\n");
    }

    for (const auto &dump : cfg.dumps) {
        fprintf(stderr, "\nMemory dump 0x%04X - 0x%04X (%u bytes):\n", dump.start, dump.start + dump.length - 1,
                dump.length);
        for (UINT16 offset = 0; offset < dump.length; offset += 16) {
            fprintf(stderr, "  %04X:", dump.start + offset);
            for (UINT16 i = 0; i < 16 && offset + i < dump.length; i++) {
                UINT16 addr = dump.start + offset + i;
                fprintf(stderr, " %02X", state->memory[addr]);
            }
            fprintf(stderr, "  |");
            for (UINT16 i = 0; i < 16 && offset + i < dump.length; i++) {
                UINT8 byte = state->memory[dump.start + offset + i];
                fprintf(stderr, "%c", (byte >= 32 && byte < 127) ? byte : '.');
            }
            fprintf(stderr, "|\n");
        }
    }

    if (cfg.outputFile)
        fclose(out);

    Free8085(state);
    return 0;
}
