/*
 * envelope.c — ADSR envelope state machine
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include "envelope.h"
#include <stdint.h>

#define TICKS_PER_MS    31U
#define ATTACK_MS       10U
#define DECAY_MS        50U
#define SUSTAIN_LEVEL   40000U
#define RELEASE_MS      200U

typedef enum {
    ENV_IDLE    = 0,
    ENV_ATTACK  = 1,
    ENV_DECAY   = 2,
    ENV_SUSTAIN = 3,
    ENV_RELEASE = 4
} env_state_t;

volatile uint32_t envelope_ticks = 0;

static volatile env_state_t env_state = ENV_IDLE;
static volatile uint16_t    env_gain  = 0;

static uint16_t attack_step;
static uint16_t decay_step;
static uint16_t release_step;

/* Precomputes ADSR step sizes and resets to idle. */
void envelope_init(void)
{
    uint16_t v;

    v = (uint16_t)(65535U / ((uint32_t)ATTACK_MS * TICKS_PER_MS));
    attack_step  = (v < 1U) ? 1U : v;

    v = (uint16_t)((65535U - SUSTAIN_LEVEL) /
                   ((uint32_t)DECAY_MS * TICKS_PER_MS));
    decay_step   = (v < 1U) ? 1U : v;

    v = (uint16_t)(SUSTAIN_LEVEL /
                   ((uint32_t)RELEASE_MS * TICKS_PER_MS));
    release_step = (v < 1U) ? 1U : v;

    env_state      = ENV_IDLE;
    env_gain       = 0;
    envelope_ticks = 0;
}

/* Starts the attack phase. */
void envelope_trigger(void)
{
    env_state = ENV_ATTACK;
}

/* Starts the release phase from any active state. */
void envelope_release(void)
{
    if (env_state != ENV_IDLE) {
        env_state = ENV_RELEASE;
    }
}

/* Advances the envelope state machine by one sample tick. */
void envelope_tick(void)
{
    envelope_ticks++;

    switch (env_state) {

    case ENV_IDLE:
        break;

    case ENV_ATTACK:
        if (env_gain >= (uint16_t)(65535U - attack_step)) {
            env_gain  = 65535U;
            env_state = ENV_DECAY;
        } else {
            env_gain += attack_step;
        }
        break;

    case ENV_DECAY:
        if (env_gain <= (uint16_t)(SUSTAIN_LEVEL + decay_step)) {
            env_gain  = SUSTAIN_LEVEL;
            env_state = ENV_SUSTAIN;
        } else {
            env_gain -= decay_step;
        }
        break;

    case ENV_SUSTAIN:
        break;

    case ENV_RELEASE:
        if (env_gain <= release_step) {
            env_gain  = 0;
            env_state = ENV_IDLE;
        } else {
            env_gain -= release_step;
        }
        break;

    default:
        env_gain  = 0;
        env_state = ENV_IDLE;
        break;
    }
}

/* Returns the current gain scalar (0 = silence, 65535 = full scale). */
uint16_t envelope_get(void)
{
    return env_gain;
}
