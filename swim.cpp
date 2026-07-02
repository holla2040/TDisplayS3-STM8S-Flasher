#include <Arduino.h>
#include "soc/gpio_struct.h"
#include "config.h"
#include "swim.h"

// ---------------------------------------------------------------------------
// Timing. ESP32-S3 CPU at 240 MHz; all bit timing done by cycle counting
// inside critical sections so FreeRTOS/WiFi interrupts can't stretch a bit.
//
// After activation the target's SWIM clock is HSI/2 = 8 MHz (UM0470).
// Low-speed bit format: 22 SWIM clocks per bit.
//   '0' = 20 clocks low + 2 high, '1' = 2 clocks low + 20 high.
// TODO: verify counts and the entry-sequence frequencies against UM0470
// before trusting on hardware. Structure is right; calibrate the numbers.
// ---------------------------------------------------------------------------
#define CPU_MHZ 240
static const uint32_t T_SWIM      = CPU_MHZ / 8;    // one SWIM clock, in CPU cycles
static const uint32_t BIT_TOTAL   = 22 * T_SWIM;
static const uint32_t BIT_LONG    = 20 * T_SWIM;
static const uint32_t BIT_SHORT   = 2 * T_SWIM;
static const uint32_t BIT_HALF    = 11 * T_SWIM;    // low-width threshold: shorter = '1'
static const uint32_t ACK_TIMEOUT = 200 * CPU_MHZ;  // 200 us: target may stretch before ACK

#define SWIM_CMD_SRST 0x00
#define SWIM_CMD_ROTF 0x01
#define SWIM_CMD_WOTF 0x02
#define FRAME_RETRIES 4
#define ROTF_MAX      255   // max byte count per ROTF/WOTF command

static portMUX_TYPE swimMux = portMUX_INITIALIZER_UNLOCKED;

// Direct register access: digitalWrite is too slow for 250 ns pulses.
static inline void drv_low()  { GPIO.out_w1tc = 1UL << SWIM_PIN; }
static inline void drv_high() { GPIO.out_w1ts = 1UL << SWIM_PIN; }  // open-drain: release
static inline bool line()     { return GPIO.in & (1UL << SWIM_PIN); }
static inline uint32_t cyc()  { return ESP.getCycleCount(); }
static inline void wait_from(uint32_t start, uint32_t cycles) {
  while (cyc() - start < cycles) {}
}

void swim_init() {
  // Internal pull-ups (~45k) so the lines idle high even with nothing else
  // on them — but that's far too weak for SWIM's rise times. A real ~1k
  // pull-up from SWIM to target VDD is required for reliable comms.
  pinMode(SWIM_PIN, OUTPUT_OPEN_DRAIN | PULLUP);
  digitalWrite(SWIM_PIN, HIGH);
  pinMode(NRST_PIN, OUTPUT_OPEN_DRAIN | PULLUP);
  digitalWrite(NRST_PIN, HIGH);
}

void swim_reset_target(bool assert) {
  digitalWrite(NRST_PIN, assert ? LOW : HIGH);
}

// --- bit layer (call inside critical section) ------------------------------

static void IRAM_ATTR send_bit(bool b) {
  uint32_t t0 = cyc();
  drv_low();
  wait_from(t0, b ? BIT_SHORT : BIT_LONG);
  drv_high();
  wait_from(t0, BIT_TOTAL);
}

// Receive one target-driven bit by measuring the low-pulse width.
// Returns 0/1, or -1 on timeout.
static int IRAM_ATTR recv_bit(uint32_t timeout_cycles) {
  uint32_t t0 = cyc();
  while (line()) {
    if (cyc() - t0 > timeout_cycles) return -1;
  }
  uint32_t fall = cyc();
  while (!line()) {
    if (cyc() - fall > 2 * BIT_TOTAL) return -1;
  }
  return (cyc() - fall) < BIT_HALF ? 1 : 0;
}

// --- frame layer ------------------------------------------------------------
// Host frame: header '0' + nbits payload (MSB first) + parity, then the
// target drives an ACK bit ('1' = accepted, '0' = retransmit).
// Parity = XOR of payload bits (UM0470).

static bool IRAM_ATTR send_frame(uint32_t data, int nbits) {
  for (int attempt = 0; attempt < FRAME_RETRIES; attempt++) {
    portENTER_CRITICAL(&swimMux);
    send_bit(0);
    uint8_t parity = 0;
    for (int i = nbits - 1; i >= 0; i--) {
      bool b = (data >> i) & 1;
      parity ^= b;
      send_bit(b);
    }
    send_bit(parity);
    int ack = recv_bit(ACK_TIMEOUT);
    portEXIT_CRITICAL(&swimMux);
    if (ack == 1) return true;
    if (ack < 0) return false;  // no ACK at all: target gone, don't hammer it
  }
  return false;
}

// Target frame: header + 8 data bits + parity; host answers ACK/NAK.
// Returns byte value, or -1 on failure.
static int IRAM_ATTR recv_frame() {
  for (int attempt = 0; attempt < FRAME_RETRIES; attempt++) {
    portENTER_CRITICAL(&swimMux);
    int hdr = recv_bit(ACK_TIMEOUT);
    bool ok = (hdr >= 0);
    uint32_t v = 0;
    uint8_t parity = 0;
    for (int i = 0; ok && i < 8; i++) {
      int b = recv_bit(2 * BIT_TOTAL);
      if (b < 0) { ok = false; break; }
      parity ^= b;
      v = (v << 1) | b;
    }
    if (ok) {
      int pb = recv_bit(2 * BIT_TOTAL);
      ok = (pb == parity);
    }
    send_bit(ok ? 1 : 0);
    portEXIT_CRITICAL(&swimMux);
    if (ok) return v;
    if (hdr < 0) return -1;  // nothing arrived: don't spin on retries
  }
  return -1;
}

// --- commands ----------------------------------------------------------------

bool swim_srst() {
  return send_frame(SWIM_CMD_SRST, 3);
}

static bool rotf_chunk(uint32_t addr, uint8_t *buf, uint8_t n) {
  if (!send_frame(SWIM_CMD_ROTF, 3)) return false;
  if (!send_frame(n, 8)) return false;
  if (!send_frame((addr >> 16) & 0xFF, 8)) return false;
  if (!send_frame((addr >> 8) & 0xFF, 8)) return false;
  if (!send_frame(addr & 0xFF, 8)) return false;
  for (uint8_t i = 0; i < n; i++) {
    int v = recv_frame();
    if (v < 0) return false;
    buf[i] = v;
  }
  return true;
}

static bool wotf_chunk(uint32_t addr, const uint8_t *buf, uint8_t n) {
  if (!send_frame(SWIM_CMD_WOTF, 3)) return false;
  if (!send_frame(n, 8)) return false;
  if (!send_frame((addr >> 16) & 0xFF, 8)) return false;
  if (!send_frame((addr >> 8) & 0xFF, 8)) return false;
  if (!send_frame(addr & 0xFF, 8)) return false;
  for (uint8_t i = 0; i < n; i++) {
    if (!send_frame(buf[i], 8)) return false;
  }
  return true;
}

bool swim_rotf(uint32_t addr, uint8_t *buf, size_t len) {
  while (len) {
    uint8_t n = len > ROTF_MAX ? ROTF_MAX : len;
    if (!rotf_chunk(addr, buf, n)) {
      Serial.printf("swim: rotf failed @ 0x%06lX len %u\n", addr, n);
      return false;
    }
    addr += n; buf += n; len -= n;
  }
  return true;
}

bool swim_wotf(uint32_t addr, const uint8_t *buf, size_t len) {
  while (len) {
    uint8_t n = len > ROTF_MAX ? ROTF_MAX : len;
    if (!wotf_chunk(addr, buf, n)) {
      Serial.printf("swim: wotf failed @ 0x%06lX len %u\n", addr, n);
      return false;
    }
    addr += n; buf += n; len -= n;
  }
  return true;
}

// --- activation ----------------------------------------------------------------
// Entry sequence (UM0470 §SWIM entry): wake the pin, then a recognizable
// pulse pattern; the target hands the pin to the SWIM module and answers
// with a sync pulse (~128 HSI clocks low, ~16 us) that we measure.
// TODO: verify pulse counts/frequencies against UM0470 §5.3 on first hardware bring-up.

bool swim_entry() {
  drv_low();
  delay(1);                                   // wake: hold low well past the 16 us minimum
  for (int i = 0; i < 4; i++) {               // 4 pulses at 1 kHz
    drv_high(); delayMicroseconds(500);
    drv_low();  delayMicroseconds(500);
  }
  for (int i = 0; i < 4; i++) {               // 4 pulses at 2 kHz
    drv_high(); delayMicroseconds(250);
    drv_low();  delayMicroseconds(250);
  }
  drv_high();

  // Wait for the target's sync pulse and sanity-check its width.
  uint32_t t0 = cyc();
  while (line()) {
    if (cyc() - t0 > 500 * CPU_MHZ) {
      // no target answering is the idle condition — don't log every 250 ms poll
      static uint32_t misses = 0;
      if (++misses % 20 == 1)
        Serial.printf("swim: entry sent, no sync pulse (attempt %lu)\n", misses);
      return false;
    }
  }
  uint32_t fall = cyc();
  while (!line()) {
    if (cyc() - fall > 100 * CPU_MHZ) {
      Serial.println("swim: sync pulse stuck low >100us");
      return false;
    }
  }
  uint32_t low_us = (cyc() - fall) / CPU_MHZ;
  // ponytail: fixed 8 MHz SWIM clock assumed; use measured sync width to
  // scale bit timing if HSI tolerance causes frame errors in production.
  bool ok = low_us > 8 && low_us < 40;
  Serial.printf("swim: sync pulse %lu us after %lu us%s\n",
                low_us, (fall - t0) / CPU_MHZ, ok ? "" : " (expected 8-40 us)");
  return ok;
}
