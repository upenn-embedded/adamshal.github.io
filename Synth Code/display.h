/*
 * display.h — SSD1306 128×64 OLED display driver interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

extern volatile uint8_t g_display_dirty;

void display_init(void);

void display_clear(void);

void display_set_cursor(uint8_t col, uint8_t page);

void display_print_char(char c);

void display_print_string(const char *s);


void display_print_note(uint8_t note_index);

void display_show_whammy(uint8_t adc_val);

void display_update(uint8_t note_idx, uint16_t whammy);

void display_update_ui(uint8_t note_index, uint8_t whammy,
                       bool muted, bool strumming);

#endif /* DISPLAY_H */
