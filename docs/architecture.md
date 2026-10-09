# Architecture

How the jig works, as built. For setup and operation see the [README](../README.md).

## Design principle

The STM8 programming logic follows [stm8flash](https://github.com/vdudouyt/stm8flash):
register addresses and block sizes come from its `stm8.c` device table, and the
flash sequence follows its orchestration. The code itself is a small
reimplementation, not a port: a jig flashes one part family, and a ~150-line
core is easier to own than stm8flash's CLI source tree compiled into a sketch.
stm8flash remains the reference for each new device entry.

## Layers

```
┌───────────────────────────────────────────────────────────────┐
│ TDisplayS3-STM8S-Flasher.ino                                  │
│  probe every 1 s → flash once per insertion → PASS / FAIL     │
│  screen, buttons, WiFiManager, ArduinoOTA, web server         │
├───────────────────────────────┬───────────────────────────────┤
│ stm8.cpp — device layer       │ ihx.cpp — Intel HEX parser    │
│  device table, connect, UID,  │ webpage.h — /, /status,       │
│  flash unlock / block program │  /upload, /update             │
│  / verify, run                │ FFat: /firmware.ihx           │
├───────────────────────────────┴───────────────────────────────┤
│ swim.cpp — SWIM wire driver                                   │
│  entry + sync, bit/frame layer, ROTF / WOTF / SRST            │
├───────────────────────────────────────────────────────────────┤
│ GPIO1 = SWIM (1 kΩ pull-up to 3.3 V), GPIO2 = NRST            │
└───────────────────────────────────────────────────────────────┘
```

## SWIM wire driver (`swim.cpp`)

Bit-banged on one open-drain GPIO, with direct register access
(`digitalWrite` is too slow for 250 ns pulses).

- **Entry**: hold SWIM low 1 ms, then 4 pulses at 1 kHz and 4 at 2 kHz. The
  target answers with a sync pulse of 128 SWIM clocks (about 16 µs).
- **Per-chip timing**: the sync pulse width is measured and the bit timing is
  derived from it (`set_timing`). The STM8's HSI is several percent off nominal
  on real parts, which is enough to corrupt a 22-clock bit at nominal timing.
  Widths outside 8-40 µs fail the entry.
- **Bits**: low-speed format, 22 SWIM clocks per bit. '0' is 20 clocks low +
  2 high, '1' is 2 low + 20 high. Received bits are decoded by low-pulse width.
- **Frames**: host frames are a '0' header, the payload MSB first, and even
  parity, then the target ACKs. Target frames are a header, 8 data bits and
  parity, then the host ACKs or NAKs. Each frame runs inside a FreeRTOS
  critical section so WiFi interrupts can't stretch a bit, and is retried up to
  4 times on NAK. No ACK at all means the target is gone, and the frame isn't retried.
- **Commands**: SRST (3-bit), ROTF and WOTF (3-bit command, 8-bit count, 24-bit
  address, then data). Transfers are split into chunks of at most 255 bytes.

## Device layer (`stm8.cpp`)

- **Connect** (3 attempts): assert NRST, SWIM entry, write `SWIM_CSR`
  (mask internal resets, whole memory map visible), release NRST, stall the
  CPU through `DM_CSR2`.
- **Probe**: read the 96-bit unique ID at 0x4865. Present means 103-class
  silicon, blank means 003. A heuristic only: the parts share a die and flash layout.
- **Program**: unlock flash with 0x56, 0xAE to `FLASH_PUKR` and check
  `IAPSR.PUL`. Then, per 64-byte block: set block-program in `FLASH_CR2` (and
  its complement in `FLASH_NCR2`), write the block with WOTF, and wait up to
  20 ms for `IAPSR.EOP`. EOP is cleared by reading it, so one corrupted poll can
  eat it on a block that programmed fine: on an EOP timeout, the block is read
  back and accepted if it matches.
- **Verify**: ROTF read-back in 128-byte chunks, compared byte for byte.
- **Run**: SWIM SRST. The target reboots into the new firmware.

## Orchestration (`TDisplayS3-STM8S-Flasher.ino`)

The jig powers the target, so there is no VDD sensing: a successful SWIM entry
is the target-present detection.

```
            probe every 1 s (connect + UID; target reset each time)
                │
 no target ◀────┤ answered, auto mode, image loaded, not yet flashed
 (clear flag,   ▼
  clear result) flash cycle: connect → unlock → program → verify
                │      up to 3 attempts while comm errors
                ▼
   PASS (green) │ FAIL (red, verify mismatch only) │ comm error (orange)
                ▼
     result held 1 s on the flash screen (KEY = retest),
     then on the home screen until the board is removed
                ▼
     flag set: no reflash until the board is disconnected
```

- **FAIL means only a verify mismatch.** No target, failed entry and
  read/write errors are communication problems and never show as FAIL.
- **Once per insertion**: the flag is set after any outcome, including a comm
  error, and cleared when the probe finds no target. A board left on the jig is
  never reflashed: the STM8S003 is rated for only 100 program cycles.
- **WiFi is secondary**: WiFiManager runs non-blocking and flashing never
  waits on the network.

## Storage and upload

FFat on the 9.9 MB FATFS partition holds `/firmware.ihx`. An upload goes to a
temp file and must parse cleanly (checksums, all data inside the target's
flash) before it replaces the stored image. The image is parsed once into RAM
at boot and after each upload; every flash cycle runs from RAM.

## Not built (possible additions)

Per-unit serial-number injection, option-byte programming and verify, a count
limit, per-unit logs, and authentication on the web endpoints.

## References

- [UM0470](https://www.st.com/resource/en/user_manual/um0470-stm8-swim-communication-protocol-and-debug-module-stmicroelectronics.pdf): STM8 SWIM communication protocol and debug module
- [RM0016](https://www.st.com/resource/en/reference_manual/rm0016-stm8s-series-and-stm8af-series-8bit-microcontrollers-stmicroelectronics.pdf): STM8S and STM8AF reference manual (flash programming)
- [stm8flash](https://github.com/vdudouyt/stm8flash): device table and flash sequence
