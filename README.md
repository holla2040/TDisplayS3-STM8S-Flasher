# TDisplayS3-STM8S-Flasher

Standalone STM8 production flasher: a LilyGO [T-Display S3](https://lilygo.cc/en-us/products/t-display-s3)
(ESP32-S3) programs STM8S103/STM8S003 targets over ST's SWIM interface — no PC,
no ST-Link. Firmware images are uploaded to the jig over WiFi and stored on it;
a technician connects a board, presses a button (or lets auto mode detect it),
and gets PASS/FAIL on the display.

## Wiring

3.3 V target, direct connection:

| T-Display S3 | Target STM8 |
|---|---|
| GPIO 1 | SWIM — with a **1 kΩ pull-up to 3.3 V** (required; internal pull-up is too weak for SWIM rise times) |
| GPIO 2 | NRST |
| GND | GND |
| jig 3.3 V | VDD |

For 5 V targets add a BSS138-style open-drain level shifter on SWIM and NRST —
ESP32 pins are not 5 V tolerant.

Buttons (on-board): GPIO0 (BOOT) toggles manual/auto mode, and held at power-up
resets WiFi credentials. GPIO14 (KEY) triggers a flash in manual mode.

## Build and deploy

Requires arduino-cli with the esp32 core (3.x) and the TFT_eSPI library
configured for the T-Display S3 (`Setup206_LilyGo_T_Display_S3`).

```
make bin       # compile (16MB flash, 3M APP/9.9M FATFS partition, OPI PSRAM)
make usb       # upload over /dev/ttyACM0
make ota       # upload over WiFi to STM8Flasher.local
```

First boot opens a WiFiManager setup AP named `STM8Flasher`; once on your
network the jig is reachable at `http://STM8Flasher.local/`.

## Loading a target firmware image

Any of:

```
make image IHX=path/to/app.ihx
curl -F ihx=@app.ihx http://STM8Flasher.local/upload
# or the upload form at http://STM8Flasher.local/
```

The Intel HEX file is validated (parse + checksums) before it replaces the
stored image, and persists on the jig's flash across reboots. A successful
upload immediately flashes the connected target with the new image (no button
press needed); with no target connected the jig just carries on.

### Build id

The home screen shows the *connected chip's* build id next to the detected
type: when a target (re)connects, its flash is read out over SWIM and scanned
for the magic string `GITHASH:`; the printable characters that follow are the
build id. Disconnecting the target hides it, and a chip without the magic
shows none. After a passing flash the id is taken from the just-written image
(the verify proves they match) instead of a re-read. To embed one, compile a
plain string into the target firmware, e.g.:

```c
const char build_id[] = "GITHASH:" GIT_HASH;   /* -DGIT_HASH='"abc1234"' from the Makefile */
```

with the Makefile passing `-DGIT_HASH='"$(shell git rev-parse --short HEAD)"'`.

## Operation

Two screens. **Home** shows the live-detected target on line 1 (`no target`,
`STM8S103`, or `STM8S003` — probed once a second via SWIM entry + the 96-bit
UID at 0x4865) with that chip's build id, image size, pass/fail counters, mode letter
(`M`/`A`) top right, and IP address. **Flash** shows write/read progress in
bytes and ends with a green PASS or red FAIL bar that holds for 10 seconds
(press the flash button during the hold to retest immediately).

FAIL means exactly one thing: the read-back didn't match the image. A missing
target or a communication hiccup is never a FAIL — the jig just keeps trying.

## Status

Compiles and runs on hardware; the SWIM wire driver's timing constants are
structured per ST UM0470 but not yet verified end-to-end against a real
target. See `docs/architecture.md` for the design and `CLAUDE.md` for
development notes and behavior contracts.

## License

MIT — see [LICENSE](LICENSE).
