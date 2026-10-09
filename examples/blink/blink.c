// Jig bring-up image: blinks the PB5 LED on the common blue STM8S103F3 board
// (LED from 3V3 to PB5, so low = on). Build: sdcc -mstm8 --std-c99 blink.c
#include <stdint.h>

#define PB_ODR (*(volatile uint8_t *)0x5005)
#define PB_DDR (*(volatile uint8_t *)0x5007)

void main(void) {
  PB_DDR |= 1 << 5;   // PB5 is a true open-drain pin: output low sinks the LED
  for (;;) {
    PB_ODR ^= 1 << 5;
    for (volatile uint16_t i = 0; i < 30000; i++) {}   // roughly 0.25 s at the 2 MHz reset clock
  }
}
