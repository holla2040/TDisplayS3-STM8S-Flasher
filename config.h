#pragma once

// --- Pin assignments (LilyGO T-Display S3; adjust to your jig wiring) ---
// Direct 3.3 V connection to the target (no level shifter); see README Wiring.
// The jig powers the target, so there is no VDD sensing: a successful SWIM
// entry (sync pulse received) IS the target-present detection.
#define SWIM_PIN        1   // bidirectional, open-drain; needs a 1k pull-up to 3.3 V
#define NRST_PIN        2   // open-drain to target reset

// T-Display S3 user buttons (per LilyGO schematic: BOOT=IO0, KEY=IO14)
#define MODE_BUTTON     0   // short press: toggle auto/manual; hold 5 s: forget WiFi
#define FLASH_BUTTON    14  // manual mode: press to flash
