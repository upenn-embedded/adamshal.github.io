/*
 * spi_dac.c — AD5686RARUZ SPI DAC driver
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#include <avr/io.h>
#include "spi_dac.h"

#define DAC_DDR   DDRB
#define DAC_PORT  PORTB
#define DAC_CS    PB2
#define DAC_MOSI  PB3
#define DAC_SCK   PB5

#define AD5686_CMD_WRITE_UPDATE_ALL  0x03U
#define AD5686_ADDR_VOUT_A           0x00U

/* Asserts the DAC chip-select line. */
static inline void cs_assert(void)
{
    DAC_PORT &= ~(1 << DAC_CS);
}

/* Deasserts the DAC chip-select line. */
static inline void cs_deassert(void)
{
    DAC_PORT |= (1 << DAC_CS);
}

/* Shifts one byte out over SPI and waits for completion. */
static void spi_transmit(uint8_t byte)
{
    SPDR = byte;
    while (!(SPSR & (1 << SPIF)));
    (void)SPDR;
}

/* Configures SPI master mode and the AD5686 chip-select pin. */
void spi_dac_init(void)
{
    DAC_DDR |= (1 << DAC_MOSI) | (1 << DAC_SCK) | (1 << DAC_CS);
    cs_deassert();
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << CPHA) | (1 << SPR0);
    SPSR &= ~(1 << SPI2X);
}

/* Sends a 16-bit sample to DAC channel A over SPI. */
void spi_dac_write(uint16_t sample)
{
    uint8_t byte0 = (uint8_t)((AD5686_CMD_WRITE_UPDATE_ALL << 4) |
                               AD5686_ADDR_VOUT_A);
    uint8_t byte1 = (uint8_t)(sample >> 8);
    uint8_t byte2 = (uint8_t)(sample & 0xFFU);

    cs_assert();
    spi_transmit(byte0);
    spi_transmit(byte1);
    spi_transmit(byte2);
    cs_deassert();
}
