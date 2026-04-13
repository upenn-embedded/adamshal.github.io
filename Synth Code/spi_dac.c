/*
 * spi_dac.c — 8-bit PWM audio output via Timer2/OC2A
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * PWM audio method (replaces SPI DAC):
 *   Timer2 runs in Fast PWM mode (8-bit) with no prescaler.
 *   PWM frequency = 16 MHz / 256 = 62.5 kHz — well above audible range.
 *   The Timer1 audio ISR (31250 Hz) updates OCR2A each sample period,
 *   so every audio sample spans exactly 2 PWM cycles.
 *
 *   OC2A is on PB3.  Hardware path:
 *     PB3 → RC low-pass filter (e.g. 1 kΩ + 10 nF, fc ≈ 16 kHz)
 *         → Adafruit 5647 mono class-D amp input
 *         → Adafruit 4445 3W 4Ω speaker / SparkFun PRT-08032 3.5mm jack
 *
 *   Midscale silence = OCR2A 128 (50 % duty cycle → ~VCC/2 after filter).
 *   Full-scale audio = OCR2A swings 0–255 around that midpoint.
 */

#include <avr/io.h>
#include "spi_dac.h"

/* Configures Timer2 Fast PWM on OC2A (PB3) at 62.5 kHz. */
void pwm_audio_init(void)
{
    DDRB  |= (1 << PB3);  /* PB3/OC2A as output */
    OCR2A  = 128U;         /* midscale at startup — avoids click on enable */

    /* Fast PWM (WGM2[2:0] = 011), clear OC2A on compare match (COM2A[1:0] = 10) */
    TCCR2A = (1 << COM2A1) | (1 << WGM21) | (1 << WGM20);
    /* No prescaler (CS2[2:0] = 001) → fPWM = 16 MHz / 256 = 62.5 kHz */
    TCCR2B = (1 << CS20);
}

/* Writes the top 8 bits of a 16-bit sample to OCR2A (single-cycle register write). */
void pwm_audio_write(uint16_t sample)
{
    OCR2A = (uint8_t)(sample >> 8);
}
