/*
 * main.c — Display-driven power-chord synth.
 *
 * The display stores one ROOT note per on-screen button.
 * A strum plays a sustained/fading POWER CHORD built from that root.
 *
 * Important:
 *   Display button order is: RED, ORANGE, YELLOW, GREEN, BLUE
 *   Fret input order is:     GREEN, RED, YELLOW, BLUE, ORANGE
 * So we map between them here.
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

/* Joystick ADC thresholds */
#define JOY_SCAN_MS         20UL
#define JOY_LOW_THRESHOLD  120U
#define JOY_HIGH_THRESHOLD 900U
#define JOY_CENTER_LOW     430U
#define JOY_CENTER_HIGH    590U

/* Volatile event flags (set by callbacks, cleared in main loop) */
static volatile uint8_t  g_active_fret          = FRET_NONE;
static volatile uint8_t  g_fret_changed         = 0U;
static volatile uint8_t  g_strum_pressed        = 0U;
static volatile uint8_t  g_strum_released       = 0U;
static volatile uint8_t  g_mute_pressed         = 0U;
static volatile uint8_t  g_joy_click_pressed    = 0U;
static volatile uint8_t  g_button_press_flags   = 0U;
static volatile uint8_t  g_button_release_flags = 0U;
static volatile uint32_t g_ms_tick              = 0UL;

static uint8_t g_strum_latched = 0U;

/*
 * Fret input order:     GREEN, RED, YELLOW, BLUE, ORANGE
 * Display button order: RED,   ORANGE, YELLOW, GREEN, BLUE
 */
static const uint8_t fret_to_display_idx[5] = {
    3U, /* GREEN  -> GREEN  */
    0U, /* RED    -> RED    */
    2U, /* YELLOW -> YELLOW */
    4U, /* BLUE   -> BLUE   */
    1U  /* ORANGE -> ORANGE */
};

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

static const char *display_button_name(uint8_t btn)
{
    switch (btn) {
    case 0U: return "RED";
    case 1U: return "ORANGE";
    case 2U: return "YELLOW";
    case 3U: return "GREEN";
    case 4U: return "BLUE";
    default: return "NONE";
    }
}

static uint8_t root_note_for_fret(uint8_t fret)
{
    if (fret >= 5U) {
        return GUITAR_NOTE_C3;
    }
    return display_get_button_note(fret_to_display_idx[fret]);
}

/* 1 ms system tick: increments g_ms_tick, debounces inputs,
 * and advances the synth decay envelope. */
static void timer0_init(void)
{
    TCCR0A = (1U << WGM01);
    OCR0A  = 249U;                        /* 16 MHz / 64 / 250 = 1 kHz */
    TCCR0B = (1U << CS01) | (1U << CS00);
    TIMSK0 = (1U << OCIE0A);
}

ISR(TIMER0_COMPA_vect)
{
    g_ms_tick++;
    inputs_tick();
    synth_decay_tick_1ms();
}

/* Input callbacks */
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

void on_strum_press(void)     { g_strum_pressed     = 1U; }
void on_strum_release(void)   { g_strum_released    = 1U; }
void on_mute_press(void)      { g_mute_pressed      = 1U; }
void on_joy_click_press(void) { g_joy_click_pressed = 1U; }

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
    display_init();

    printf("System ready\r\n");
    printf("Joystick Y = button select, X = note scroll, click = commit\r\n");
    printf("Committed note becomes the new POWER-CHORD root for that button.\r\n");

    sei();

    for (;;) {
        uint8_t  press_flags;
        uint8_t  release_flags;
        uint32_t now_ms;

        cli();
        press_flags            = g_button_press_flags;
        g_button_press_flags   = 0U;
        release_flags          = g_button_release_flags;
        g_button_release_flags = 0U;
        now_ms                 = g_ms_tick;
        sei();

        for (uint8_t i = 0U; i < 5U; i++) {
            if (press_flags & (1U << i)) {
                char nm[4];
                note_name_get(root_note_for_fret(i), nm);
                printf("%s pressed -> root %s\r\n", fret_name(i), nm);
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

        if (g_strum_pressed) {
            cli();
            g_strum_pressed = 0U;
            sei();

            if (!g_strum_latched) {
                g_strum_latched = 1U;
                printf("STRUM pressed\r\n");

                if (g_active_fret < 5U) {
                    char nm[4];
                    uint8_t root = root_note_for_fret(g_active_fret);
                    synth_set_note(root);
                    note_name_get(root, nm);
                    printf("Playing power chord rooted at %s on %s\r\n",
                           nm, fret_name(g_active_fret));
                } else {
                    printf("No fret held, nothing triggered\r\n");
                }
            }
        }

        /* Re-arm next strum. Do not stop the current fade here. */
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

        if (g_joy_click_pressed) {
            cli();
            g_joy_click_pressed = 0U;
            sei();

            display_commit_selected_note();

            {
                uint8_t btn  = display_get_selected_button();
                uint8_t root = display_get_button_note(btn);
                char nm[4];
                note_name_get(root, nm);
                printf("Saved root %s to %s (power chord)\r\n",
                       nm, display_button_name(btn));
            }
        }

        if ((uint32_t)(now_ms - last_joy_ms) >= JOY_SCAN_MS) {
            last_joy_ms = now_ms;
            inputs_adc_scan();

            last_whammy_adc = inputs_whammy;
            {
                uint16_t joy_y = inputs_joy_y;
                uint16_t joy_x = inputs_joy_x;

                if (joy_y_armed) {
                    if (joy_y <= JOY_LOW_THRESHOLD) {
                        display_move_button_selection(-1);
                        joy_y_armed = 0U;
                    } else if (joy_y >= JOY_HIGH_THRESHOLD) {
                        display_move_button_selection(+1);
                        joy_y_armed = 0U;
                    }
                } else if (joy_y >= JOY_CENTER_LOW && joy_y <= JOY_CENTER_HIGH) {
                    joy_y_armed = 1U;
                }

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
            }

            /* Whammy -> vibrato depth */
            {
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
        }

        if (synth_is_active() && last_vibrato_pct > 0U &&
            (uint32_t)(now_ms - last_vibrato_ms) >= 10UL) {
            last_vibrato_ms = now_ms;
            synth_vibrato_tick();
        }
    }
}
