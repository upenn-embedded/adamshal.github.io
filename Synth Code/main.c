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

/*
 * Buzzer-debug note map chosen to be clearly different on a passive piezo.
 * You can swap these back later if you want the original guitar-string map.
 */
static const uint8_t fret_note_map[5] = {
    GUITAR_NOTE_C3,   /* GREEN  */
    GUITAR_NOTE_E3,   /* RED    */
    GUITAR_NOTE_G3,   /* YELLOW */
    GUITAR_NOTE_B3,   /* BLUE   */
    GUITAR_NOTE_E4,   /* ORANGE */
};

static volatile uint8_t g_active_fret          = FRET_NONE;
static volatile uint8_t g_fret_changed         = 0U;
static volatile uint8_t g_strum_pressed        = 0U;
static volatile uint8_t g_strum_released       = 0U;
static volatile uint8_t g_button_press_flags   = 0U;
static volatile uint8_t g_button_release_flags = 0U;

static uint8_t g_strum_down       = 0U;
static uint8_t g_display_note_idx = GUITAR_NOTE_C3;

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

static void note_name_local(uint8_t idx, char out[4])
{
    static const char note_names[NUM_GUITAR_NOTES][4] = {
        "E2",  "F2",  "F#2", "G2",  "G#2",
        "A2",  "A#2", "B2",  "C3",  "C#3",
        "D3",  "D#3", "E3",  "F3",  "F#3",
        "G3",  "G#3", "A3",  "A#3", "B3",
        "C4",  "C#4", "D4",  "D#4", "E4"
    };

    if (idx < NUM_GUITAR_NOTES) {
        out[0] = note_names[idx][0];
        out[1] = note_names[idx][1];
        out[2] = note_names[idx][2];
        out[3] = '\0';
    } else {
        out[0] = '?';
        out[1] = '?';
        out[2] = '?';
        out[3] = '\0';
    }
}

static const char *note_name_for_fret(uint8_t fret)
{
    static char buf[4];

    if (fret < 5U) {
        note_name_local(fret_note_map[fret], buf);
        return buf;
    }

    return "NONE";
}

static void display_refresh_now(void)
{
    uint8_t muted = 1U;
    uint8_t strumming = 0U;

    if ((g_active_fret < 5U) && g_strum_down) {
        muted = 0U;
        strumming = 1U;
    }

    display_update_ui(g_display_note_idx, 0U, muted, strumming);
}

/* 1 ms system tick for debounce. */
static void timer0_init(void)
{
    TCCR0A = (1 << WGM01);
    OCR0A  = 249U;
    TCCR0B = (1 << CS01) | (1 << CS00);
    TIMSK0 = (1 << OCIE0A);
}

ISR(TIMER0_COMPA_vect)
{
    inputs_tick();
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

int main(void)
{
    uart_init();
    printf("\r\nGuitar Hero input debug start\r\n");
    printf("Watching GREEN, RED, YELLOW, BLUE, ORANGE, and STRUM\r\n");

    pwm_audio_init();
    synth_init();
    inputs_init();
    timer0_init();

    display_init();
    g_display_note_idx = fret_note_map[0];
    display_refresh_now();

    printf("System initialized\r\n");
    printf("PB2 buzzer mode: silent until strum, then play selected note\r\n");

    sei();

    for (;;) {
        uint8_t press_flags;
        uint8_t release_flags;

        cli();
        press_flags = g_button_press_flags;
        g_button_press_flags = 0U;
        release_flags = g_button_release_flags;
        g_button_release_flags = 0U;
        sei();

        for (uint8_t i = 0U; i < 5U; i++) {
            if (press_flags & (1U << i)) {
                printf("%s pressed -> %s\r\n", fret_name(i), note_name_for_fret(i));
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

            if (fret < 5U) {
                g_display_note_idx = fret_note_map[fret];
            }
            display_refresh_now();

            if (g_strum_down) {
                if (fret < 5U) {
                    synth_set_note(fret_note_map[fret]);
                    printf("Now playing %s\r\n", note_name_for_fret(fret));
                } else {
                    synth_mute();
                    printf("No fret held, output muted\r\n");
                }
                display_refresh_now();
            }
        }

        if (g_strum_pressed) {
            cli();
            g_strum_pressed = 0U;
            sei();

            g_strum_down = 1U;
            printf("STRUM pressed\r\n");

            if (g_active_fret < 5U) {
                g_display_note_idx = fret_note_map[g_active_fret];
                synth_set_note(fret_note_map[g_active_fret]);
                printf("Playing %s\r\n", note_name_for_fret(g_active_fret));
            } else {
                synth_mute();
                printf("No fret held, output muted\r\n");
            }

            display_refresh_now();
        }

        if (g_strum_released) {
            cli();
            g_strum_released = 0U;
            sei();

            g_strum_down = 0U;
            printf("STRUM released\r\n");
            synth_mute();
            display_refresh_now();
        }
    }
}