/*
 * synth.c — DDS wavetable synthesis engine and Timer1 ISR
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "synth.h"
#include "spi_dac.h"
#include "notes.h"
#include "envelope.h"

/* 256-entry sine wavetable, uint16_t, stored in flash. */
static const uint16_t sine_table[256] PROGMEM = {
    32768, 33572, 34375, 35179, 35981, 36779, 37576, 38370,
    39161, 39948, 40731, 41508, 42280, 43047, 43808, 44562,
    45307, 46046, 46778, 47501, 48215, 48919, 49614, 50299,
    50973, 51637, 52288, 52929, 53557, 54172, 54774, 55363,
    55938, 56500, 57048, 57581, 58099, 58601, 59088, 59559,
    60014, 60453, 60876, 61280, 61668, 62038, 62391, 62726,
    63043, 63342, 63622, 63884, 64127, 64351, 64556, 64742,
    64909, 65056, 65184, 65292, 65381, 65453, 65502, 65525,
    65535, 65525, 65502, 65453, 65381, 65292, 65184, 65056,
    64909, 64742, 64556, 64351, 64127, 63884, 63622, 63342,
    63043, 62726, 62391, 62038, 61668, 61280, 60876, 60453,
    60014, 59559, 59088, 58601, 58099, 57581, 57048, 56500,
    55938, 55363, 54774, 54172, 53557, 52929, 52288, 51637,
    50973, 50299, 49614, 48919, 48215, 47501, 46778, 46046,
    45307, 44562, 43808, 43047, 42280, 41508, 40731, 39948,
    39161, 38370, 37576, 36779, 35981, 35179, 34375, 33572,
    32767, 31963, 31160, 30356, 29554, 28756, 27959, 27165,
    26374, 25587, 24804, 24027, 23255, 22488, 21727, 20973,
    20228, 19489, 18757, 18034, 17320, 16616, 15921, 15236,
    14562, 13898, 13247, 12606, 11978, 11363, 10761, 10172,
     9597,  9035,  8487,  7954,  7436,  6934,  6447,  5976,
     5521,  5082,  4659,  4255,  3867,  3497,  3144,  2809,
     2492,  2193,  1913,  1651,  1408,  1184,   979,   793,
      626,   479,   351,   243,   154,    82,    33,    10,
        0,    10,    33,    82,   154,   243,   351,   479,
      626,   793,   979,  1184,  1408,  1651,  1913,  2193,
     2492,  2809,  3144,  3497,  3867,  4255,  4659,  5082,
     5521,  5976,  6447,  6934,  7436,  7954,  8487,  9035,
     9597, 10172, 10761, 11363, 11978, 12606, 13247, 13898,
    14562, 15236, 15921, 16616, 17320, 18034, 18757, 19489,
    20228, 20973, 21727, 22488, 23255, 24027, 24804, 25587,
    26374, 27165, 27959, 28756, 29554, 30356, 31160, 31963
};

/* Q16 pitch-bend multiplier table, -12 to +12 semitones (index 0–24). */
static const uint32_t bend_table[25] PROGMEM = {
     32768UL,  34716UL,  36781UL,  38968UL,  41285UL,
     43739UL,  46341UL,  49096UL,  52016UL,  55109UL,
     58386UL,  61857UL,  65536UL,  69432UL,  73562UL,
     77936UL,  82570UL,  87480UL,  92682UL,  98193UL,
    104032UL, 110218UL, 116771UL, 123714UL, 131072UL
};

static volatile uint32_t g_phase_acc  = 0UL;
static volatile uint32_t g_phase_inc  = 0UL;
static volatile uint8_t  g_muted      = 1U;

static uint32_t g_base_phase_inc = 0UL;
static uint8_t  g_bend_idx       = 12U;

/* Recalculates g_phase_inc from the base increment and current bend index. */
static void apply_bend(void)
{
    uint32_t mult = pgm_read_dword(&bend_table[g_bend_idx]);
    uint32_t bent = (uint32_t)(((uint64_t)g_base_phase_inc * mult) >> 16);
    g_phase_inc = bent;
}

ISR(TIMER1_COMPA_vect)
{
    g_phase_acc += g_phase_inc;

    uint8_t  idx    = (uint8_t)(g_phase_acc >> 24);
    uint16_t sample = pgm_read_word(&sine_table[idx]);

    if (g_muted) {
        sample = 32768U;
    }

    envelope_tick();

    uint16_t gain = envelope_get();
    uint32_t out  = ((uint32_t)sample       * (uint32_t)gain +
                     (uint32_t)32768U * (uint32_t)(65535U - gain)) >> 16;

    pwm_audio_write((uint16_t)out);
}

/* Starts Timer1 in CTC mode to fire the audio ISR at 31250 Hz. */
void synth_init(void)
{
    TCCR1B = 0x00;
    TCCR1A = 0x00;
    OCR1A  = 511U;
    TCNT1  = 0U;
    TIFR1  = (1 << OCF1A);
    TIMSK1 = (1 << OCIE1A);
    TCCR1B = (1 << WGM12) | (1 << CS10);
}

/* Sets the DDS frequency to the given note index and unmutes. */
void synth_set_note(uint8_t note_idx)
{
    if (note_idx >= NUM_GUITAR_NOTES) {
        return;
    }
    g_base_phase_inc = pgm_read_dword(&note_phase_inc[note_idx]);
    uint8_t sreg = SREG;
    cli();
    apply_bend();
    g_phase_acc = 0UL;
    g_muted     = 0U;
    SREG = sreg;
}

/* Silences the audio output. */
void synth_mute(void)
{
    g_muted = 1U;
}

/* Applies pitch bend in semitones (-12 to +12) to the current note. */
void synth_set_bend(int8_t semitones)
{
    if (semitones < -12) semitones = -12;
    if (semitones >  12) semitones =  12;
    g_bend_idx = (uint8_t)(semitones + 12);
    uint8_t sreg = SREG;
    cli();
    apply_bend();
    SREG = sreg;
}
