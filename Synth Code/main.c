/*
 * main.c — System init, main loop, UART debug
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint.h>

#include "spi_dac.h"
#include "synth.h"
#include "inputs.h"
#include "notes.h"
#include "envelope.h"
#include "display.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define BAUD      9600UL
#define UBRR_VAL  ((F_CPU / (16UL * BAUD)) - 1UL)

static const uint8_t fret_note_map[5] = {
    GH_NOTE_GREEN,
    GH_NOTE_RED,
    GH_NOTE_YELLOW,
    GH_NOTE_BLUE,
    GH_NOTE_ORANGE,
};

static volatile uint8_t  g_strum_flag   = 0U;
static volatile uint8_t  g_mute_flag    = 0U;
static volatile uint8_t  g_fret_changed = 0U;
static volatile uint8_t  g_active_fret  = FRET_NONE;
static volatile uint32_t g_ms_tick      = 0UL;

static uint8_t  g_note_idx           = GUITAR_NOTE_E3;
static uint8_t  g_is_muted           = 0U;
static uint32_t g_strum_visual_until = 0UL;

/* Configures Timer0 for a 1 ms system tick interrupt. */
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
}

/* Configures USART0 for 9600 baud TX and RX. */
static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UBRR_VAL & 0xFFU);
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

/* Transmits one character over UART. */
static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = (uint8_t)c;
}

/* Transmits a null-terminated string over UART. */
static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* Transmits a PROGMEM string over UART. */
static void uart_puts_P(const char *s)
{
    char c;
    while ((c = (char)pgm_read_byte(s++))) uart_putc(c);
}

/* Prints a uint16_t as decimal over UART. */
static void uart_put_u16(uint16_t n)
{
    char    buf[5];
    uint8_t i = 0U;
    if (n == 0U) { uart_putc('0'); return; }
    while (n > 0U && i < 5U) {
        buf[i++] = (char)('0' + (uint8_t)(n % 10U));
        n /= 10U;
    }
    while (i > 0U) uart_putc(buf[--i]);
}

/* Returns the next received byte, or -1 if none available. */
static int16_t uart_getc_nowait(void)
{
    if (!(UCSR0A & (1 << RXC0))) return -1;
    return (int16_t)(uint8_t)UDR0;
}

/* Streams the ADSR gain curve to UART for plotting. */
static void run_envelope_debug(void)
{
    uart_puts_P(PSTR("\r\n=== ENVELOPE DEBUG ===\r\n"));
    uart_puts_P(PSTR("gain 0-65535, one value per ~100 samples (3.2 ms)\r\n"));
    uart_puts_P(PSTR("Auto-releases after 360 ms; streams until IDLE.\r\n\r\n"));

    envelope_trigger();

    uint32_t t_release  = envelope_ticks + 11160UL;
    uint32_t next_print = envelope_ticks + 100UL;
    uint8_t  released   = 0U;
    uint8_t  was_active = 0U;

    for (;;) {
        uint32_t now = envelope_ticks;

        if (!released && now >= t_release) {
            envelope_release();
            released = 1U;
        }

        if (now >= next_print) {
            next_print += 100UL;

            uint16_t gain = envelope_get();
            uart_put_u16(gain);
            uart_putc('\n');

            if (gain > 0U)                            was_active = 1U;
            if (released && was_active && gain == 0U) break;
        }
    }

    uart_puts_P(PSTR("\r\n=== END ENVELOPE DEBUG ===\r\n"));
}

/* Strum interrupt callback — sets the strum flag. */
void on_strum(void)
{
    g_strum_flag = 1U;
}

/* Mute button callback — sets the mute flag. */
void on_mute(void)
{
    g_mute_flag = 1U;
}

/* Fret change callback — stores the new active fret. */
void on_pitch_change(uint8_t fret)
{
    g_active_fret  = fret;
    g_fret_changed = 1U;
}

/* Busy-waits for ms milliseconds; the Timer1 ISR drives audio throughout. */
static void test_wait_ms(uint16_t ms)
{
    uint32_t end = g_ms_tick + (uint32_t)ms;
    while (g_ms_tick < end);
}

/* Boot test: cycles E2–E6 every 500 ms and reports note names over UART. */
static void run_test_mode(void)
{
    uart_puts_P(PSTR("\r\n=== ESE3500 BOOT TEST (hold mute at reset) ===\r\n"));
    uart_puts_P(PSTR("Cycling E2 -> E6, 500ms per note\r\n\r\n"));

    for (uint8_t idx = GUITAR_NOTE_E2; idx <= GUITAR_NOTE_E6; idx++) {
        synth_set_note(idx);
        g_note_idx = idx;

        inputs_adc_scan();

        char name[4];
        note_name_get(idx, name);

        uart_puts_P(PSTR("Note: "));
        uart_puts(name);
        uart_puts_P(PSTR("\t\tWhammy: "));
        uart_put_u16(inputs_whammy);
        uart_puts_P(PSTR("\r\n"));

        test_wait_ms(500U);
    }

    synth_mute();
    g_note_idx = GUITAR_NOTE_E3;
    uart_puts_P(PSTR("\r\n=== TEST COMPLETE -- entering normal mode ===\r\n\r\n"));
}

int main(void)
{
    DDRB  &= ~(1 << PB0);
    PORTB |=  (1 << PB0);
    for (volatile uint16_t i = 0U; i < 1000U; i++);
    uint8_t boot_test = !(PINB & (1 << PB0));

    pwm_audio_init();
    synth_init();
    inputs_init();
    display_init();
    envelope_init();
    timer0_init();
    uart_init();

    sei();

    if (boot_test) {
        run_test_mode();
    }

    uint32_t last_whammy_ms  = 0UL;
    uint32_t last_display_ms = 0UL;

    for (;;) {

        if (g_strum_flag) {
            g_strum_flag = 0U;
            envelope_trigger();
            g_strum_visual_until = g_ms_tick + 100UL;
        }

        if (g_mute_flag) {
            g_mute_flag = 0U;
            g_is_muted  = 1U;
            envelope_release();
            synth_mute();
        }

        if (g_fret_changed) {
            uint8_t fret   = g_active_fret;
            g_fret_changed = 0U;
            if (fret < 5U) {
                g_is_muted = 0U;
                g_note_idx = fret_note_map[fret];
                synth_set_note(g_note_idx);
            } else {
                synth_mute();
            }
        }

        if (g_ms_tick - last_whammy_ms >= 20UL) {
            last_whammy_ms = g_ms_tick;
            inputs_adc_scan();
            int16_t raw  = (int16_t)inputs_whammy - 512;
            int8_t  bend = (int8_t)(raw * 12 / 512);
            synth_set_bend(bend);
        }

        if (g_ms_tick - last_display_ms >= 50UL) {
            last_display_ms = g_ms_tick;
            bool strumming  = (g_ms_tick < g_strum_visual_until);
            display_update_ui(g_note_idx,
                              (uint8_t)(inputs_whammy >> 2),
                              (bool)g_is_muted,
                              strumming);
        }

        {
            int16_t ch = uart_getc_nowait();
            if (ch == (int16_t)'e') {
                run_envelope_debug();
            }
        }

    }

    return 0;
}
