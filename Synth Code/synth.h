/*
 * synth.h — DDS wavetable engine interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 */

#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

void synth_init(void);
void synth_set_note(uint8_t note_idx);
void synth_mute(void);
void synth_set_bend(int8_t semitones);

#endif /* SYNTH_H */
