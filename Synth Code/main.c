#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h>

#include "uart.h"
#include "spi_dac.h"
#include "synth.h"
#include "inputs.h"
#include "notes.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define WHAMMY_CUTOFF_ADC      614U   /* ~3.0 V at 5 V reference */
#define WHAMMY_MAX_ADC         941U   /* ~4.6 V at 5 V reference */
#define WHAMMY_MAX_VIBRATO_PCT 4U

/*
 * Button -> chord index map
 *   GREEN  -> D4 chord
 *   RED    -> F4 chord
 *   YELLOW -> G4 chord
 *   BLUE   -> Ab4 chord
 *   ORANGE -> D5 chord
 */
static const uint8_t fret_chord_map[5] = {
    0U,  /* D4  */
    1U,  /* F4  */
    2U,  /* G4  */
    3U,  /* Ab4 */
    4U   /* D5  */
};

static volatile uint8_t  g_active_fret          = FRET_NONE;
static volatile uint8_t  g_fret_changed         = 0U;
static volatile uint8_t  g_strum_pressed        = 0U;
static volatile uint8_t  g_strum_released       = 0U;
static volatile uint8_t  g_mute_pressed         = 0U;
static volatile uint8_t  g_button_press_flags   = 0U;
static volatile uint8_t  g_button_release_flags = 0U;
static volatile uint32_t g_ms_tick              = 0UL;

static uint8_t g_strum_latched = 0U;  /* 1 while strum is physically held */

static const char *fret_name(uint8_t fret)
{
    switch (fret) {
    case 0U: return "GREEN";
    case 1U: return "RED";
    case 2U: return "YELLOW";
    case 3U: return "BLUE";
    case 4U: return "ORANGE";
    default: return "NONE";
    }
}

static const char *chord_name_for_fret(uint8_t fret)
{
    switch (fret) {
    case 0U: return "D4 chord";
    case 1U: return "F4 chord";
    case 2U: return "G4 chord";
    case 3U: return "Ab4 chord";
    case 4U: return "D5 chord";
    default: return "NONE";
    }
}

/* 1 ms system tick for debounce + timing. */
static void timer0_init(void)
{
    TCCR0A = (1 << WGM01);
    OCR0A  = 249U;
    TCCR0B = (1 << CS01) | (1 << CS00);
    TIMSK0 = (1 << OCIE0A);
}

ISR(TIMER0_COMPA_vect)
{
    g_ms_tick++;
    inputs_tick();
    synth_decay_tick_1ms();
}

void on_fret_change(uint8_t fret)
{
    g_active_fret  = fret;
    g_fret_changed = 1U;
}

void on_button_press(uint8_t fret)
{
    if (fret < 5U) {
        g_button_press_flags |= (uint8_t)(1U << fret);
    }
}

void on_button_release(uint8_t fret)
{
    if (fret < 5U) {
        g_button_release_flags |= (uint8_t)(1U << fret);
    }
}

void on_strum_press(void)
{
    g_strum_pressed = 1U;
}

void on_strum_release(void)
{
    g_strum_released = 1U;
}

void on_mute_press(void)
{
    g_mute_pressed = 1U;
}

int main(void)
{
    uint32_t last_whammy_ms   = 0UL;
    uint32_t last_vibrato_ms  = 0UL;
    uint8_t  last_vibrato_pct = 0U;

    uart_init();
    printf("\r\nGuitar Hero input debug start\r\n");
    printf("Watching GREEN, RED, YELLOW, BLUE, ORANGE, STRUM, WHAMMY, and MUTE\r\n");

    pwm_audio_init();
    synth_init();
    inputs_init();
    timer0_init();

    printf("System initialized\r\n");
    printf("PB2 PWM chord mode with active-high mute on PB0\r\n");

    sei();

    for (;;) {
        uint8_t press_flags;
        uint8_t release_flags;
        uint32_t now_ms;

        cli();
        press_flags = g_button_press_flags;
        g_button_press_flags = 0U;
        release_flags = g_button_release_flags;
        g_button_release_flags = 0U;
        now_ms = g_ms_tick;
        sei();

        for (uint8_t i = 0U; i < 5U; i++) {
            if (press_flags & (1U << i)) {
                printf("%s pressed -> %s\r\n", fret_name(i), chord_name_for_fret(i));
            }
            if (release_flags & (1U << i)) {
                printf("%s released\r\n", fret_name(i));
            }
        }

        if (g_fret_changed) {
            uint8_t fret;

            cli();
            fret = g_active_fret;
            g_fret_changed = 0U;
            sei();

            printf("Active fret: %s\r\n", fret_name(fret));
        }

        /*
         * One new physical strum press always restarts the chord at full volume,
         * but only after a release has re-armed the latch.
         */
        if (g_strum_pressed) {
            cli();
            g_strum_pressed = 0U;
            sei();

            if (!g_strum_latched) {
                g_strum_latched = 1U;
                printf("STRUM pressed\r\n");

                if (g_active_fret < 5U) {
                    synth_set_chord(fret_chord_map[g_active_fret]);
                    printf("Playing %s\r\n", chord_name_for_fret(g_active_fret));
                } else {
                    printf("No fret held, nothing triggered\r\n");
                }
            }
        }

        if (g_strum_released) {
            cli();
            g_strum_released = 0U;
            sei();

            g_strum_latched = 0U;
            printf("STRUM released\r\n");
        }

        if (g_mute_pressed) {
            cli();
            g_mute_pressed = 0U;
            sei();

            synth_mute();
            synth_set_vibrato_depth(0U);
            synth_reset_vibrato();
            last_vibrato_pct = 0U;
            printf("MUTE pressed\r\n");
        }

        /* Whammy controls vibrato depth only. */
        if ((uint32_t)(now_ms - last_whammy_ms) >= 10UL) {
            uint16_t adc;
            uint8_t vibrato_pct;

            last_whammy_ms = now_ms;
            inputs_adc_scan();
            adc = inputs_whammy;

            if (adc < WHAMMY_CUTOFF_ADC) {
                vibrato_pct = 0U;
            } else if (adc >= WHAMMY_MAX_ADC) {
                vibrato_pct = WHAMMY_MAX_VIBRATO_PCT;
            } else {
                uint32_t num = (uint32_t)(adc - WHAMMY_CUTOFF_ADC) * (uint32_t)WHAMMY_MAX_VIBRATO_PCT;
                uint32_t den = (uint32_t)(WHAMMY_MAX_ADC - WHAMMY_CUTOFF_ADC);
                vibrato_pct = (uint8_t)(num / den);
            }

            if (vibrato_pct != last_vibrato_pct) {
                if (vibrato_pct == 0U) {
                    synth_set_vibrato_depth(0U);
                    synth_reset_vibrato();
                } else {
                    synth_set_vibrato_depth(vibrato_pct);
                }

                last_vibrato_pct = vibrato_pct;
            }
        }

        /* Advance vibrato only while a chord is actually active. */
        if (synth_is_active() && last_vibrato_pct > 0U && (uint32_t)(now_ms - last_vibrato_ms) >= 10UL) {
            last_vibrato_ms = now_ms;
            synth_vibrato_tick();
        }
    }
}
