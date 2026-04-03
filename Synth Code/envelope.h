/*
 * envelope.h — ADSR envelope state machine interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 */

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <stdint.h>

extern volatile uint32_t envelope_ticks;

void envelope_init(void);

void envelope_trigger(void);
void envelope_release(void);

void envelope_tick(void);

uint16_t envelope_get(void);

#endif /* ENVELOPE_H */
