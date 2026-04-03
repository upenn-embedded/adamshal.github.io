/*
 * notes.h — Guitar note frequency table (E2–E6, equal temperament)
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Covers all 49 chromatic semitones from E2 (82.41 Hz) to E6 (1318.51 Hz),
 *
 * DDS phase increment formula (Fs = 31250 Hz):
 *   phi_inc = round(freq_hz × 2^32 / 31250)
 *           = round(freq_hz × 137438.953088)
 */

#ifndef NOTES_H
#define NOTES_H

#include <stdint.h>
#include <avr/pgmspace.h>

/* Total number of notes in the table (E2 = 0, E6 = 48) */
#define NUM_GUITAR_NOTES  49U

/* Chromatic Index Macros -- Done by Claude*/
#define GUITAR_NOTE_E2   0U
#define GUITAR_NOTE_F2   1U
#define GUITAR_NOTE_FS2  2U   /* F#2 */
#define GUITAR_NOTE_G2   3U
#define GUITAR_NOTE_GS2  4U   /* G#2 / Ab2 */
#define GUITAR_NOTE_A2   5U
#define GUITAR_NOTE_AS2  6U   /* A#2 / Bb2 */
#define GUITAR_NOTE_B2   7U
#define GUITAR_NOTE_C3   8U
#define GUITAR_NOTE_CS3  9U   /* C#3 / Db3 */
#define GUITAR_NOTE_D3   10U
#define GUITAR_NOTE_DS3  11U  /* D#3 / Eb3 */
#define GUITAR_NOTE_E3   12U
#define GUITAR_NOTE_F3   13U
#define GUITAR_NOTE_FS3  14U  /* F#3 */
#define GUITAR_NOTE_G3   15U
#define GUITAR_NOTE_GS3  16U  /* G#3 / Ab3 */
#define GUITAR_NOTE_A3   17U
#define GUITAR_NOTE_AS3  18U  /* A#3 / Bb3 */
#define GUITAR_NOTE_B3   19U
#define GUITAR_NOTE_C4   20U  /* Middle C */
#define GUITAR_NOTE_CS4  21U  /* C#4 / Db4 */
#define GUITAR_NOTE_D4   22U
#define GUITAR_NOTE_DS4  23U  /* D#4 / Eb4 */
#define GUITAR_NOTE_E4   24U
#define GUITAR_NOTE_F4   25U
#define GUITAR_NOTE_FS4  26U  /* F#4 */
#define GUITAR_NOTE_G4   27U
#define GUITAR_NOTE_GS4  28U  /* G#4 / Ab4 */
#define GUITAR_NOTE_A4   29U  /* Concert A, 440 Hz */
#define GUITAR_NOTE_AS4  30U  /* A#4 / Bb4 */
#define GUITAR_NOTE_B4   31U
#define GUITAR_NOTE_C5   32U
#define GUITAR_NOTE_CS5  33U  /* C#5 / Db5 */
#define GUITAR_NOTE_D5   34U
#define GUITAR_NOTE_DS5  35U  /* D#5 / Eb5 */
#define GUITAR_NOTE_E5   36U
#define GUITAR_NOTE_F5   37U
#define GUITAR_NOTE_FS5  38U  /* F#5 */
#define GUITAR_NOTE_G5   39U
#define GUITAR_NOTE_GS5  40U  /* G#5 / Ab5 */
#define GUITAR_NOTE_A5   41U
#define GUITAR_NOTE_AS5  42U  /* A#5 / Bb5 */
#define GUITAR_NOTE_B5   43U
#define GUITAR_NOTE_C6   44U
#define GUITAR_NOTE_CS6  45U  /* C#6 / Db6 */
#define GUITAR_NOTE_D6   46U
#define GUITAR_NOTE_DS6  47U  /* D#6 / Eb6 */
#define GUITAR_NOTE_E6   48U

/* Button Mapping ──────────
 * Standard guitar open strings, high-to-low:
 *   Green  (PD2) = high E4 string  → E4
 *   Red    (PD3) = B3 string       → B3
 *   Yellow (PD4) = G3 string       → G3
 *   Blue   (PD5) = D3 string       → D3
 *   Orange (PD6) = A2 string       → A2
 * Used in inputs.c button-to-note dispatch.               */
#define GH_NOTE_GREEN   GUITAR_NOTE_E4
#define GH_NOTE_RED     GUITAR_NOTE_B3
#define GH_NOTE_YELLOW  GUITAR_NOTE_G3
#define GH_NOTE_BLUE    GUITAR_NOTE_D3
#define GH_NOTE_ORANGE  GUITAR_NOTE_A2

/*
 * note_phase_inc[i] — 32-bit DDS phase increment for note index i.
 * Access: pgm_read_dword(&note_phase_inc[i])
 */
extern const uint32_t note_phase_inc[NUM_GUITAR_NOTES] PROGMEM;

/*
 * note_names[i] — null-terminated note name string, max 4 chars
 * including
 * Access: strcpy_P(local_buf, note_names[i])  (buf ≥ 4 bytes)
 */
extern const char note_names[NUM_GUITAR_NOTES][4] PROGMEM;

#endif /* NOTES_H */
