#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

void synth_init(void);
void synth_set_chord(uint8_t chord_idx);
void synth_mute(void);

void synth_set_vibrato_depth(uint8_t depth_percent);
void synth_reset_vibrato(void);
void synth_vibrato_tick(void);

void synth_decay_tick_1ms(void);
uint8_t synth_is_active(void);
void synth_set_note(uint8_t note_idx);

#endif /* SYNTH_H */