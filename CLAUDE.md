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
(live status, target .ihx upload, flasher firmware update); images persist in
FFat as /firmware.ihx. flashing-shop.md is a local, untracked note (.gitignore).

## Layout

- `TDisplayS3-STM8S-Flasher.ino` — UI (2 pages: home, flash), state machine,
  wifi/OTA/mDNS/webserver, FFat image storage. Auto mode default (no buttons on the
  enclosure; mode letter hidden). MODE_BUTTON (GPIO0): short press (<1 s, on
  release) toggles auto/manual, held 5 s forgets WiFi and reboots (it can't be
  read at power-up: GPIO0 is the boot strap). FLASH_BUTTON (GPIO14) tests;
  tests start on debounced release (a press must not spill into the next page
  and double-fire).
- `swim.cpp/h` — bit-banged SWIM wire driver (entry, sync, pulse-width bits,
  parity/ACK frames, ROTF/WOTF/SRST). Cycle-counted timing in critical
  sections, direct GPIO register access.
- `stm8.cpp/h` — device table, flash unlock/block-program/verify, UID probe.
- `webpage.h` — the web page (PROGMEM HTML); /status JSON, /upload (.ihx), /update (.ino.bin).
- `ihx.cpp/h` — Intel HEX parser (host-testable: see test_ihx.c.txt header).
- `config.h` — pins: SWIM=GPIO1, NRST=GPIO2. 1k external pull-up SWIM→3.3V
  required (internal ~45k is too weak for SWIM rise times).

## Behavior contracts (user-specified, don't regress)

- FAIL means verify mismatch ONLY. No target / entry failure / comm errors
  are never shown as FAIL. A flash that gets comm errors 3x in a row shows an
  orange "comm error" notice (flashWithRetry), not a FAIL.
- Home line 1 = live detected target ("no target" / STM8S103 / STM8S003 via
  UID probe at 0x4865, polled every 1 s in both modes), NOT the configured name.
- Result holds on the flash page 1 s (pass and fail); FLASH_BUTTON during
  the hold retests immediately. The last result word then stays on the home
  screen (line 4) until the board is removed.
- The jig powers the target: no VDD sensing; SWIM entry IS target detection.
- Auto mode flashes every inserted target, once per insertion (`flashed`, set
  after ANY outcome including comm error, cleared on disconnect). No reading
  of the chip's existing firmware or build id — it's a flasher, not a reader.
  Never reflash a chip that stays in the jig (S003 flash endurance is 100
  cycles).
- WiFi is secondary — flashing must never wait on it (WiFiManager non-blocking).

## Current state / caveats

- In production: ~50 boards flashed successfully (as of 2026-10-09). The SWIM
  timing in swim.cpp works on real parts; if entry/frame failures ever show
  up, serial logs pinpoint the failing layer (sync pulse / CSR write / unlock / EOP).
- UID probe distinguishes 003/103 heuristically: blank UID ⇒ 003; present ⇒
  103-class silicon (many 003-marked dies also have it).
- Detection probe resets the target once per second (NRST + entry) — a
  connected chip only runs in ~1 s bursts while the jig is idle.
- Known gotcha: an unpowered target can be parasitically powered through the
  SWIM pull-up and still answer — "disconnected" tests must unplug SWIM too.
