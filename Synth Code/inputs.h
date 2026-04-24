#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

#define FRET_NONE  0xFFU

extern volatile uint16_t inputs_whammy;

void on_fret_change(uint8_t fret);
void on_button_press(uint8_t fret);
void on_button_release(uint8_t fret);
void on_strum_press(void);
void on_strum_release(void);
void on_mute_press(void);

void inputs_init(void);
void inputs_tick(void);
void inputs_adc_scan(void);

#endif /* INPUTS_H */
