#include <avr/io.h>
#include "inputs.h"

/* Active-high controller inputs: 0 V idle, 5 V pressed. */
#define GREEN_PIN        PD2
#define RED_PIN          PD3
#define YELLOW_PIN       PD4
#define BLUE_PIN         PD5
#define ORANGE_PIN       PD6
#define STRUM_PIN        PD7
#define MUTE_PIN         PB0
#define WHAMMY_ADC_CH    0U      /* PC0 / ADC0 */
#define DEBOUNCE_THRESH  3U

volatile uint16_t inputs_whammy = 0U;

static uint8_t fret_cnt[5] = {0U, 0U, 0U, 0U, 0U};
static uint8_t fret_state  = 0U;
static uint8_t last_fret   = FRET_NONE;

/* State-change debounce for strum. */
static uint8_t strum_cnt   = 0U;
static uint8_t strum_state = 0U;

/* One-shot debounce for mute press. */
static uint8_t mute_cnt    = 0U;
static uint8_t mute_state  = 0U;

static uint8_t read_frets_raw(void)
{
    uint8_t raw = 0U;

    if (PIND & (1 << GREEN_PIN))  raw |= (1U << 0);
    if (PIND & (1 << RED_PIN))    raw |= (1U << 1);
    if (PIND & (1 << YELLOW_PIN)) raw |= (1U << 2);
    if (PIND & (1 << BLUE_PIN))   raw |= (1U << 3);
    if (PIND & (1 << ORANGE_PIN)) raw |= (1U << 4);

    return raw;
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (1 << REFS0) | (channel & 0x07U);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {
    }
    return ADC;
}

void inputs_init(void)
{
    DDRD  &= ~((1 << GREEN_PIN) | (1 << RED_PIN) | (1 << YELLOW_PIN) |
               (1 << BLUE_PIN)  | (1 << ORANGE_PIN) | (1 << STRUM_PIN));

    /* No internal pull-ups because the controller drives 0 V / 5 V actively. */
    PORTD &= ~((1 << GREEN_PIN) | (1 << RED_PIN) | (1 << YELLOW_PIN) |
               (1 << BLUE_PIN)  | (1 << ORANGE_PIN) | (1 << STRUM_PIN));

    DDRB  &= ~(1 << MUTE_PIN);
    PORTB &= ~(1 << MUTE_PIN);

    /* Whammy stick on PC0 / ADC0, no pull-up. */
    DDRC  &= ~(1 << DDC0);
    PORTC &= ~(1 << PORTC0);

    /* ADC: AVcc reference, prescaler 128 for stable reads at 16 MHz. */
    ADMUX  = (1 << REFS0) | WHAMMY_ADC_CH;
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

    inputs_whammy = adc_read(WHAMMY_ADC_CH);
}

void inputs_tick(void)
{
    uint8_t raw_frets = read_frets_raw();

    for (uint8_t i = 0U; i < 5U; i++) {
        uint8_t pressed = (raw_frets >> i) & 0x01U;

        if (pressed) {
            if (fret_cnt[i] < DEBOUNCE_THRESH) {
                fret_cnt[i]++;
            }
        } else {
            if (fret_cnt[i] > 0U) {
                fret_cnt[i]--;
            }
        }

        if ((fret_cnt[i] == DEBOUNCE_THRESH) && !(fret_state & (1U << i))) {
            fret_state |= (1U << i);
            on_button_press(i);
        }

        if ((fret_cnt[i] == 0U) && (fret_state & (1U << i))) {
            fret_state &= ~(1U << i);
            on_button_release(i);
        }
    }

    {
        uint8_t cur_fret = FRET_NONE;
        for (uint8_t i = 0U; i < 5U; i++) {
            if (fret_state & (1U << i)) {
                cur_fret = i;
                break;
            }
        }

        if (cur_fret != last_fret) {
            last_fret = cur_fret;
            on_fret_change(cur_fret);
        }
    }

    /* Debounce strum as a stable state change. */
    {
        uint8_t strum_raw = (PIND & (1 << STRUM_PIN)) ? 1U : 0U;

        if (strum_raw != strum_state) {
            if (strum_cnt < DEBOUNCE_THRESH) {
                strum_cnt++;
            } else {
                strum_state = strum_raw;
                strum_cnt = 0U;

                if (strum_state) {
                    on_strum_press();
                } else {
                    on_strum_release();
                }
            }
        } else {
            strum_cnt = 0U;
        }
    }

    /* Debounce active-high mute press as a one-shot event. */
    {
        uint8_t mute_raw = (PINB & (1 << MUTE_PIN)) ? 1U : 0U;

        if (mute_raw) {
            if (mute_cnt < DEBOUNCE_THRESH) {
                mute_cnt++;
            }
        } else {
            if (mute_cnt > 0U) {
                mute_cnt--;
            }
        }

        if ((mute_cnt == DEBOUNCE_THRESH) && !mute_state) {
            mute_state = 1U;
            on_mute_press();
        }

        if ((mute_cnt == 0U) && mute_state) {
            mute_state = 0U;
        }
    }
}

void inputs_adc_scan(void)
{
    inputs_whammy = adc_read(WHAMMY_ADC_CH);
}
