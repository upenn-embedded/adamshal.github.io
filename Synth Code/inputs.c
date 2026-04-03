/*
 * inputs.c — Button debounce, ADC scan, and strum interrupt
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include "inputs.h"
#include "notes.h"

#define FRET_SHIFT       2U
#define STRUM_PIN        PD7
#define MUTE_PIN         PB0
#define DEBOUNCE_THRESH  10U

volatile uint16_t inputs_whammy = 512U;
volatile uint16_t inputs_joy_x  = 512U;
volatile uint16_t inputs_joy_y  = 512U;

static uint8_t fret_cnt[5] = {0, 0, 0, 0, 0};
static uint8_t fret_state  = 0U;
static uint8_t mute_cnt    = 0U;
static uint8_t mute_state  = 0U;
static uint8_t last_fret   = FRET_NONE;

/* Reads and returns a 10-bit ADC result from the given channel. */
static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (1 << REFS0) | (channel & 0x07U);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    uint8_t  low  = ADCL;
    uint16_t high = ADCH;
    return (high << 8) | low;
}

/* Configures fret, mute, and strum pins, enables PCINT23, and initialises the ADC. */
void inputs_init(void)
{
    DDRD  &= ~((1 << PD2) | (1 << PD3) | (1 << PD4) |
               (1 << PD5) | (1 << PD6) | (1 << PD7));
    PORTD |=  ((1 << PD2) | (1 << PD3) | (1 << PD4) |
               (1 << PD5) | (1 << PD6) | (1 << PD7));

    DDRB  &= ~(1 << PB0);
    PORTB |=  (1 << PB0);

    PCICR  |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT23);

    ADMUX  = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    (void)ADCL; (void)ADCH;
}

/* Debounces all buttons each millisecond and fires callbacks on state changes. */
void inputs_tick(void)
{
    uint8_t raw_frets = (~PIND >> FRET_SHIFT) & 0x1FU;

    for (uint8_t i = 0; i < 5U; i++) {
        uint8_t pressed = (raw_frets >> i) & 0x01U;

        if (pressed) {
            if (fret_cnt[i] < DEBOUNCE_THRESH) fret_cnt[i]++;
        } else {
            if (fret_cnt[i] > 0U)              fret_cnt[i]--;
        }

        if (fret_cnt[i] == DEBOUNCE_THRESH && !(fret_state & (1U << i))) {
            fret_state |= (1U << i);
        }
        if (fret_cnt[i] == 0U && (fret_state & (1U << i))) {
            fret_state &= ~(1U << i);
        }
    }

    uint8_t cur_fret = FRET_NONE;
    for (uint8_t i = 0; i < 5U; i++) {
        if (fret_state & (1U << i)) {
            cur_fret = i;
            break;
        }
    }

    if (cur_fret != last_fret) {
        last_fret = cur_fret;
        on_pitch_change(cur_fret);
    }

    uint8_t mute_raw = !(PINB & (1 << MUTE_PIN));

    if (mute_raw) {
        if (mute_cnt < DEBOUNCE_THRESH) mute_cnt++;
    } else {
        if (mute_cnt > 0U) mute_cnt--;
    }

    if (mute_cnt == DEBOUNCE_THRESH && !mute_state) {
        mute_state = 1U;
        on_mute();
    }
    if (mute_cnt == 0U && mute_state) {
        mute_state = 0U;
    }
}

/* Reads whammy (ADC0), joystick X (ADC1), and joystick Y (ADC2). */
void inputs_adc_scan(void)
{
    inputs_whammy = adc_read(0U);
    inputs_joy_x  = adc_read(1U);
    inputs_joy_y  = adc_read(2U);
}

ISR(PCINT2_vect)
{
    if (!(PIND & (1 << STRUM_PIN))) {
        on_strum();
    }
}
