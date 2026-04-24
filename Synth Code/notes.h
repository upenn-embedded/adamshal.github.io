/*
 * notes.h ? Guitar note index macros and PROGMEM table declarations
 * ESE3500 Final Project ? Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#ifndef NOTES_H
#define NOTES_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define NUM_GUITAR_NOTES  49U

/* Chromatic note indices E2 (0) through E6 (48). */
#define GUITAR_NOTE_E2   0U
#define GUITAR_NOTE_F2   1U
#define GUITAR_NOTE_FS2  2U
#define GUITAR_NOTE_G2   3U
#define GUITAR_NOTE_GS2  4U
#define GUITAR_NOTE_A2   5U
#define GUITAR_NOTE_AS2  6U
#define GUITAR_NOTE_B2   7U
#define GUITAR_NOTE_C3   8U
#define GUITAR_NOTE_CS3  9U
#define GUITAR_NOTE_D3   10U
#define GUITAR_NOTE_DS3  11U
#define GUITAR_NOTE_E3   12U
#define GUITAR_NOTE_F3   13U
#define GUITAR_NOTE_FS3  14U
#define GUITAR_NOTE_G3   15U
#define GUITAR_NOTE_GS3  16U
#define GUITAR_NOTE_A3   17U
#define GUITAR_NOTE_AS3  18U
#define GUITAR_NOTE_B3   19U
#define GUITAR_NOTE_C4   20U
#define GUITAR_NOTE_CS4  21U
#define GUITAR_NOTE_D4   22U
#define GUITAR_NOTE_DS4  23U
#define GUITAR_NOTE_E4   24U
#define GUITAR_NOTE_F4   25U
#define GUITAR_NOTE_FS4  26U
#define GUITAR_NOTE_G4   27U
#define GUITAR_NOTE_GS4  28U
#define GUITAR_NOTE_A4   29U
#define GUITAR_NOTE_AS4  30U
#define GUITAR_NOTE_B4   31U
#define GUITAR_NOTE_C5   32U
#define GUITAR_NOTE_CS5  33U
#define GUITAR_NOTE_D5   34U
#define GUITAR_NOTE_DS5  35U
#define GUITAR_NOTE_E5   36U
#define GUITAR_NOTE_F5   37U
#define GUITAR_NOTE_FS5  38U
#define GUITAR_NOTE_G5   39U
#define GUITAR_NOTE_GS5  40U
#define GUITAR_NOTE_A5   41U
#define GUITAR_NOTE_AS5  42U
#define GUITAR_NOTE_B5   43U
#define GUITAR_NOTE_C6   44U
#define GUITAR_NOTE_CS6  45U
#define GUITAR_NOTE_D6   46U
#define GUITAR_NOTE_DS6  47U
#define GUITAR_NOTE_E6   48U

/* Guitar Hero button-to-note mapping (open strings, high to low). */
#define GH_NOTE_GREEN   GUITAR_NOTE_E4
#define GH_NOTE_RED     GUITAR_NOTE_B3
#define GH_NOTE_YELLOW  GUITAR_NOTE_G3
#define GH_NOTE_BLUE    GUITAR_NOTE_D3
#define GH_NOTE_ORANGE  GUITAR_NOTE_A2

/* DDS phase increment for each note, stored in flash. */
extern const uint32_t note_phase_inc[NUM_GUITAR_NOTES] PROGMEM;

/* Null-terminated note name string for each index, stored in flash. */
extern const char note_names[NUM_GUITAR_NOTES][4] PROGMEM;

/* Copies the note name at idx into buf (buf must be >= 4 bytes).
 * Use this instead of strcpy_P ? XC8 does not provide strcpy_P. */
void note_name_get(uint8_t idx, char buf[4]);

#endif /* NOTES_H */
