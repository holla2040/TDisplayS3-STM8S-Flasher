#pragma once
#include <stdint.h>
#include <stddef.h>

// SWIM wire driver: entry sequence, bit-banged frames, ROTF/WOTF/SRST.
// Timing reference: ST UM0470 "STM8 SWIM communication protocol and debug module".

void swim_init();                       // configure SWIM/NRST GPIOs, idle high
void swim_reset_target(bool assert);    // drive NRST low (true) or release (false)
bool swim_entry();                      // activation sequence + wait for target sync pulse
bool swim_srst();                       // SWIM system-reset command
bool swim_rotf(uint32_t addr, uint8_t *buf, size_t len);        // read on the fly
bool swim_wotf(uint32_t addr, const uint8_t *buf, size_t len);  // write on the fly
