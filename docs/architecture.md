# ESP32 Standalone STM8 SWIM Flasher — Architecture

A production-jig flasher: a LilyGO T-Display S3 (ESP32-S3) that programs STM8 targets over SWIM with no PC in the loop. Firmware images arrive over Wi-Fi, are stored locally, and are flashed on demand (button press) with pass/fail on the board's display.

**Status:** scaffolded and compiling. Wire driver, device layer, IHX parser, flash-cycle state machine, FFat image storage, and HTTP upload (`make image IHX=path`, or the form at `http://STM8Flasher.local/`) all exist. SWIM timing constants are structured but not yet hardware-verified against UM0470.

## Design principle

Don't invent the STM8 programming logic — **copy it from `stm8flash`**. Device register addresses and block sizes come from its `stm8.c` device table; the flash sequence follows its orchestration. But the code itself is a minimal reimplementation, not a wholesale port: this is an Arduino sketch (matching the tDisplayS3Base project style — arduino-cli, WiFiManager, ArduinoOTA, TFT_eSPI on a T-Display S3), a jig flashes one part family, and a ~150-line core is easier to own than stm8flash's CLI-shaped source tree compiled into a sketch. stm8flash remains the reference to crib each new device entry from.

The ESP32 stops being a dumb pipe (esp-stlink's role) and becomes the brain.

## Layer diagram

```
┌─────────────────────────────────────────────────────┐
│  Orchestration (flashing state machine)             │
│  idle → detect → entry → unlock → program →         │
│  verify → pass/fail                                 │
├──────────────────────────┬──────────────────────────┤
│  stm8flash core (ported) │  Management plane        │
│  - device table (stm8.c) │  - HTTP upload endpoint  │
│  - IHX/S19 parser        │  - manifest validation   │
│  - program/verify logic  │  - rollback image        │
├──────────────────────────┼──────────────────────────┤
│  esp32swim.c backend     │  Storage (LittleFS)      │
│  (stm8flash programmer   │  - firmware.ihx          │
│   shim → local calls)    │  - manifest.json         │
├──────────────────────────┤  - serial counter        │
│  SWIM wire driver        │  - unit logs             │
│  (ported esp-stlink      ├──────────────────────────┤
│   swim.c: entry, sync,   │  Wi-Fi / lwIP            │
│   ROTF, WOTF, bit-bang)  │  (core 0, idle during    │
│  core 1, critical secs   │   flash cycles)          │
├──────────────────────────┴──────────────────────────┤
│  Hardware: level shifter (BSS138), 1k pull-up on    │
│  SWIM, NRST, T-Display S3 screen + buttons          │
└─────────────────────────────────────────────────────┘
```

## Components

### SWIM wire driver (`swim/`)
Bit-banged SWIM protocol ported from esp-stlink: entry sequence (16 kHz + 8 kHz bursts), sync pulse, pulse-width-encoded bits, byte framing with parity + ACK, ROTF/WOTF/SRST commands, low→high speed switch.

Timing strategy (in escalation order — start at 1, move down only if yield rate demands it):
1. Pin the flash task to **core 1**; Wi-Fi/lwIP stays on core 0.
2. No Wi-Fi TX during a flash cycle (phases are naturally separate).
3. Each byte transfer inside `portENTER_CRITICAL` — windows are short.
4. Fallback: RMT peripheral for deterministic TX + width-measured RX.

### STM8 device layer (`stm8.h/.cpp`, `ihx.h/.cpp`)
A `stm8_device_t` table (values copied from stm8flash's `stm8.c`), the flash unlock/program/verify sequence, and an Intel HEX parser.

What it does on the wire per flash cycle: SWIM entry + sync → halt core via DM → unlock flash (`0x56,0xAE` to FLASH_PUKR; `0xAE,0x56` to FLASH_DUKR for EEPROM/option bytes) → set block-program mode in FLASH_CR2 (+ complement to FLASH_NCR2 on STM8S) → WOTF block-aligned writes → poll FLASH_IAPSR → ROTF read-back verify → reset and run. Register addresses are family-specific — that's exactly why the device table is reused, not rewritten. Authoritative refs: UM0470 (SWIM), RM0016 (STM8S flash).

### Orchestration (`esp32flasher.ino`)
The jig powers the target, so there is no VDD sensing — a periodic SWIM entry attempt (every 250 ms) doubles as target detection. Per-cycle state machine:

```
WAITING ──sync pulse received──▶ CONNECT (SWIM_CSR, stall core)
  ▲  ▲                              │
  │  │ comm error: retry quietly ◀──┼── UNLOCK → PROGRAM → VERIFY
  │  │                                              │        │
  │ button press                    PASS 2 s ◀── match   mismatch ──▶ FAIL 10 s
  │  │                                │                               │
  └──┴─────── SWAP TARGET ◀───────────┴───────────────────────────────┘
```

FAIL means exactly one thing: the read-back didn't match the image. Everything else (no target, failed entry, write/read comm errors) retries quietly — it's a technician who hasn't hooked up yet, not a bad part. After a verdict, the button re-arms for the next board. Pass/fail counters and status live on the board's display.

Deferred manufacturing features: per-unit serial injection, option-byte programming/verify, count limit, per-unit logs.

### Storage (FFat) + upload
The 16MB flash's FATFS partition holds `/firmware.ihx`. Upload paths (all validated — the ihx must parse cleanly before it replaces the stored image):

- `make image IHX=path/to/app.ihx`
- `curl -F ihx=@app.ihx http://STM8Flasher.local/upload`
- browser form at `http://STM8Flasher.local/`

On boot (and after each upload) the ihx is parsed once into a RAM buffer; each cycle flashes from the buffer. WiFi is strictly secondary: WiFiManager runs non-blocking and flashing never waits on the network.

## Hardware notes

- **Level shifting is mandatory**: ESP32 GPIOs are not 5 V tolerant; many STM8 boards run at 5 V. BSS138 open-drain bidirectional shifter on SWIM and NRST — fits SWIM's open-drain nature and doubles as the pull-up path.
- **1 kΩ pull-up** on SWIM (internal pull-up too weak for the rise time).
- The jig powers the target. Optical isolation if the line environment is electrically noisy.

## References

- esp-stlink — SWIM wire layer to port
- stm8flash — device layer to retarget
- dimitarm1/SWIM_Programmer — closest existing "MCU does the whole job" example (STM32F103)
- ST UM0470 — SWIM protocol + debug module (authoritative timing/counts)
- ST RM0016 — STM8S/STM8AF flash programming procedure
