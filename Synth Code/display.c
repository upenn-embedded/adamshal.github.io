/*
 * display.c ? ST7796S 480�320 LCD driver (software SPI)
 * ESE3500 Final Project ? Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania ? Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 *
 * ?? Hardware connections ??????????????????????????????????????????
 *   PB4 = MOSI   PB5 = SCK   PB1 = CS   PC3 = DC   PC4 = RST
 *
 * ?? Performance note ?????????????????????????????????????????????
 *   Software SPI at 16 MHz ? 3 �s/byte.  A full redraw takes ~600 ms.
 *   Partial updates (text-only scroll, single box) take 5?60 ms.
 *   For faster updates rewire MOSI to PB3 and use hardware SPI0.
 *
 * ?? Colour calibration ???????????????????????????????????????????
 *   MADCTL_VAL 0xE8 ? landscape, BGR colour order, 180� corrected.
 *   If still rotated wrong try 0x68 (remove MY) or 0xA8 (remove MX).
 *   If colours wrong on a different panel try 0xE0 (BGR=0).
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <string.h>
#include <stdint.h>

#include "display.h"
#include "notes.h"

/* ???????????????????????????????????????????????????????????????????
 * Pin macros
 * ??????????????????????????????????????????????????????????????????? */
#define TFT_MOSI_HI()  (PORTB |=  (1U << PB4))
#define TFT_MOSI_LO()  (PORTB &= ~(1U << PB4))
#define TFT_SCK_HI()   (PORTB |=  (1U << PB5))
#define TFT_SCK_LO()   (PORTB &= ~(1U << PB5))
#define TFT_CS_HI()    (PORTB |=  (1U << PB1))
#define TFT_CS_LO()    (PORTB &= ~(1U << PB1))
#define TFT_DC_HI()    (PORTC |=  (1U << PC3))   /* data  */
#define TFT_DC_LO()    (PORTC &= ~(1U << PC3))   /* command */
#define TFT_RST_HI()   (PORTC |=  (1U << PC4))
#define TFT_RST_LO()   (PORTC &= ~(1U << PC4))

/* ???????????????????????????????????????????????????????????????????
 * ST7796S command bytes
 * ??????????????????????????????????????????????????????????????????? */
#define ST_SWRESET  0x01U
#define ST_SLPOUT   0x11U
#define ST_NORON    0x13U
#define ST_DISPON   0x29U
#define ST_CASET    0x2AU   /* column address set */
#define ST_RASET    0x2BU   /* row address set    */
#define ST_RAMWR    0x2CU   /* write to frame RAM */
#define ST_MADCTL   0x36U   /* memory data access control */
#define ST_COLMOD   0x3AU   /* colour mode */

/* MY=1, MX=1, MV=1, BGR=1 ? landscape 480�320, BGR colour order.
 *
 * Why these bits:
 *   MY+MX+MV  rotates the scan direction 180� relative to 0x60,
 *             correcting the upside-down / mirrored image.
 *   BGR=1     swaps the red and blue channels in the pixel data,
 *             correcting the inverted colours.
 *
 * If the image is still wrong try removing MY (? 0x68) or MX (? 0xA8).
 */
#define MADCTL_VAL  0xE8U

/* ???????????????????????????????????????????????????????????????????
 * Colour palette ? BGR565
 *
 * With MADCTL BGR=1 the display maps incoming pixel bits as:
 *   bits [15:11] ? Blue channel (5 bits)
 *   bits [10:5]  ? Green channel (6 bits)
 *   bits  [4:0]  ? Red channel (5 bits)
 *
 * Use the helper macro:  BGR565(r, g, b)
 *   r : 0?31   g : 0?63   b : 0?31
 * ??????????????????????????????????????????????????????????????????? */
#define BGR565(r, g, b) \
    ((uint16_t)(((uint16_t)(b) << 11) | ((uint16_t)(g) << 5) | (uint16_t)(r)))

#define COLOR_BG         BGR565(31,  0, 0)   /* deep blue background   */
#define COLOR_WHITE      BGR565(31, 63, 31)   /* white                  */
#define COLOR_DIVIDER    BGR565(31, 63, 31)   /* white divider          */
#define COLOR_RED        BGR565(0,  0,  31)   /* red                    */
#define COLOR_ORANGE     BGR565(0, 41,  31)   /* orange                 */
#define COLOR_YELLOW     BGR565(0, 63,  31)   /* yellow                 */
#define COLOR_GREEN      BGR565(0, 63,  0)   /* green                  */
#define COLOR_BLUE_BOX   BGR565(28, 20, 0)   /* bright blue            */
#define COLOR_SCROLL_BG  BGR565(2,  2, 16)   /* dark blue, scroll bg   */
#define COLOR_SCROLL_HL  BGR565(26, 28, 2)   /* highlight for centre   */
#define COLOR_UNDERLINE  BGR565(31, 63, 31)   /* white underline        */

/* ???????????????????????????????????????????????????????????????????
 * Layout constants
 * ??????????????????????????????????????????????????????????????????? */
#define SCREEN_W    480U
#define SCREEN_H    320U
 
/* Left panel ? 5 button boxes stacked vertically */
#define BTN_X       5U
#define BTN_W       180U
#define BTN_H       58U
#define BTN_GAP     4U      /* vertical gap between boxes */
#define BTN_Y0      5U      /* y of top edge of first box */
#define BTN_MARGIN  8U      /* horizontal text margin inside box */
 
/* Right panel ? scroll wheel */
#define SCR_X       196U
#define SCR_Y       5U
#define SCR_W       279U
#define SCR_H       310U
#define SCR_SLOTS   5U
#define SCR_SLOT_H  (SCR_H / SCR_SLOTS)   /* 62 px per slot */
 
/* Text rendering scales (font cell = 6�8 px at scale 1) */
#define SCALE_BTN   2U   /* button labels and note names */
#define SCALE_N     2U   /* non-centre scroll items      */
#define SCALE_C     3U   /* centre (highlighted) item    */
 
/* Max erase region for scroll text: 4 chars at largest scale        */
#define SCR_ERASE_W ((uint16_t)(4U * 6U * SCALE_C))   /* 72 px */
#define SCR_ERASE_H ((uint16_t)(8U * SCALE_C))        /* 24 px */
 
/* ???????????????????????????????????????????????????????????????????
 * 5�8 ASCII font [0x20?0x7E], PROGMEM.
 * 5 bytes per glyph, one byte per column.
 * LSB = topmost pixel row.  Bit 7 is always 0 (natural row gap).
 * ??????????????????????????????????????????????????????????????????? */
static const uint8_t font5x8[95][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20  ' '  */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21  '!'  */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22  '"'  */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23  '#'  */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24  '$'  */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25  '%'  */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26  '&'  */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27  '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28  '('  */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29  ')'  */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A  '*'  */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B  '+'  */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C  ','  */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D  '-'  */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E  '.'  */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F  '/'  */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30  '0'  */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31  '1'  */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32  '2'  */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33  '3'  */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34  '4'  */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35  '5'  */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36  '6'  */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37  '7'  */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38  '8'  */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39  '9'  */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A  ':'  */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B  ';'  */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C  '<'  */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D  '='  */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E  '>'  */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F  '?'  */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40  '@'  */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41  'A'  */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42  'B'  */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43  'C'  */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44  'D'  */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45  'E'  */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46  'F'  */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47  'G'  */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48  'H'  */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49  'I'  */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A  'J'  */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B  'K'  */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C  'L'  */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 0x4D  'M'  */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E  'N'  */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F  'O'  */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50  'P'  */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51  'Q'  */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52  'R'  */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53  'S'  */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54  'T'  */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55  'U'  */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56  'V'  */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57  'W'  */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58  'X'  */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59  'Y'  */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A  'Z'  */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B  '['  */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C  '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D  ']'  */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E  '^'  */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F  '_'  */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60  '`'  */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61  'a'  */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62  'b'  */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63  'c'  */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64  'd'  */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65  'e'  */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66  'f'  */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67  'g'  */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68  'h'  */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69  'i'  */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A  'j'  */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B  'k'  */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C  'l'  */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D  'm'  */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E  'n'  */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F  'o'  */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70  'p'  */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71  'q'  */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72  'r'  */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73  's'  */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74  't'  */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 0x75  'u'  */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76  'v'  */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77  'w'  */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78  'x'  */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79  'y'  */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A  'z'  */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B  '{'  */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C  '|'  */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D  '}'  */
    {0x08,0x08,0x2A,0x1C,0x08}, /* 0x7E  '~'  */
};
 
/* ???????????????????????????????????????????????????????????????????
 * Module state
 * ??????????????????????????????????????????????????????????????????? */
static uint8_t g_btn_notes[5];   /* note index assigned to each button  */
static uint8_t g_sel_btn;        /* highlighted button (0?4)            */
static uint8_t g_scroll_note;    /* note at centre of scroll wheel      */
 
/* Button labels ? top to bottom: Red, Orange, Yellow, Green, Blue */
static const char btn_labels[5][7] = {
    "RED", "ORANGE", "YELLOW", "GREEN", "BLUE"
};
 
/* Box fill colours (match label text) */
static const uint16_t btn_colors[5] = {
    COLOR_RED,
    COLOR_ORANGE,
    COLOR_YELLOW,
    COLOR_GREEN,
    COLOR_BLUE_BOX,
};
 
/* Default note assignments (open guitar strings, high to low) */
static const uint8_t btn_default_notes[5] = {
    GUITAR_NOTE_B3,   /* Red    */
    GUITAR_NOTE_A2,   /* Orange */
    GUITAR_NOTE_G3,   /* Yellow */
    GUITAR_NOTE_E4,   /* Green  */
    GUITAR_NOTE_D3,   /* Blue   */
};
 
/* ???????????????????????????????????????????????????????????????????
 * Software SPI
 * ??????????????????????????????????????????????????????????????????? */
 
/* Shifts one byte out MSB-first. CS and DC must be set by the caller. */
static void spi_write_byte(uint8_t b)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        if (b & 0x80U) { TFT_MOSI_HI(); } else { TFT_MOSI_LO(); }
        TFT_SCK_HI();
        TFT_SCK_LO();
        b = (uint8_t)(b << 1U);
    }
}
 
/* Sends one command byte (DC low, CS pulses around the byte). */
static void tft_cmd(uint8_t cmd)
{
    TFT_DC_LO();
    TFT_CS_LO();
    spi_write_byte(cmd);
    TFT_CS_HI();
}
 
/* Sends one data byte (DC high, CS pulses around the byte). */
static void tft_data(uint8_t d)
{
    TFT_DC_HI();
    TFT_CS_LO();
    spi_write_byte(d);
    TFT_CS_HI();
}
 
/* ???????????????????????????????????????????????????????????????????
 * Window / pixel helpers
 * ??????????????????????????????????????????????????????????????????? */
 
/* Sets the ST7796S write window.  Must call ST_RAMWR + data after. */
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Column address set */
    TFT_DC_LO(); TFT_CS_LO(); spi_write_byte(ST_CASET);
    TFT_DC_HI();
    spi_write_byte((uint8_t)(x0 >> 8U)); spi_write_byte((uint8_t)(x0 & 0xFFU));
    spi_write_byte((uint8_t)(x1 >> 8U)); spi_write_byte((uint8_t)(x1 & 0xFFU));
    TFT_CS_HI();
 
    /* Row address set */
    TFT_DC_LO(); TFT_CS_LO(); spi_write_byte(ST_RASET);
    TFT_DC_HI();
    spi_write_byte((uint8_t)(y0 >> 8U)); spi_write_byte((uint8_t)(y0 & 0xFFU));
    spi_write_byte((uint8_t)(y1 >> 8U)); spi_write_byte((uint8_t)(y1 & 0xFFU));
    TFT_CS_HI();
}
 
/*
 * fill_rect ? fills a rectangle with a solid colour.
 * Sends RAMWR once then streams all pixels without toggling CS.
 */
static void fill_rect(uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0U || h == 0U) { return; }
 
    set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
 
    /* Send RAMWR command, then switch to data mode for the pixel stream. */
    TFT_DC_LO(); TFT_CS_LO(); spi_write_byte(ST_RAMWR); TFT_DC_HI();
 
    uint8_t  hi = (uint8_t)(color >> 8U);
    uint8_t  lo = (uint8_t)(color & 0xFFU);
    uint32_t n  = (uint32_t)w * (uint32_t)h;
 
    while (n--) {
        spi_write_byte(hi);
        spi_write_byte(lo);
    }
    TFT_CS_HI();
}
 
/* ???????????????????????????????????????????????????????????????????
 * Text rendering
 * Font cell at scale s: (5+1)�s wide, 8�s tall (1 col gap, 1 row gap).
 * ??????????????????????????????????????????????????????????????????? */
 
/*
 * draw_char ? renders one glyph at (x,y), pixel-doubled by scale.
 * Sets a single window for the whole glyph and streams all pixels.
 */
static void draw_char(uint16_t x, uint16_t y, char c,
                       uint16_t fg, uint16_t bg, uint8_t scale)
{
    if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7EU) { c = '?'; }
    uint8_t fidx = (uint8_t)c - 0x20U;
 
    uint16_t cw = (uint16_t)(6U * (uint16_t)scale);   /* cols incl. gap */
    uint16_t ch = (uint16_t)(8U * (uint16_t)scale);   /* rows incl. gap */
 
    set_window(x, y, (uint16_t)(x + cw - 1U), (uint16_t)(y + ch - 1U));
    TFT_DC_LO(); TFT_CS_LO(); spi_write_byte(ST_RAMWR); TFT_DC_HI();
 
    uint8_t fhi = (uint8_t)(fg >> 8U); uint8_t flo = (uint8_t)(fg & 0xFFU);
    uint8_t bhi = (uint8_t)(bg >> 8U); uint8_t blo = (uint8_t)(bg & 0xFFU);
 
    for (uint16_t r = 0U; r < ch; r++) {
        uint8_t font_row = (uint8_t)(r / (uint16_t)scale);
        for (uint16_t ci = 0U; ci < cw; ci++) {
            uint8_t font_col = (uint8_t)(ci / (uint16_t)scale);
            uint8_t pixel    = 0U;
            if (font_col < 5U) {
                uint8_t col_data = pgm_read_byte(&font5x8[fidx][font_col]);
                pixel = (col_data >> font_row) & 1U;
            }
            if (pixel) { spi_write_byte(fhi); spi_write_byte(flo); }
            else        { spi_write_byte(bhi); spi_write_byte(blo); }
        }
    }
    TFT_CS_HI();
}
 
/* draw_string ? draws a null-terminated string left-to-right. */
static void draw_string(uint16_t x, uint16_t y, const char *s,
                         uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*s) {
        draw_char(x, y, *s, fg, bg, scale);
        x += (uint16_t)(6U * (uint16_t)scale);
        s++;
    }
}
 
/* Returns the pixel width of string s at the given scale. */
static uint16_t str_px_w(const char *s, uint8_t scale)
{
    return (uint16_t)(strlen(s) * 6U * (uint16_t)scale);
}
 
/* ???????????????????????????????????????????????????????????????????
 * Button box rendering
 * ??????????????????????????????????????????????????????????????????? */
 
/* Returns the y-coordinate of the top edge of button box idx. */
static uint16_t btn_top_y(uint8_t idx)
{
    return (uint16_t)(BTN_Y0 + (uint16_t)idx * (uint16_t)(BTN_H + BTN_GAP));
}
 
/*
 * draw_button_box ? redraws one button box completely.
 *
 * Box contents:
 *   Left side:  colour label (e.g. "RED"), white text
 *               + white underline if this box is selected
 *   Right side: assigned note name (e.g. "B3"), white text
 */
static void draw_button_box(uint8_t idx)
{
    uint16_t    bx    = BTN_X;
    uint16_t    by    = btn_top_y(idx);
    uint16_t    bc    = btn_colors[idx];
    const char *label = btn_labels[idx];
 
    /* 1. Fill box background ---------------------------------------- */
    fill_rect(bx, by, BTN_W, BTN_H, bc);
 
    /* 2. Colour label, vertically centred, left-aligned -------------- */
    uint16_t lh  = (uint16_t)(8U * (uint16_t)SCALE_BTN);
    uint16_t lx  = bx + BTN_MARGIN;
    uint16_t ly  = by + (uint16_t)((BTN_H - lh) / 2U);
    draw_string(lx, ly, label, COLOR_WHITE, bc, SCALE_BTN);
 
    /* 3. Underline if selected --------------------------------------- */
    if (idx == g_sel_btn) {
        uint16_t lw = str_px_w(label, SCALE_BTN);
        /* 2 px thick underline, 2 px below glyph baseline */
        fill_rect(lx, (uint16_t)(ly + lh + 2U), lw, 2U, COLOR_UNDERLINE);
    }
 
    /* 4. Note name, vertically centred, right-aligned ---------------- */
    char note_buf[4];
    note_name_get(g_btn_notes[idx], note_buf);
    uint16_t nw = str_px_w(note_buf, SCALE_BTN);
    uint16_t nx = (uint16_t)(bx + BTN_W - nw - BTN_MARGIN);
    draw_string(nx, ly, note_buf, COLOR_WHITE, bc, SCALE_BTN);
}
 
/* ???????????????????????????????????????????????????????????????????
 * Scroll wheel rendering
 * ??????????????????????????????????????????????????????????????????? */
 
/* Returns the y-coordinate of the top edge of scroll slot s. */
static uint16_t slot_top_y(uint8_t slot)
{
    return (uint16_t)(SCR_Y + (uint16_t)slot * (uint16_t)SCR_SLOT_H);
}
 
/*
 * draw_scroll_slot ? draws one scroll slot (background + note text).
 *
 * slot:      0 (top) ? 4 (bottom), slot 2 is always the centre.
 * note_idx:  index into note_phase_inc / note_names tables (0?48).
 * is_centre: 1 ? highlighted background + white border + large text.
 */
static void draw_scroll_slot(uint8_t slot, uint8_t note_idx, uint8_t is_centre)
{
    uint16_t sy  = slot_top_y(slot);
    uint16_t bg  = is_centre ? COLOR_SCROLL_HL : COLOR_SCROLL_BG;
    uint8_t  sc  = is_centre ? SCALE_C : SCALE_N;
 
    /* Background */
    fill_rect(SCR_X, sy, SCR_W, (uint16_t)SCR_SLOT_H, bg);
 
    /* Horizontal separator at top of slot (not for the very first slot) */
    if (slot > 0U) {
        fill_rect(SCR_X, sy, SCR_W, 1U, COLOR_BG);
    }
 
    /* Note name, centred horizontally and vertically in slot */
    char buf[4];
    note_name_get(note_idx, buf);
    uint16_t tw = str_px_w(buf, sc);
    uint16_t th = (uint16_t)(8U * (uint16_t)sc);
    uint16_t tx = SCR_X + (uint16_t)((SCR_W - tw) / 2U);
    uint16_t ty = sy    + (uint16_t)((SCR_SLOT_H - th) / 2U);
    draw_string(tx, ty, buf, COLOR_WHITE, bg, sc);
 
    /* White border around centre slot to highlight it */
    if (is_centre) {
        fill_rect(SCR_X,                             sy,                           SCR_W, 2U, COLOR_WHITE);
        fill_rect(SCR_X,                             (uint16_t)(sy + SCR_SLOT_H - 2U), SCR_W, 2U, COLOR_WHITE);
        fill_rect(SCR_X,                             sy,                           2U, (uint16_t)SCR_SLOT_H, COLOR_WHITE);
        fill_rect((uint16_t)(SCR_X + SCR_W - 2U),   sy,                           2U, (uint16_t)SCR_SLOT_H, COLOR_WHITE);
    }
}
 
/*
 * draw_scroll_wheel ? redraws all 5 slots for the current g_scroll_note.
 * Used during init and force_redraw.  ~260 ms with software SPI.
 */
static void draw_scroll_wheel(void)
{
    for (uint8_t s = 0U; s < SCR_SLOTS; s++) {
        int16_t offset  = (int16_t)s - 2;        /* -2?+2 around centre */
        int16_t ni      = (int16_t)g_scroll_note + offset;
 
        while (ni < 0)                           { ni += (int16_t)NUM_GUITAR_NOTES; }
        while (ni >= (int16_t)NUM_GUITAR_NOTES)  { ni -= (int16_t)NUM_GUITAR_NOTES; }
 
        draw_scroll_slot(s, (uint8_t)ni, (s == 2U) ? 1U : 0U);
    }
}
 
/*
 * redraw_scroll_text_only ? erases and redraws only the note-name text
 * in each of the 5 slots.  Backgrounds and borders are untouched.
 *
 * This is the fast path called by display_move_note_selection().
 * ~15 ms with software SPI (vs ~260 ms for a full scroll redraw).
 *
 * Erase region: the maximum possible text bounding box (4 chars at
 * SCALE_C), centred in each slot.  This safely covers all note names
 * (longest is 4 chars, e.g. "A#2") at the largest scale.
 */
static void redraw_scroll_text_only(void)
{
    for (uint8_t s = 0U; s < SCR_SLOTS; s++) {
        int16_t offset = (int16_t)s - 2;
        int16_t ni     = (int16_t)g_scroll_note + offset;
 
        while (ni < 0)                          { ni += (int16_t)NUM_GUITAR_NOTES; }
        while (ni >= (int16_t)NUM_GUITAR_NOTES) { ni -= (int16_t)NUM_GUITAR_NOTES; }
 
        uint16_t sy        = slot_top_y(s);
        uint8_t  is_centre = (s == 2U) ? 1U : 0U;
        uint16_t bg        = is_centre ? COLOR_SCROLL_HL : COLOR_SCROLL_BG;
        uint8_t  sc        = is_centre ? SCALE_C : SCALE_N;
 
        /* Erase: fill the max-size text bounding box with background.
         * The white border of the centre slot is at �2 px from the
         * slot edges, well outside this erase region.                */
        uint16_t erase_x = SCR_X + (uint16_t)((SCR_W  - SCR_ERASE_W) / 2U);
        uint16_t erase_y = sy    + (uint16_t)((SCR_SLOT_H - SCR_ERASE_H) / 2U);
        fill_rect(erase_x, erase_y, SCR_ERASE_W, SCR_ERASE_H, bg);
 
        /* Draw new note name */
        char buf[4];
        note_name_get((uint8_t)ni, buf);
        uint16_t tw = str_px_w(buf, sc);
        uint16_t th = (uint16_t)(8U * (uint16_t)sc);
        uint16_t tx = SCR_X + (uint16_t)((SCR_W - tw) / 2U);
        uint16_t ty = sy    + (uint16_t)((SCR_SLOT_H - th) / 2U);
        draw_string(tx, ty, buf, COLOR_WHITE, bg, sc);
    }
}
 
/* ???????????????????????????????????????????????????????????????????
 * ST7796S initialisation sequence
 * ??????????????????????????????????????????????????????????????????? */
static void tft_hw_init(void)
{
    /* Configure pins as outputs */
    DDRB |= (1U << PB4) | (1U << PB5) | (1U << PB1);
    DDRC |= (1U << PC3) | (1U << PC4);
 
    /* Idle state: CS deasserted, clock low */
    TFT_CS_HI();
    TFT_DC_HI();
    TFT_SCK_LO();
    TFT_MOSI_LO();
 
    /* Hardware reset: pulse RST low for ?10 ms */
    TFT_RST_HI();
    _delay_ms(5);
    TFT_RST_LO();
    _delay_ms(20);
    TFT_RST_HI();
    _delay_ms(150);   /* wait for internal regulator to stabilise */
 
    /* Software reset */
    tft_cmd(ST_SWRESET);
    _delay_ms(150);
 
    /* Sleep out */
    tft_cmd(ST_SLPOUT);
    _delay_ms(120);
 
    /* Memory data access control
     * 0x60 = MX=1, MV=1, BGR=0 ? landscape 480�320, RGB colour order.
     * If colours appear inverted change to 0x68 (BGR=1).
     * If image is rotated try 0x20, 0xA0, 0xC0, or 0xE0.           */
    tft_cmd(ST_MADCTL);
    tft_data(MADCTL_VAL);
 
    /* Colour mode: 16-bit RGB565 */
    tft_cmd(ST_COLMOD);
    tft_data(0x55U);
 
    /* Normal display mode on */
    tft_cmd(ST_NORON);
 
    /* Display on */
    tft_cmd(ST_DISPON);
    _delay_ms(25);
}
 
/* ???????????????????????????????????????????????????????????????????
 * Public API
 * ??????????????????????????????????????????????????????????????????? */
 
void display_init(void)
{
    /* Initialise state */
    for (uint8_t i = 0U; i < 5U; i++) {
        g_btn_notes[i] = btn_default_notes[i];
    }
    g_sel_btn     = 0U;
    g_scroll_note = g_btn_notes[0];   /* scroll wheel starts on Red's note */
 
    tft_hw_init();
 
    /* Flood background */
    fill_rect(0U, 0U, SCREEN_W, SCREEN_H, COLOR_BG);
 
    /* White vertical divider between left panel and scroll wheel */
    fill_rect((uint16_t)(BTN_X + BTN_W + 5U), 0U, 2U, SCREEN_H, COLOR_DIVIDER);
 
    /* Draw all 5 button boxes */
    for (uint8_t i = 0U; i < 5U; i++) {
        draw_button_box(i);
    }
 
    /* Draw scroll wheel */
    draw_scroll_wheel();
}
 
void display_force_redraw(void)
{
    fill_rect(0U, 0U, SCREEN_W, SCREEN_H, COLOR_BG);
    fill_rect((uint16_t)(BTN_X + BTN_W + 5U), 0U, 2U, SCREEN_H, COLOR_DIVIDER);
 
    for (uint8_t i = 0U; i < 5U; i++) {
        draw_button_box(i);
    }
    draw_scroll_wheel();
}
 
/*
 * display_move_button_selection ? moves the highlighted box up or down.
 * Redraws only the old box (remove underline) and new box (add underline).
 */
void display_move_button_selection(int8_t dir)
{
    uint8_t old = g_sel_btn;
 
    if (dir < 0) {
        g_sel_btn = (g_sel_btn == 0U) ? 4U : (uint8_t)(g_sel_btn - 1U);
    } else if (dir > 0) {
        g_sel_btn = (g_sel_btn == 4U) ? 0U : (uint8_t)(g_sel_btn + 1U);
    }
 
    if (g_sel_btn != old) {
        draw_button_box(old);        /* redraw without underline */
        draw_button_box(g_sel_btn);  /* redraw with underline    */
    }
}
 
/*
 * display_move_note_selection ? scrolls the note wheel by one step.
 * now_ms is reserved for future animation timing and is currently unused.
 * Only redraws the text in each slot (fast path, ~15 ms).
 */
void display_move_note_selection(int8_t dir, uint32_t now_ms)
{
    (void)now_ms;   /* reserved */
 
    if (dir > 0) {
        g_scroll_note = (uint8_t)((g_scroll_note + 1U) % (uint8_t)NUM_GUITAR_NOTES);
    } else if (dir < 0) {
        g_scroll_note = (g_scroll_note == 0U)
                      ? (uint8_t)(NUM_GUITAR_NOTES - 1U)
                      : (uint8_t)(g_scroll_note  - 1U);
    }
 
    redraw_scroll_text_only();
}
 
/*
 * display_commit_selected_note ? saves the centre scroll note to the
 * highlighted button and redraws only that button box.
 */
void display_commit_selected_note(void)
{
    g_btn_notes[g_sel_btn] = g_scroll_note;
    draw_button_box(g_sel_btn);
}
 
uint8_t display_get_button_note(uint8_t btn)
{
    if (btn >= 5U) { return g_btn_notes[0]; }
    return g_btn_notes[btn];
}
 
uint8_t display_get_selected_button(void)
{
    return g_sel_btn;
}
 
uint8_t display_get_selected_note(void)
{
    return g_scroll_note;
}