/*
 * display.h ? ST7735 160x128 note-mapping UI for Guitar Synth project
 *
 * Updated pin wiring (ATmega328PB ? Adafruit ST7735 TFT):
 *   PB4 ? TFT SDA / MOSI
 *   PB5 ? TFT SCK
 *   PB1 ? TFT CS      (moved off PB2 because PB2 is the audio output)
 *   PC3 ? TFT DC / A0
 *   PC4 ? TFT RST
 *
 * Recommended joystick pins with the current project pinout:
 *   PC1 / ADC1 ? joystick vertical axis  (up/down button selection)
 *   PC2 / ADC2 ? joystick horizontal axis (left/right note wheel)
 *   PC5 / ADC5 or any free GPIO ? optional joystick pushbutton
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_NUM_BUTTONS  5U
#define DISPLAY_COMMIT_MS    500UL

extern volatile uint8_t g_display_dirty;

void display_init(void);
void display_clear(void);

/* Legacy text helpers retained so older code still compiles. */
void display_set_cursor(uint8_t col, uint8_t page);
void display_print_char(char c);
void display_print_string(const char *s);
void display_print_note(uint8_t note_index);
void display_show_whammy(uint8_t adc_val);
void display_update(uint8_t note_idx, uint16_t whammy);
void display_update_ui(uint8_t note_index, uint8_t whammy,
                       bool muted, bool strumming);

/* New note-mapper UI API. */
void display_force_redraw(void);
void display_ui_tick(uint32_t now_ms);
void display_move_button_selection(int8_t dir);
void display_move_note_selection(int8_t dir, uint32_t now_ms);
void display_set_button_note(uint8_t button_idx, uint8_t note_idx);
uint8_t display_get_button_note(uint8_t button_idx);
uint8_t display_get_selected_button(void);
uint8_t display_get_selected_note(void);
void display_copy_button_notes(uint8_t dest[DISPLAY_NUM_BUTTONS]);

#endif /* DISPLAY_H */
