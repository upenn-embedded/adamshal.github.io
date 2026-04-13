/*
 * spi_dac.h — 8-bit PWM audio output driver interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * Audio output method: Timer2 Fast PWM on OC2A (PB3)
 *   PWM frequency = F_CPU / 256 = 16 MHz / 256 = 62.5 kHz
 *   Sample rate driven by Timer1 ISR @ 31250 Hz (2 PWM cycles per sample)
 *   Output pin PB3 (OC2A) → RC low-pass filter → Adafruit 5647 class-D amp
 *                                               → Adafruit 4445 3W 4Ω speaker
 *
 * PWM protocol:
 *   OCR2A sets the duty cycle (0 = 0 V, 255 = VCC, 128 = midscale/silence).
 *   A single OCR2A register write is the entire "transfer" — no bus protocol.
 *   The RC filter on the output smooths the 62.5 kHz PWM into an analog voltage.
 */

#ifndef SPI_DAC_H
#define SPI_DAC_H

#include <stdint.h>

/* Configures Timer2 Fast PWM on OC2A (PB3) at 62.5 kHz. */
void pwm_audio_init(void);

/* Writes the top 8 bits of a 16-bit sample to OCR2A as the PWM duty cycle. */
void pwm_audio_write(uint16_t sample);

#endif /* SPI_DAC_H */
