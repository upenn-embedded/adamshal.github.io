/*
 * spi_dac.h — AD5686RARUZ SPI DAC driver interface
 * ESE3500 Final Project — Guitar Synthesizer Controller
 * Team 3: Synth Specialist (Guitar Hero Edition)
 * University of Pennsylvania — Spring 2026
 *
 * Authors: Adam Shalabi, Brandon Parkansky, Panos Dimtsoudis
 */

#ifndef SPI_DAC_H
#define SPI_DAC_H

#include <stdint.h>

/*
 * spi_dac_init()
 *   Configure the ATmega328PB SPI peripheral as master via SPCR/SPSR
 */
void spi_dac_init(void);

/*
 * spi_dac_write(sample)
 *
 *   @param sample  16-bit unsigned DAC value (0–65535, full scale)
 */
void spi_dac_write(uint16_t sample);

#endif /* SPI_DAC_H */
