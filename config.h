#pragma once

// --- Pin assignments (LilyGO T-Display S3; adjust to your jig wiring) ---
// SWIM and NRST go through a BSS138 open-drain level shifter to the target.
// The jig powers the target, so there is no VDD sensing: a successful SWIM
// entry (sync pulse received) IS the target-present detection.
#define SWIM_PIN        1   // bidirectional, open-drain, 1k external pull-up on target side
#define NRST_PIN        2   // open-drain to target reset

// T-Display S3 user buttons (per LilyGO schematic: BOOT=IO0, KEY=IO14)
#define MODE_BUTTON     0   // toggle auto/manual; hold at boot to reset WiFi
#define FLASH_BUTTON    14  // manual mode: press to flash
