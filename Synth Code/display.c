/*
 * display.c — Adafruit 1.8" ST7735R 128×160 color TFT driver (software SPI)
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * ── SPI protocol ────────────────────────────────────────────────────────────
 * Software bit-bang SPI, CPOL=0 CPHA=0 (Mode 0), MSB first.
 * Each transaction: assert CS low → send bytes → deassert CS high.
 * DC low  = command byte.
 * DC high = data byte(s).
 * 16-bit colour pixels are sent as two bytes, high byte first (RGB565 big-endian).
 * The display is write-only; no MISO line is used.
 *
 * Hardware SPI0 cannot be used because PB3 is claimed by Timer2 OC2A (PWM audio).
 *
 * ── Pin wiring (ATmega328PB → Adafruit #358 breakout) ───────────────────────
 *   PB4 → SDA  (MOSI)
 *   PB5 → SCK  (SCLK)
 *   PB2 → TCS  (CS,  active-low)
 *   PC3 → A0   (DC,  low=command high=data)
 *   PC4 → RST  (RST, active-low)
 *
 * ── Screen layout  128 × 160 px, portrait ───────────────────────────────────
 *   y   0..  7  top margin
 *   y   8.. 71  note name — 4× scaled font, centred (64 px tall)
 *   y  72.. 79  gap
 *   y  80.. 99  whammy bar — 16 green/grey segments (20 px tall)
 *   y 100..107  gap
 *   y 108..119  status line — MUTED (red) or STRUM (yellow), 1× font
 *   y 120..159  bottom margin
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include "display.h"
#include "notes.h"

/* ── Pin aliases ─────────────────────────────────────────────────────────── */
#define TFT_MOSI   PB4
#define TFT_SCK    PB5
#define TFT_CS     PB2
#define TFT_DC     PC3
#define TFT_RST    PC4

/* ── RGB565 colour constants ─────────────────────────────────────────────── */
#define COL_BLACK   0x0000U
#define COL_WHITE   0xFFFFU
#define COL_GREEN   0x07E0U   /* whammy bar fill           */
#define COL_YELLOW  0xFFE0U   /* STRUM status indicator    */
#define COL_RED     0xF800U   /* MUTED status indicator    */
#define COL_GREY    0x2104U   /* whammy bar empty segments */

/* ── ST7735R register addresses ─────────────────────────────────────────── */
#define ST_SWRESET  0x01U
#define ST_SLPOUT   0x11U
#define ST_NORON    0x13U
#define ST_INVOFF   0x20U
#define ST_DISPON   0x29U
#define ST_CASET    0x2AU
#define ST_RASET    0x2BU
#define ST_RAMWR    0x2CU
#define ST_MADCTL   0x36U
#define ST_COLMOD   0x3AU
#define ST_FRMCTR1  0xB1U
#define ST_FRMCTR2  0xB2U
#define ST_FRMCTR3  0xB3U
#define ST_INVCTR   0xB4U
#define ST_PWCTR1   0xC0U
#define ST_PWCTR2   0xC1U
#define ST_PWCTR3   0xC2U
#define ST_PWCTR4   0xC3U
#define ST_PWCTR5   0xC4U
#define ST_VMCTR1   0xC5U
#define ST_GMCTRP1  0xE0U
#define ST_GMCTRN1  0xE1U

/* ── Screen geometry ─────────────────────────────────────────────────────── */
#define SCREEN_W    128U
#define SCREEN_H    160U

/* Column/row offsets — adjust if the image is shifted on your specific panel. */
#define COL_OFFSET  0U
#define ROW_OFFSET  0U

/* ── Layout constants ────────────────────────────────────────────────────── */
#define NOTE_Y      8U    /* top of large note-name area                    */
#define NOTE_H      64U   /* height of note-name area (8 rows × 4× scale)   */
#define NOTE_SCALE  4U    /* font scale for note name                        */
#define WHAMMY_Y    80U   /* top of whammy bar                               */
#define WHAMMY_H    20U   /* height of whammy bar                            */
#define STATUS_Y    108U  /* top of status text line                         */
#define STATUS_H    12U   /* height of status text area                      */

/* ── Module state ────────────────────────────────────────────────────────── */
volatile uint8_t g_display_dirty = 0U;

static uint8_t g_cursor_x = 0U;
static uint8_t g_cursor_y = 0U;

/* ── 5×8 ASCII font, 0x20–0x7E, stored in flash ────────────────────────── */
/* Each entry is 5 column bytes; bit 0 = top row, bit 7 = bottom row.       */
static const uint8_t font5x8[95][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20  ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21  '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22  '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23  '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24  '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25  '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26  '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27  ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28  '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29  ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A  '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B  '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C  ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D  '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E  '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F  '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30  '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31  '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32  '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33  '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34  '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35  '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36  '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37  '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38  '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39  '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A  ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B  ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C  '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D  '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E  '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F  '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40  '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41  'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42  'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43  'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44  'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45  'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46  'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47  'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48  'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49  'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A  'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B  'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C  'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 0x4D  'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E  'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F  'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50  'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51  'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52  'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53  'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54  'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55  'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56  'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57  'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58  'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59  'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A  'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B  '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C  '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D  ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E  '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F  '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60  '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61  'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62  'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63  'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64  'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65  'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66  'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67  'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68  'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69  'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A  'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B  'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C  'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D  'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E  'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F  'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70  'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71  'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72  'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73  's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74  't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 0x75  'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76  'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77  'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78  'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79  'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A  'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B  '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C  '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D  '}' */
    {0x08,0x08,0x2A,0x1C,0x08}, /* 0x7E  '~' */
};

/* ── Software SPI primitives ─────────────────────────────────────────────── */

static inline void cs_low(void)  { PORTB &= ~(1U << TFT_CS);  }
static inline void cs_high(void) { PORTB |=  (1U << TFT_CS);  }
static inline void dc_low(void)  { PORTC &= ~(1U << TFT_DC);  }
static inline void dc_high(void) { PORTC |=  (1U << TFT_DC);  }

/* Clocks out one byte MSB-first (CPOL=0 CPHA=0). ~10 cycles/bit at 16 MHz. */
static void spi_byte(uint8_t b)
{
    uint8_t i = 8U;
    do {
        if (b & 0x80U) PORTB |=  (1U << TFT_MOSI);
        else            PORTB &= ~(1U << TFT_MOSI);
        PORTB |=  (1U << TFT_SCK);   /* clock high — data sampled */
        PORTB &= ~(1U << TFT_SCK);   /* clock low  */
        b <<= 1U;
    } while (--i);
}

/* Sends one command byte (DC low). */
static void tft_cmd(uint8_t c)
{
    dc_low();
    cs_low();
    spi_byte(c);
    cs_high();
}

/* Sends one data byte (DC high). */
static void tft_data(uint8_t d)
{
    dc_high();
    cs_low();
    spi_byte(d);
    cs_high();
}

/* ── Address window ──────────────────────────────────────────────────────── */

/* Sets the pixel write window and opens a RAMWR data stream.
 * Subsequent data bytes are received as RGB565 pixel pairs until CS goes high. */
static void tft_set_addr(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    tft_cmd(ST_CASET);
    tft_data(0U); tft_data((uint8_t)(x0 + COL_OFFSET));
    tft_data(0U); tft_data((uint8_t)(x1 + COL_OFFSET));

    tft_cmd(ST_RASET);
    tft_data(0U); tft_data((uint8_t)(y0 + ROW_OFFSET));
    tft_data(0U); tft_data((uint8_t)(y1 + ROW_OFFSET));

    tft_cmd(ST_RAMWR);
}

/* ── Pixel helpers ───────────────────────────────────────────────────────── */

/* Streams `count` pixels of `color` into an already-open RAMWR window.
 * DC must be high and CS must be low before the call; CS left low on return. */
static void stream_pixels(uint16_t color, uint16_t count)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFU);
    while (count--) { spi_byte(hi); spi_byte(lo); }
}

/* Fills a rectangle with a solid colour. */
static void tft_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          uint16_t color)
{
    tft_set_addr(x, y, (uint8_t)(x + w - 1U), (uint8_t)(y + h - 1U));
    dc_high();
    cs_low();
    stream_pixels(color, (uint16_t)w * (uint16_t)h);
    cs_high();
}

/* ── Character rendering ─────────────────────────────────────────────────── */

/* Draws one ASCII character at pixel (x, y) using the given scale and colours.
 * Rendered width  = (5 + 1) × scale   (5 glyph columns + 1 spacing column).
 * Rendered height = 8 × scale.
 * Pixels are written in a single batched RAMWR stream (row-major order). */
static void draw_char_scaled(uint8_t x, uint8_t y, char c,
                              uint8_t scale, uint16_t fg, uint16_t bg)
{
    if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7EU) c = '?';
    uint8_t idx = (uint8_t)c - 0x20U;

    /* Cache 5 column bytes from flash into SRAM to avoid repeated pgm_read. */
    uint8_t cols[5];
    for (uint8_t i = 0U; i < 5U; i++) {
        cols[i] = pgm_read_byte(&font5x8[idx][i]);
    }

    uint8_t char_w = (uint8_t)(6U * scale);  /* 5 data + 1 gap, scaled */
    uint8_t char_h = (uint8_t)(8U * scale);

    tft_set_addr(x, y, (uint8_t)(x + char_w - 1U), (uint8_t)(y + char_h - 1U));
    dc_high();
    cs_low();

    /* Iterate font rows (bit position = vertical pixel), then scale vertically.
     * Within each scaled row, output all x-pixels (5 glyph cols + gap), scaled. */
    for (uint8_t row = 0U; row < 8U; row++) {
        for (uint8_t sr = 0U; sr < scale; sr++) {          /* vertical scale */
            for (uint8_t col = 0U; col < 5U; col++) {      /* glyph columns  */
                uint16_t px = (cols[col] & (1U << row)) ? fg : bg;
                uint8_t  phi = (uint8_t)(px >> 8);
                uint8_t  plo = (uint8_t)(px & 0xFFU);
                for (uint8_t sc = 0U; sc < scale; sc++) {  /* horizontal scale */
                    spi_byte(phi); spi_byte(plo);
                }
            }
            /* Spacing / gap column */
            for (uint8_t sc = 0U; sc < scale; sc++) {
                spi_byte((uint8_t)(bg >> 8));
                spi_byte((uint8_t)(bg & 0xFFU));
            }
        }
    }

    cs_high();
}

/* ── Busy-wait delay (used only during init before Timer0 is running) ─────── */
static void delay_ms(uint16_t ms)
{
    /* ~1 ms per step at 16 MHz / -O1; exact timing is not critical for display init. */
    while (ms--) {
        for (volatile uint16_t i = 0U; i < 3200U; i++);
    }
}

/* ── ST7735R power-on initialisation sequence ────────────────────────────── */
/* Follows the standard Adafruit "R-type" init: frame rate, power control,   */
/* gamma correction, 16-bit colour (RGB565), portrait orientation.            */
void display_init(void)
{
    /* Set all TFT control pins as outputs. */
    DDRB |= (1U << TFT_MOSI) | (1U << TFT_SCK) | (1U << TFT_CS);
    DDRC |= (1U << TFT_DC)   | (1U << TFT_RST);

    /* Idle state: SCK/MOSI low, CS high (deselected), DC low. */
    PORTB &= ~((1U << TFT_MOSI) | (1U << TFT_SCK));
    cs_high();
    dc_low();

    /* Hardware reset: pull RST low for 10 ms then release. */
    PORTC &= ~(1U << TFT_RST);  delay_ms(10U);
    PORTC |=  (1U << TFT_RST);  delay_ms(120U);

    tft_cmd(ST_SWRESET);  delay_ms(150U);  /* software reset          */
    tft_cmd(ST_SLPOUT);   delay_ms(500U);  /* exit sleep mode         */

    /* Frame rate control: normal / idle / partial modes. */
    tft_cmd(ST_FRMCTR1); tft_data(0x01U); tft_data(0x2CU); tft_data(0x2DU);
    tft_cmd(ST_FRMCTR2); tft_data(0x01U); tft_data(0x2CU); tft_data(0x2DU);
    tft_cmd(ST_FRMCTR3);
        tft_data(0x01U); tft_data(0x2CU); tft_data(0x2DU);
        tft_data(0x01U); tft_data(0x2CU); tft_data(0x2DU);

    tft_cmd(ST_INVCTR);  tft_data(0x07U); /* column inversion control */

    /* Power control registers. */
    tft_cmd(ST_PWCTR1); tft_data(0xA2U); tft_data(0x02U); tft_data(0x84U);
    tft_cmd(ST_PWCTR2); tft_data(0xC5U);
    tft_cmd(ST_PWCTR3); tft_data(0x0AU); tft_data(0x00U);
    tft_cmd(ST_PWCTR4); tft_data(0x8AU); tft_data(0x2AU);
    tft_cmd(ST_PWCTR5); tft_data(0x8AU); tft_data(0xEEU);
    tft_cmd(ST_VMCTR1); tft_data(0x0EU); /* VCOM control             */

    tft_cmd(ST_INVOFF);                   /* display inversion off    */

    /* Memory access: portrait orientation, RGB colour order. */
    tft_cmd(ST_MADCTL); tft_data(0x00U);

    /* Colour mode: 16-bit RGB565. */
    tft_cmd(ST_COLMOD); tft_data(0x05U);

    /* Positive gamma correction. */
    tft_cmd(ST_GMCTRP1);
        tft_data(0x02U); tft_data(0x1CU); tft_data(0x07U); tft_data(0x12U);
        tft_data(0x37U); tft_data(0x32U); tft_data(0x29U); tft_data(0x2DU);
        tft_data(0x29U); tft_data(0x25U); tft_data(0x2BU); tft_data(0x39U);
        tft_data(0x00U); tft_data(0x01U); tft_data(0x03U); tft_data(0x10U);

    /* Negative gamma correction. */
    tft_cmd(ST_GMCTRN1);
        tft_data(0x03U); tft_data(0x1DU); tft_data(0x07U); tft_data(0x06U);
        tft_data(0x2EU); tft_data(0x2CU); tft_data(0x29U); tft_data(0x2DU);
        tft_data(0x2EU); tft_data(0x2EU); tft_data(0x37U); tft_data(0x3FU);
        tft_data(0x00U); tft_data(0x00U); tft_data(0x02U); tft_data(0x10U);

    tft_cmd(ST_NORON);  delay_ms(10U);    /* normal display mode on   */
    tft_cmd(ST_DISPON); delay_ms(100U);   /* display on               */

    display_clear();
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void display_clear(void)
{
    tft_fill_rect(0U, 0U, SCREEN_W, SCREEN_H, COL_BLACK);
}

/* Maps page-based SSD1306-style coordinates to pixel positions:
 *   x = col × 6   (6 px per character column)
 *   y = page × 9  (9 px per character row) */
void display_set_cursor(uint8_t col, uint8_t page)
{
    g_cursor_x = (uint8_t)(col  * 6U);
    g_cursor_y = (uint8_t)(page * 9U);
}

/* Renders one character at the cursor position and advances by 6 px. */
void display_print_char(char c)
{
    draw_char_scaled(g_cursor_x, g_cursor_y, c, 1U, COL_WHITE, COL_BLACK);
    g_cursor_x += 6U;
}

void display_print_string(const char *s)
{
    while (*s) display_print_char(*s++);
}

void display_print_note(uint8_t note_index)
{
    char name[4] = "???";
    if (note_index < NUM_GUITAR_NOTES) note_name_get(note_index, name);
    display_print_string(name);
}

/* Draws a 16-segment whammy bar at WHAMMY_Y.
 * Each segment is 7 px wide with a 1 px black gap; green when filled, grey when empty. */
void display_show_whammy(uint8_t adc_val)
{
    uint8_t filled = (uint8_t)(((uint16_t)adc_val * 16U) / 256U);
    for (uint8_t seg = 0U; seg < 16U; seg++) {
        uint16_t color = (seg < filled) ? COL_GREEN : COL_GREY;
        tft_fill_rect((uint8_t)(seg * 8U),       WHAMMY_Y, 7U, WHAMMY_H, color);
        tft_fill_rect((uint8_t)(seg * 8U + 7U),  WHAMMY_Y, 1U, WHAMMY_H, COL_BLACK);
    }
}

void display_update(uint8_t note_idx, uint16_t whammy)
{
    display_set_cursor(0U, 0U);
    display_print_string("Note: ");
    display_print_note(note_idx);
    display_show_whammy((uint8_t)(whammy >> 2));
}

/* ── Private: draw the note name large and centred in NOTE_Y area ─────────── */
static void draw_note_large(uint8_t note_index)
{
    char    name[4] = "???";
    uint8_t len     = 3U;

    if (note_index < NUM_GUITAR_NOTES) {
        note_name_get(note_index, name);
        len = 0U;
        while (name[len]) len++;
    }

    uint8_t char_w  = (uint8_t)(6U * NOTE_SCALE);          /* 24 px per char */
    uint8_t total_w = (uint8_t)((uint16_t)len * char_w);
    uint8_t x0      = (uint8_t)((SCREEN_W - total_w) / 2U);

    /* Clear entire note area to avoid stale pixels from longer names. */
    tft_fill_rect(0U, NOTE_Y, SCREEN_W, NOTE_H, COL_BLACK);

    for (uint8_t i = 0U; i < len; i++) {
        draw_char_scaled((uint8_t)(x0 + (uint16_t)i * char_w), NOTE_Y,
                         name[i], NOTE_SCALE, COL_WHITE, COL_BLACK);
    }
}

/* ── Private: draw the status line ───────────────────────────────────────── */
static void draw_status(bool muted, bool strumming)
{
    tft_fill_rect(0U, STATUS_Y, SCREEN_W, STATUS_H, COL_BLACK);

    const char *msg   = 0;
    uint16_t    color = COL_WHITE;

    if (muted) {
        msg   = "   [ MUTED ]";
        color = COL_RED;
    } else if (strumming) {
        msg   = "  ** STRUM! **";
        color = COL_YELLOW;
    }

    if (msg) {
        uint8_t x = 2U;
        while (*msg) {
            draw_char_scaled(x, STATUS_Y, *msg, 1U, color, COL_BLACK);
            x   += 6U;
            msg++;
        }
    }
}

/* Full UI refresh.
 * Redraws the note name only when the note index changes.
 * Redraws the whammy bar only when the filled-segment count changes.
 * Redraws the status line only when muted/strumming state changes. */
void display_update_ui(uint8_t note_index, uint8_t whammy,
                       bool muted, bool strumming)
{
    static uint8_t last_note   = 0xFFU;
    static uint8_t last_segs   = 0xFFU;
    static uint8_t last_muted  = 0xFFU;
    static uint8_t last_strum  = 0xFFU;

    if (note_index != last_note) {
        last_note = note_index;
        draw_note_large(note_index);
    }

    uint8_t segs = (uint8_t)(((uint16_t)whammy * 16U) / 256U);
    if (segs != last_segs) {
        last_segs = segs;
        display_show_whammy(whammy);
    }

    uint8_t m = muted     ? 1U : 0U;
    uint8_t s = strumming ? 1U : 0U;
    if (m != last_muted || s != last_strum) {
        last_muted = m;
        last_strum = s;
        draw_status(muted, strumming);
    }

    g_display_dirty = 0U;
}
