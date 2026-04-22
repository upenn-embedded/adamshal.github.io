#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdint.h>
#include <stdbool.h>
#include "display.h"
#include "notes.h"

/* -------------------------------------------------------------------------- */
/* Pinout                                                                      */
/* -------------------------------------------------------------------------- */

#define TFT_MOSI   PB4
#define TFT_SCK    PB5
#define TFT_CS     PB1
#define TFT_DC     PC3
#define TFT_RST    PC4

/* -------------------------------------------------------------------------- */
/* ST7789 commands                                                             */
/* -------------------------------------------------------------------------- */

#define ST_SWRESET   0x01U
#define ST_SLPOUT    0x11U
#define ST_INVON     0x21U
#define ST_DISPON    0x29U
#define ST_CASET     0x2AU
#define ST_RASET     0x2BU
#define ST_RAMWR     0x2CU
#define ST_MADCTL    0x36U
#define ST_COLMOD    0x3AU
#define ST_PORCTRL   0xB2U
#define ST_GCTRL     0xB7U
#define ST_VCOMS     0xBBU
#define ST_LCMCTRL   0xC0U
#define ST_VDVVRHEN  0xC2U
#define ST_VRHS      0xC3U
#define ST_VDVS      0xC4U
#define ST_FRCTRL2   0xC6U
#define ST_PWCTRL1   0xD0U
#define ST_PVGAMCTRL 0xE0U
#define ST_NVGAMCTRL 0xE1U

/* -------------------------------------------------------------------------- */
/* Geometry                                                                    */
/* -------------------------------------------------------------------------- */

#define SCREEN_W    320U
#define SCREEN_H    240U

#ifndef DISPLAY_NUM_BUTTONS
#define DISPLAY_NUM_BUTTONS 5U
#endif

#ifndef DISPLAY_COMMIT_MS
#define DISPLAY_COMMIT_MS 500UL
#endif

/* -------------------------------------------------------------------------- */
/* Colors                                                                      */
/* -------------------------------------------------------------------------- */

#define COL_BLACK       0x0000U
#define COL_WHITE       0xFFFFU
#define COL_BG          0x0006U
#define COL_WHEEL_BG    0x1083U
#define COL_DIM_TEXT    0xBDF7U
#define COL_BORDER      0xFFFFU
#define COL_ARROW       0xFFE0U

#define COL_GREEN_BTN   0x07E0U
#define COL_RED_BTN     0xF800U
#define COL_YELLOW_BTN  0xFFE0U
#define COL_BLUE_BTN    0x001FU
#define COL_ORANGE_BTN  0xFD20U

#define COL_GREEN_SEL   0x87F0U
#define COL_RED_SEL     0xFBB0U
#define COL_YELLOW_SEL  0xFFF0U
#define COL_BLUE_SEL    0x7DFFU
#define COL_ORANGE_SEL  0xFEB2U

/* -------------------------------------------------------------------------- */
/* Layout                                                                      */
/* -------------------------------------------------------------------------- */

#define LEFT_X      8U
#define LEFT_Y      12U
#define LEFT_W      146U
#define ROW_H       34U
#define ROW_GAP      8U

#define RIGHT_X     166U
#define RIGHT_Y      12U
#define RIGHT_W     146U
#define RIGHT_H     200U

#define STATUS_Y    218U

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

volatile uint8_t g_display_dirty = 0U;

static uint8_t g_cursor_x = 0U;
static uint8_t g_cursor_y = 0U;

static uint8_t g_selected_button = 0U;
static uint8_t g_selected_note   = 8U;
static uint8_t g_button_notes[DISPLAY_NUM_BUTTONS] = {
    8U, 12U, 15U, 19U, 24U
};

static uint8_t  g_commit_pending  = 0U;
static uint32_t g_commit_deadline = 0UL;

static const char *button_labels[DISPLAY_NUM_BUTTONS] = {
    "GREEN", "RED", "YELLOW", "BLUE", "ORANGE"
};

/* -------------------------------------------------------------------------- */
/* 5x8 font                                                                    */
/* -------------------------------------------------------------------------- */

static const uint8_t font5x8[95][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08}
};

static inline void cs_low(void)  { PORTB &= ~(1U << TFT_CS); }
static inline void cs_high(void) { PORTB |=  (1U << TFT_CS); }
static inline void dc_low(void)  { PORTC &= ~(1U << TFT_DC); }
static inline void dc_high(void) { PORTC |=  (1U << TFT_DC); }

static void delay_ms(uint16_t ms)
{
    while (ms--) {
        for (volatile uint16_t i = 0U; i < 3200U; i++) { ; }
    }
}

static uint16_t button_fill_color(uint8_t idx, bool selected)
{
    switch (idx) {
    case 0U: return selected ? COL_GREEN_SEL  : COL_GREEN_BTN;
    case 1U: return selected ? COL_RED_SEL    : COL_RED_BTN;
    case 2U: return selected ? COL_YELLOW_SEL : COL_YELLOW_BTN;
    case 3U: return selected ? COL_BLUE_SEL   : COL_BLUE_BTN;
    default: return selected ? COL_ORANGE_SEL : COL_ORANGE_BTN;
    }
}

static uint16_t button_text_color(bool selected)
{
    return selected ? COL_BLACK : COL_WHITE;
}

static uint8_t wrap_button_index(int16_t idx)
{
    while (idx < 0) idx += DISPLAY_NUM_BUTTONS;
    while (idx >= DISPLAY_NUM_BUTTONS) idx -= DISPLAY_NUM_BUTTONS;
    return (uint8_t)idx;
}

static uint8_t wrap_note_index(int16_t idx)
{
    while (idx < 0) idx += NUM_GUITAR_NOTES;
    while (idx >= NUM_GUITAR_NOTES) idx -= NUM_GUITAR_NOTES;
    return (uint8_t)idx;
}

static void note_name_local(uint8_t idx, char out[4])
{
    static const char note_names[NUM_GUITAR_NOTES][4] = {
        "E2",  "F2",  "F#2", "G2",  "G#2",
        "A2",  "A#2", "B2",  "C3",  "C#3",
        "D3",  "D#3", "E3",  "F3",  "F#3",
        "G3",  "G#3", "A3",  "A#3", "B3",
        "C4",  "C#4", "D4",  "D#4", "E4"
    };

    if (idx < NUM_GUITAR_NOTES) {
        out[0] = note_names[idx][0];
        out[1] = note_names[idx][1];
        out[2] = note_names[idx][2];
        out[3] = '\0';
    } else {
        out[0] = '?';
        out[1] = '?';
        out[2] = '?';
        out[3] = '\0';
    }
}

static void spi_byte(uint8_t b)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        if (b & 0x80U) PORTB |=  (1U << TFT_MOSI);
        else           PORTB &= ~(1U << TFT_MOSI);
        PORTB |=  (1U << TFT_SCK);
        PORTB &= ~(1U << TFT_SCK);
        b <<= 1U;
    }
}

static void tft_cmd(uint8_t c)
{
    dc_low();
    cs_low();
    spi_byte(c);
    cs_high();
}

static void tft_data(uint8_t d)
{
    dc_high();
    cs_low();
    spi_byte(d);
    cs_high();
}

static void tft_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    tft_cmd(ST_CASET);
    tft_data((uint8_t)(x0 >> 8));
    tft_data((uint8_t)(x0 & 0xFFU));
    tft_data((uint8_t)(x1 >> 8));
    tft_data((uint8_t)(x1 & 0xFFU));

    tft_cmd(ST_RASET);
    tft_data((uint8_t)(y0 >> 8));
    tft_data((uint8_t)(y0 & 0xFFU));
    tft_data((uint8_t)(y1 >> 8));
    tft_data((uint8_t)(y1 & 0xFFU));

    tft_cmd(ST_RAMWR);
}

static void stream_pixels(uint16_t color, uint32_t count)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFU);
    while (count--) {
        spi_byte(hi);
        spi_byte(lo);
    }
}

static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0U || h == 0U) return;
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;

    tft_set_addr(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    dc_high();
    cs_low();
    stream_pixels(color, (uint32_t)w * (uint32_t)h);
    cs_high();
}

static void tft_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    tft_fill_rect(x, y, w, 1U, color);
}

static void tft_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w < 1U || h < 1U) return;
    tft_fill_rect(x, y, w, 1U, color);
    tft_fill_rect(x, (uint16_t)(y + h - 1U), w, 1U, color);
    tft_fill_rect(x, y, 1U, h, color);
    tft_fill_rect((uint16_t)(x + w - 1U), y, 1U, h, color);
}

static void draw_char_scaled(uint16_t x, uint16_t y, char c, uint8_t scale,
                             uint16_t fg, uint16_t bg)
{
    if ((uint8_t)c < 0x20U || (uint8_t)c > 0x7EU) c = '?';
    uint8_t idx = (uint8_t)c - 0x20U;

    uint8_t cols[5];
    for (uint8_t i = 0U; i < 5U; i++) cols[i] = pgm_read_byte(&font5x8[idx][i]);

    uint16_t char_w = (uint16_t)(6U * scale);
    uint16_t char_h = (uint16_t)(8U * scale);

    if (x + char_w > SCREEN_W || y + char_h > SCREEN_H) return;

    tft_set_addr(x, y, (uint16_t)(x + char_w - 1U), (uint16_t)(y + char_h - 1U));
    dc_high();
    cs_low();

    for (uint8_t row = 0U; row < 8U; row++) {
        for (uint8_t sr = 0U; sr < scale; sr++) {
            for (uint8_t col = 0U; col < 5U; col++) {
                uint16_t px = (cols[col] & (1U << row)) ? fg : bg;
                uint8_t hi = (uint8_t)(px >> 8);
                uint8_t lo = (uint8_t)(px & 0xFFU);
                for (uint8_t sc = 0U; sc < scale; sc++) {
                    spi_byte(hi);
                    spi_byte(lo);
                }
            }
            for (uint8_t sc = 0U; sc < scale; sc++) {
                spi_byte((uint8_t)(bg >> 8));
                spi_byte((uint8_t)(bg & 0xFFU));
            }
        }
    }
    cs_high();
}

static void draw_string_scaled(uint16_t x, uint16_t y, const char *s, uint8_t scale,
                               uint16_t fg, uint16_t bg)
{
    while (*s) {
        draw_char_scaled(x, y, *s, scale, fg, bg);
        x += (uint16_t)(6U * scale);
        s++;
    }
}

static uint16_t str_px_w(const char *s, uint8_t scale)
{
    uint16_t len = 0U;
    while (*s++) len++;
    return (uint16_t)(len * 6U * scale);
}

static void draw_string_centered(uint16_t x, uint16_t y, uint16_t w, const char *s,
                                 uint8_t scale, uint16_t fg, uint16_t bg)
{
    uint16_t px = str_px_w(s, scale);
    uint16_t x0 = x;
    if (px < w) x0 = x + (uint16_t)((w - px) / 2U);
    draw_string_scaled(x0, y, s, scale, fg, bg);
}

static void draw_underlined_label(uint16_t x, uint16_t y, const char *s, uint8_t scale,
                                  uint16_t fg, uint16_t bg, bool underline)
{
    uint16_t w = str_px_w(s, scale);
    draw_string_scaled(x, y, s, scale, fg, bg);

    if (underline) {
        uint16_t uy = y + (uint16_t)(8U * scale) + 1U;
        tft_draw_hline(x, uy, w, COL_WHITE);
    }
}

static void draw_ui_full(void)
{
    tft_fill_rect(0U, 0U, SCREEN_W, SCREEN_H, COL_BG);

    for (uint8_t i = 0U; i < DISPLAY_NUM_BUTTONS; i++) {
        uint16_t y = LEFT_Y + (uint16_t)i * (ROW_H + ROW_GAP);
        bool selected = (i == g_selected_button);
        uint16_t fill = button_fill_color(i, selected);
        uint16_t txt  = button_text_color(selected);
        char note[4];

        tft_fill_rect(LEFT_X, y, LEFT_W, ROW_H, fill);
        tft_draw_rect(LEFT_X, y, LEFT_W, ROW_H, COL_BORDER);

        draw_underlined_label(LEFT_X + 8U, y + 10U, button_labels[i], 2U, txt, fill, selected);

        draw_string_scaled(LEFT_X + 92U, y + 10U, "-", 2U, txt, fill);
        draw_string_scaled(LEFT_X + 104U, y + 10U, "-", 2U, txt, fill);
        draw_string_scaled(LEFT_X + 116U, y + 10U, "-", 2U, txt, fill);

        note_name_local(g_button_notes[i], note);
        draw_string_scaled(LEFT_X + 124U, y + 10U, note, 2U, txt, fill);
    }

    tft_fill_rect(RIGHT_X, RIGHT_Y, RIGHT_W, RIGHT_H, COL_WHEEL_BG);
    tft_draw_rect(RIGHT_X, RIGHT_Y, RIGHT_W, RIGHT_H, COL_BORDER);

    draw_string_centered(RIGHT_X, RIGHT_Y + 8U, RIGHT_W, "NOTE", 2U, COL_WHITE, COL_WHEEL_BG);

    for (int8_t ofs = -2; ofs <= 2; ofs++) {
        uint8_t idx = wrap_note_index((int16_t)g_selected_note + ofs);
        char note[4];
        uint16_t y;
        uint8_t scale;
        uint16_t fg;

        if (ofs == -2) {
            y = RIGHT_Y + 34U;
            scale = 2U;
            fg = COL_DIM_TEXT;
        } else if (ofs == -1) {
            y = RIGHT_Y + 68U;
            scale = 2U;
            fg = COL_DIM_TEXT;
        } else if (ofs == 0) {
            y = RIGHT_Y + 104U;
            scale = 3U;
            fg = COL_WHITE;
        } else if (ofs == 1) {
            y = RIGHT_Y + 150U;
            scale = 2U;
            fg = COL_DIM_TEXT;
        } else {
            y = RIGHT_Y + 184U;
            scale = 2U;
            fg = COL_DIM_TEXT;
        }

        note_name_local(idx, note);
        draw_string_centered(RIGHT_X, y, RIGHT_W, note, scale, fg, COL_WHEEL_BG);
    }

    draw_string_scaled(RIGHT_X + 10U, RIGHT_Y + 112U, ">", 2U, COL_ARROW, COL_WHEEL_BG);
    draw_string_scaled(RIGHT_X + RIGHT_W - 22U, RIGHT_Y + 112U, "<", 2U, COL_ARROW, COL_WHEEL_BG);

    if (g_commit_pending) {
        draw_string_scaled(10U, STATUS_Y, "Commit in 500ms", 2U, COL_WHITE, COL_BG);
    }

    g_display_dirty = 0U;
}

void display_init(void)
{
    DDRB |= (1U << TFT_MOSI) | (1U << TFT_SCK) | (1U << TFT_CS);
    DDRC |= (1U << TFT_DC) | (1U << TFT_RST);

    PORTB &= ~((1U << TFT_MOSI) | (1U << TFT_SCK));
    cs_high();
    dc_low();

    PORTC &= ~(1U << TFT_RST);
    delay_ms(30U);
    PORTC |= (1U << TFT_RST);
    delay_ms(120U);

    tft_cmd(ST_SWRESET);
    delay_ms(150U);

    tft_cmd(ST_SLPOUT);
    delay_ms(120U);

    tft_cmd(ST_COLMOD);
    tft_data(0x55U);
    delay_ms(10U);

    /* 90 degrees right, RGB color order */
    tft_cmd(ST_MADCTL);
    tft_data(0xA0U);

    tft_cmd(ST_PORCTRL);
    tft_data(0x0CU);
    tft_data(0x0CU);
    tft_data(0x00U);
    tft_data(0x33U);
    tft_data(0x33U);

    tft_cmd(ST_GCTRL);
    tft_data(0x35U);

    tft_cmd(ST_VCOMS);
    tft_data(0x19U);

    tft_cmd(ST_LCMCTRL);
    tft_data(0x2CU);

    tft_cmd(ST_VDVVRHEN);
    tft_data(0x01U);

    tft_cmd(ST_VRHS);
    tft_data(0x12U);

    tft_cmd(ST_VDVS);
    tft_data(0x20U);

    tft_cmd(ST_FRCTRL2);
    tft_data(0x0FU);

    tft_cmd(ST_PWCTRL1);
    tft_data(0xA4U);
    tft_data(0xA1U);

    tft_cmd(ST_PVGAMCTRL);
    tft_data(0xD0U); tft_data(0x04U); tft_data(0x0DU); tft_data(0x11U);
    tft_data(0x13U); tft_data(0x2BU); tft_data(0x3FU); tft_data(0x54U);
    tft_data(0x4CU); tft_data(0x18U); tft_data(0x0DU); tft_data(0x0BU);
    tft_data(0x1FU); tft_data(0x23U);

    tft_cmd(ST_NVGAMCTRL);
    tft_data(0xD0U); tft_data(0x04U); tft_data(0x0CU); tft_data(0x11U);
    tft_data(0x13U); tft_data(0x2CU); tft_data(0x3FU); tft_data(0x44U);
    tft_data(0x51U); tft_data(0x2FU); tft_data(0x1FU); tft_data(0x1FU);
    tft_data(0x20U); tft_data(0x23U);

    tft_cmd(ST_INVON);
    delay_ms(10U);

    tft_cmd(ST_DISPON);
    delay_ms(120U);

    g_selected_button = 0U;
    g_selected_note = g_button_notes[g_selected_button];
    g_commit_pending = 0U;
    g_commit_deadline = 0UL;

    display_force_redraw();
    draw_ui_full();
}

void display_clear(void)
{
    tft_fill_rect(0U, 0U, SCREEN_W, SCREEN_H, COL_BG);
}

void display_force_redraw(void)
{
    g_display_dirty = 1U;
}

void display_ui_tick(uint32_t now_ms)
{
    if (g_commit_pending) {
        if ((int32_t)(now_ms - g_commit_deadline) >= 0) {
            g_button_notes[g_selected_button] = g_selected_note;
            g_commit_pending = 0U;
            g_display_dirty = 1U;
        }
    }

    if (g_display_dirty) {
        draw_ui_full();
    }
}

void display_move_button_selection(int8_t dir)
{
    g_selected_button = wrap_button_index((int16_t)g_selected_button + dir);
    g_selected_note = g_button_notes[g_selected_button];
    g_commit_pending = 0U;
    g_display_dirty = 1U;
}

void display_move_note_selection(int8_t dir, uint32_t now_ms)
{
    g_selected_note = wrap_note_index((int16_t)g_selected_note + dir);
    g_commit_pending = 1U;
    g_commit_deadline = now_ms + DISPLAY_COMMIT_MS;
    g_display_dirty = 1U;
}

void display_set_button_note(uint8_t button_idx, uint8_t note_idx)
{
    if (button_idx >= DISPLAY_NUM_BUTTONS || note_idx >= NUM_GUITAR_NOTES) return;

    g_button_notes[button_idx] = note_idx;
    if (button_idx == g_selected_button) {
        g_selected_note = note_idx;
        g_commit_pending = 0U;
    }
    g_display_dirty = 1U;
}

uint8_t display_get_button_note(uint8_t button_idx)
{
    if (button_idx >= DISPLAY_NUM_BUTTONS) return g_button_notes[0];
    return g_button_notes[button_idx];
}

uint8_t display_get_selected_button(void)
{
    return g_selected_button;
}

uint8_t display_get_selected_note(void)
{
    return g_selected_note;
}

void display_copy_button_notes(uint8_t dest[DISPLAY_NUM_BUTTONS])
{
    for (uint8_t i = 0U; i < DISPLAY_NUM_BUTTONS; i++) {
        dest[i] = g_button_notes[i];
    }
}

void display_set_cursor(uint8_t col, uint8_t page)
{
    g_cursor_x = (uint8_t)(col * 6U);
    g_cursor_y = (uint8_t)(page * 9U);
}

void display_print_char(char c)
{
    draw_char_scaled(g_cursor_x, g_cursor_y, c, 1U, COL_WHITE, COL_BG);
    g_cursor_x = (uint8_t)(g_cursor_x + 6U);
}

void display_print_string(const char *s)
{
    while (*s) display_print_char(*s++);
}

void display_print_note(uint8_t note_index)
{
    char name[4];
    note_name_local(note_index, name);
    display_print_string(name);
}

void display_show_whammy(uint8_t adc_val)
{
    (void)adc_val;
}

void display_update(uint8_t note_idx, uint16_t whammy)
{
    (void)note_idx;
    (void)whammy;
    display_force_redraw();
}

void display_update_ui(uint8_t note_index, uint8_t whammy, bool muted, bool strumming)
{
    (void)note_index;
    (void)whammy;
    (void)muted;
    (void)strumming;

    if (g_display_dirty) {
        draw_ui_full();
    }
}