/*
 * inputs.h — Button, ADC, and strum-interrupt driver interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * ── HARDWARE MAP ──────────────────────────────────────────────────
 *
 *   PD2   Fret 0 — Green  button  (active-low, internal pullup)
 *   PD3   Fret 1 — Red    button  (active-low, internal pullup)
 *   PD4   Fret 2 — Yellow button  (active-low, internal pullup)
 *   PD5   Fret 3 — Blue   button  (active-low, internal pullup)
 *   PD6   Fret 4 — Orange button  (active-low, internal pullup)
 *   PD7   Strum switch             (active-low, internal pullup, interrupt)
 *   PB0   Mute button              (active-low, internal pullup, polled)
 *   ADC0  Whammy bar potentiometer (0–1023)
 *   ADC1  Joystick X axis          (0–1023)
 *   ADC2  Joystick Y axis          (0–1023)
 */

#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

/* Returned by on_pitch_change() when all fret buttons are released */
#define FRET_NONE  0xFFU

/* ADC Readings*/
extern volatile uint16_t inputs_whammy;
extern volatile uint16_t inputs_joy_x;
extern volatile uint16_t inputs_joy_y;

void on_strum(void);
void on_mute(void);
void on_pitch_change(uint8_t fret);

/*
 * inputs_init()
 *   One-time peripheral setup via direct register writes (no HAL):
 *     DDRD / PORTD  — PD2–PD7 as inputs with internal pullups
 *     DDRB / PORTB  — PB0 as input with internal pullup
 *     PCICR / PCMSK2 — PCINT23 (PD7) strum interrupt
 *     ADMUX / ADCSRA — AVcc ref, right-adjust, prescaler ÷128
 *   Call before sei() in main().
 */
void inputs_init(void);

/*
 * inputs_tick()
 *   Advance all debounce counters by one tick.  Call every 1 ms.
 *   Fires on_pitch_change() when the dominant fret changes.
 *   Fires on_mute() once per stable mute-button press.
 *   Must not be called from the strum ISR.
 */
void inputs_tick(void);

/*
 * inputs_adc_scan()
 *   Blocking sequential read of ADC0, ADC1, ADC2 (~312 µs total).
 *   Updates inputs_whammy, inputs_joy_x, inputs_joy_y.
 *   Call from main loop only.
 */
void inputs_adc_scan(void);

#endif /* INPUTS_H */
