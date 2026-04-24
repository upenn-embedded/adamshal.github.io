/*
 * main.c — System init and main loop
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * ── Init order ───────────────────────────────────────────────────
 *   uart_init()       9600 baud TX/RX
 *   pwm_audio_init()  Timer1 Fast PWM on OC1B
 *   synth_init()      DDS engine + Timer2 ISR
 *   inputs_init()     GPIO, ADC, PCINT
 *   timer0_init()     1 ms system tick (Timer0 CTC)
 *   display_init()    ST7796S LCD — call before sei()
 *   sei()
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h>

#include "uart.h"
#include "spi_dac.h"
#include "synth.h"
#include "inputs.h"
#include "notes.h"
#include "display.h"

/* ── Joystick ADC thresholds ────────────────────────────────────── */
#define JOY_SCAN_MS         20UL
#define JOY_LOW_THRESHOLD  120U
#define JOY_HIGH_THRESHOLD 900U
#define JOY_CENTER_LOW     430U
#define JOY_CENTER_HIGH    590U

/* ── Volatile event flags (set by callbacks, cleared in main loop) ─ */
static volatile uint8_t  g_active_fret          = FRET_NONE;
static volatile uint8_t  g_fret_changed         = 0U;
static volatile uint8_t  g_strum_pressed        = 0U;
static volatile uint8_t  g_strum_released       = 0U;
static volatile uint8_t  g_mute_pressed         = 0U;
static volatile uint8_t  g_joy_click_pressed    = 0U;
static volatile uint8_t  g_button_press_flags   = 0U;
static volatile uint8_t  g_button_release_flags = 0U;
static volatile uint32_t g_ms_tick              = 0UL;

/* Tracks whether the strum switch is currently held */
static uint8_t g_strum_latched = 0U;

/* Maps fret index (0-4) to synth chord index (0-4) */
static const uint8_t fret_chord_map[5] = { 0U, 1U, 2U, 3U, 4U };

/* ── Helpers ────────────────────────────────────────────────────── */

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

/* 1 ms system tick: increments g_ms_tick, debounces inputs,
 * advances the synth decay envelope.                              */
static void timer0_init(void)
{
    TCCR0A = (1U << WGM01);                      /* CTC mode              */
    OCR0A  = 249U;                               /* 16 MHz / 64 / 250 = 1 kHz */
    TCCR0B = (1U << CS01) | (1U << CS00);        /* prescaler = 64        */
    TIMSK0 = (1U << OCIE0A);                     /* enable compare-A IRQ  */
}

ISR(TIMER0_COMPA_vect)
{
    g_ms_tick++;
    inputs_tick();
    synth_decay_tick_1ms();
}

/* ── Input callbacks (called from inputs_tick / strum ISR) ─────── */

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

void on_strum_press(void)  { g_strum_pressed  = 1U; }
void on_strum_release(void){ g_strum_released = 1U; }
void on_mute_press(void)   { g_mute_pressed   = 1U; }
void on_joy_click_press(void) { g_joy_click_pressed = 1U; }

/* ── Main ───────────────────────────────────────────────────────── */
int main(void)
{
    uint32_t last_joy_ms      = 0UL;
    uint32_t last_vibrato_ms  = 0UL;
    uint16_t last_whammy_adc  = 0U;
    uint8_t  last_vibrato_pct = 0U;
    uint8_t  joy_x_armed      = 1U;
    uint8_t  joy_y_armed      = 1U;

    uart_init();
    printf("\r\nGuitar Hero Synth — boot\r\n");

    pwm_audio_init();
    synth_init();
    inputs_init();
    timer0_init();
    display_init();   /* draws full UI — must be before sei() */

    printf("System ready\r\n");
    printf("Joystick Y = button select, X = note scroll, click = commit\r\n");

    sei();

    for (;;) {

        /* ── Atomically snapshot and clear all flags ─────────────── */
        uint8_t  press_flags;
        uint8_t  release_flags;
        uint32_t now_ms;

        cli();
        press_flags              = g_button_press_flags;
        g_button_press_flags     = 0U;
        release_flags            = g_button_release_flags;
        g_button_release_flags   = 0U;
        now_ms                   = g_ms_tick;
        sei();

        /* ── Log button press/release over UART ──────────────────── */
        for (uint8_t i = 0U; i < 5U; i++) {
            if (press_flags & (1U << i)) {
                char nm[4];
                note_name_get(display_get_button_note(i), nm);
                printf("%s pressed -> %s\r\n", fret_name(i), nm);
            }
            if (release_flags & (1U << i)) {
                printf("%s released\r\n", fret_name(i));
            }
        }

        /* ── Fret change ─────────────────────────────────────────── */
        if (g_fret_changed) {
            uint8_t fret;
            cli(); fret = g_active_fret; g_fret_changed = 0U; sei();

            printf("Active fret: %s\r\n", fret_name(fret));

            /* If strum is held, change note immediately */
            if (g_strum_latched) {
                if (fret < 5U) {
                    synth_set_note(display_get_button_note(fret));
                } else {
                    synth_mute();
                }
            }
        }

        /* ── Strum press ─────────────────────────────────────────── */
        if (g_strum_pressed) {
            cli(); g_strum_pressed = 0U; sei();

            if (!g_strum_latched) {
                g_strum_latched = 1U;
                printf("STRUM pressed\r\n");

                if (g_active_fret < 5U) {
                    char nm[4];
                    uint8_t note = display_get_button_note(g_active_fret);
                    synth_set_note(note); 
                    note_name_get(note, nm);
                    printf("Playing %s on %s\r\n", nm, fret_name(g_active_fret));
                } else {
                    synth_mute();
                    printf("No fret held\r\n");
                }
            }
        }

        /* ── Strum release ───────────────────────────────────────── */
        if (g_strum_released) {
            cli(); g_strum_released = 0U; sei();
            g_strum_latched = 0U;
            synth_mute();
            printf("STRUM released\r\n");
        }

        /* ── Mute button ─────────────────────────────────────────── */
        if (g_mute_pressed) {
            cli(); g_mute_pressed = 0U; sei();
            synth_mute();
            synth_set_vibrato_depth(0U);
            synth_reset_vibrato();
            last_vibrato_pct = 0U;
            printf("MUTE pressed\r\n");
        }

        /* ── Joystick click → commit note assignment ─────────────── */
        if (g_joy_click_pressed) {
            cli(); g_joy_click_pressed = 0U; sei();

            display_commit_selected_note();   /* assigns note + redraws box */

            uint8_t btn  = display_get_selected_button();
            uint8_t note = display_get_button_note(btn);
            char nm[4];
            note_name_get(note, nm);
            printf("Saved %s to %s\r\n", nm, fret_name(btn));
        }

        /* ── Joystick ADC scan (every JOY_SCAN_MS) ───────────────── */
        if ((uint32_t)(now_ms - last_joy_ms) >= JOY_SCAN_MS) {
            last_joy_ms = now_ms;
            inputs_adc_scan();

            last_whammy_adc  = inputs_whammy;
            uint16_t joy_y   = inputs_joy_y;
            uint16_t joy_x   = inputs_joy_x;

            /* Y axis → move button selection up/down */
            if (joy_y_armed) {
                if (joy_y <= JOY_LOW_THRESHOLD) {
                    display_move_button_selection(-1);
                    joy_y_armed = 0U;
                } else if (joy_y >= JOY_HIGH_THRESHOLD) {
                    display_move_button_selection(+1);
                    joy_y_armed = 0U;
                }
            } else if (joy_y >= JOY_CENTER_LOW && joy_y <= JOY_CENTER_HIGH) {
                joy_y_armed = 1U;   /* re-arm once joystick returns to centre */
            }

            /* X axis → scroll note wheel */
            if (joy_x_armed) {
                if (joy_x <= JOY_LOW_THRESHOLD) {
                    display_move_note_selection(-1, now_ms);
                    joy_x_armed = 0U;
                } else if (joy_x >= JOY_HIGH_THRESHOLD) {
                    display_move_note_selection(+1, now_ms);
                    joy_x_armed = 0U;
                }
            } else if (joy_x >= JOY_CENTER_LOW && joy_x <= JOY_CENTER_HIGH) {
                joy_x_armed = 1U;
            }

            /* Whammy → vibrato depth
             * Below ~60 % of travel: no vibrato
             * 60–92 %: ramp vibrato 0→4
             * Above 92 %: max vibrato (4)                           */
            uint8_t vibrato_pct;
            if (last_whammy_adc < 614U) {
                vibrato_pct = 0U;
            } else if (last_whammy_adc >= 941U) {
                vibrato_pct = 4U;
            } else {
                uint32_t num = (uint32_t)(last_whammy_adc - 614U) * 4UL;
                vibrato_pct  = (uint8_t)(num / (uint32_t)(941U - 614U));
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

        /* ── Vibrato LFO tick (every 10 ms while active) ─────────── */
        if (synth_is_active() && last_vibrato_pct > 0U &&
            (uint32_t)(now_ms - last_vibrato_ms) >= 10UL) {
            last_vibrato_ms = now_ms;
            synth_vibrato_tick();
        }
    }
}