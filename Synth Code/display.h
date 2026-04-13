/*
 * display.h — Adafruit 1.8" ST7735R 128×160 color TFT driver interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * Hardware: Adafruit product #358, ST7735R controller, 128×160 px, RGB565 color.
 * Interface: software SPI (bit-bang) — hardware SPI0 pins (PB3/SCK) are occupied
 *   by Timer2 PWM audio output.
 *
 * Pin wiring (ATmega328PB → Adafruit 358 breakout):
 *   PB4 → SDA  (MOSI — display data in)
 *   PB5 → SCK  (SCLK — display clock)
 *   PB2 → TCS  (CS, active-low)
 *   PC3 → A0   (DC: low = command, high = data)
 *   PC4 → RST  (RESET, active-low)
 *   3.3 V or 5 V → VIN  (board has onboard 3.3 V LDO)
 *   GND → GND
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* Set by display_update_ui; cleared after a successful refresh. */
extern volatile uint8_t g_display_dirty;

/* Initialises software SPI pins and runs the ST7735R power-on sequence. */
void display_init(void);

/* Fills the entire screen with black. */
void display_clear(void);

/* Moves the text cursor to the given column and page (1× scale: 6 px/col, 9 px/page). */
void display_set_cursor(uint8_t col, uint8_t page);

/* Renders one ASCII character at the current cursor position (1× scale, white on black). */
void display_print_char(char c);

/* Renders a null-terminated string at the current cursor position. */
void display_print_string(const char *s);

/* Renders a note name looked up by index (reads from PROGMEM via note_name_get). */
void display_print_note(uint8_t note_index);

/* Draws a 16-segment green whammy-bar indicator across the middle of the screen. */
void display_show_whammy(uint8_t adc_val);

/* Minimal refresh: note name (small) at top-left and whammy bar. */
void display_update(uint8_t note_idx, uint16_t whammy);

/* Full UI refresh: large centred note name, whammy bar, and colour-coded status line. */
void display_update_ui(uint8_t note_index, uint8_t whammy,
                       bool muted, bool strumming);

#endif /* DISPLAY_H */
