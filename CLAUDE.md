# TDisplayS3-STM8S-Flasher

Standalone production-jig flasher: a LilyGO T-Display S3 (ESP32-S3) programs
STM8S103/003 targets over SWIM, no PC in the loop. Arduino sketch built with
arduino-cli (esp32 core 3.3.0), TFT_eSPI (Setup206, 170x320), WiFiManager,
ArduinoOTA. See docs/architecture.md for the full design.

## Build / deploy

- `make bin` — compile (FQBN pins 16MB flash, app3M_fat9M_16MB partition, OPI PSRAM)
- `make ota` — deploy to the running device at STM8Flasher.local (mDNS)
- `make usb` — deploy over /dev/ttyACM0
- `make image IHX=path/to/app.ihx` — push a target firmware image to the flasher

Serial debug: 115200 on /dev/ttyACM0. The jig hosts http://STM8Flasher.local/
(status + ihx upload form); images persist in FFat as /firmware.ihx.

## Layout

- `TDisplayS3-STM8S-Flasher.ino` — UI (2 pages: home, flash), state machine,
  wifi/OTA/mDNS/webserver, FFat image storage. Manual/auto mode: MODE_BUTTON
  (GPIO0) toggles, FLASH_BUTTON (GPIO14) tests; buttons act on debounced
  release (a press must not spill into the next page and double-fire).
- `swim.cpp/h` — bit-banged SWIM wire driver (entry, sync, pulse-width bits,
  parity/ACK frames, ROTF/WOTF/SRST). Cycle-counted timing in critical
  sections, direct GPIO register access.
- `stm8.cpp/h` — device table, flash unlock/block-program/verify, UID probe.
- `ihx.cpp/h` — Intel HEX parser (host-testable: see test_ihx.c.txt header).
- Build id: scanHash() finds the "GITHASH:" magic and keeps the printable
  chars that follow. Two hashes: fwHash (from the ihx image on every parse,
  internal only) and targetHash (read out of the connected chip's flash over
  SWIM on reconnect, shown on home next to the detected type, cleared on
  disconnect). A verify PASS copies fwHash into targetHash instead of
  re-reading; any flash attempt clears it first. Target firmware embeds it as
  `const char build_id[] = "GITHASH:" GIT_HASH;` with the hash passed in by its
  Makefile (`git rev-parse --short HEAD`) — see comet src/firmware.
- `config.h` — pins: SWIM=GPIO1, NRST=GPIO2. 1k external pull-up SWIM→3.3V
  required (internal ~45k is too weak for SWIM rise times).

## Behavior contracts (user-specified, don't regress)

- FAIL means verify mismatch ONLY. No target / entry failure / comm errors
  retry quietly — never shown as FAIL.
- Home line 1 = live detected target ("no target" / STM8S103 / STM8S003 via
  UID probe at 0x4865, polled every 1 s in both modes), NOT the configured name.
- Result holds on the flash page 10 s (pass and fail); FLASH_BUTTON during
  the hold retests immediately.
- The jig powers the target: no VDD sensing; SWIM entry IS target detection.
- WiFi is secondary — flashing must never wait on it (WiFiManager non-blocking).

## Current state / caveats

- Everything above is implemented, compiles, and is deployed to hardware; the
  wire protocol has NOT yet flashed a real chip successfully end-to-end.
- SWIM timing constants in swim.cpp (entry pulse pattern, 22-clock bit format,
  sync width window) were written from memory and are UNVERIFIED against ST
  UM0470 — that's the prime suspect for any entry/frame failures. Serial
  logs pinpoint the failing layer (sync pulse / CSR write / unlock / EOP).
- UID probe distinguishes 003/103 heuristically: blank UID ⇒ 003; present ⇒
  103-class silicon (many 003-marked dies also have it).
- Detection probe resets the target once per second (NRST + entry) — a
  connected chip only runs in ~1 s bursts while the jig is idle.
- Known gotcha: an unpowered target can be parasitically powered through the
  SWIM pull-up and still answer — "disconnected" tests must unplug SWIM too.
