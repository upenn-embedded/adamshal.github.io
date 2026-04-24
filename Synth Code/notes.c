/*
 * notes.c ? DDS phase-increment and note-name tables (PROGMEM)
 * ESE3500 Final Project ? Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include <avr/pgmspace.h>
#include "notes.h"

const uint32_t note_phase_inc[NUM_GUITAR_NOTES] PROGMEM = {
    /*  E2  */  11325922UL,
    /*  F2  */  11999408UL,
    /* F#2  */  12712943UL,
    /*  G2  */  13468860UL,
    /* G#2  */  14269763UL,
    /*  A2  */  15118285UL,
    /* A#2  */  16017281UL,
    /*  B2  */  16969700UL,
    /*  C3  */  17978786UL,
    /* C#3  */  19047862UL,
    /*  D3  */  20180518UL,
    /* D#3  */  21380483UL,
    /*  E3  */  22651847UL,
    /*  F3  */  23998808UL,
    /* F#3  */  25425823UL,
    /*  G3  */  26937721UL,
    /* G#3  */  28539521UL,
    /*  A3  */  30236570UL,
    /* A#3  */  32034531UL,
    /*  B3  */  33939425UL,
    /*  C4  */  36047554UL,
    /* C#4  */  38095694UL,
    /*  D4  */  40361186UL,
    /* D#4  */  42770974UL,
    /*  E4  */  45303666UL,
    /*  F4  */  47997572UL,
    /* F#4  */  50901646UL,
    /*  G4  */  53925445UL,
    /* G#4  */  57079043UL,
    /*  A4  */  60473139UL,
    /* A#4  */  64069064UL,
    /*  B4  */  67878803UL,
    /*  C5  */  71915107UL,
    /* C#5  */  76191400UL,
    /*  D5  */  80751878UL,
    /* D#5  */  85581976UL,
    /*  E5  */  90607344UL,
    /*  F5  */  95995156UL,
    /* F#5  */ 101763415UL,
    /*  G5  */ 107761940UL,
    /* G#5  */ 114158116UL,
    /*  A5  */ 120946279UL,
    /* A#5  */ 128138142UL,
    /*  B5  */ 135747656UL,
    /*  C6  */ 143890192UL,
    /* C#6  */ 152483001UL,
    /*  D6  */ 161543833UL,
    /* D#6  */ 171024424UL,
    /*  E6  */ 181254898UL,
};

void note_name_get(uint8_t idx, char buf[4])
{
    uint8_t i = 0;
    char    c;
    while ((c = (char)pgm_read_byte(&note_names[idx][i])) != '\0') {
        buf[i] = c;
        i++;
    }
    buf[i] = '\0';
}

const char note_names[NUM_GUITAR_NOTES][4] PROGMEM = {
    "E2",  "F2",  "F#2", "G2",  "G#2",
    "A2",  "A#2", "B2",  "C3",  "C#3",
    "D3",  "D#3", "E3",  "F3",  "F#3",
    "G3",  "G#3", "A3",  "A#3", "B3",
    "C4",  "C#4", "D4",  "D#4", "E4",
    "F4",  "F#4", "G4",  "G#4", "A4",
    "A#4", "B4",  "C5",  "C#5", "D5",
    "D#5", "E5",  "F5",  "F#5", "G5",
    "G#5", "A5",  "A#5", "B5",  "C6",
    "C#6", "D6",  "D#6", "E6",
};
