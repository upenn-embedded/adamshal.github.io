# ESE3500 S26 Final Project — Guitar Synthesizer Controller
# Team 3: Synth Specialist (Guitar Hero Edition)
# University of Pennsylvania — Spring 2026

## Team
- Adam Shalabi       (adamshal@seas.upenn.edu)
- Brandon Parkansky  (bpar@seas.upenn.edu / GitHub: bpar)
- Panos Dimtsoudis   (panosdim@seas.upenn.edu / GitHub: panosdimts)

## Hardware
- MCU:        ATmega328PB @ 16MHz
- DAC:        Analog Devices AD5686RARUZ — quad 16-bit SPI DAC
                → Use VOUT A for audio signal output
                → SPI interface via MOSI/SCK/CS (SPDR, SPCR registers)
                → This satisfies the course I2C/SPI serial requirement
- Amp:        Adafruit PAM8302 mono class-D amp (#5647)
                → DAC VOUT A → amp input → speaker
- Speaker:    Adafruit 3W 4Ω enclosed mono speaker (#4445)
- Audio Jack: SparkFun 3.5mm audio jack (PRT-08032)
- Power:      8× AA battery holder (#875) → 12V
                → LTC1174CS8-5 buck converter → 5V regulated
                → 2.1mm jack (#3642) for board input
- Inputs:
    - 5 pitch buttons on PD2–PD6 (internal pullups via PORTD)
    - Strum switch on PD7 (INT1 interrupt via EICRA/EIMSK)
    - Mute button on PB0
    - Whammy potentiometer on ADC0 (ADMUX/ADCSRA registers)
    - Joystick X/Y on ADC1, ADC2

## Audio Signal Chain
  ATmega328PB
  Timer1 ISR @ 31250Hz
      → compute DDS sample (uint16_t, 0–65535)
      → write to AD5686 over SPI (SPDR register)
      → AD5686 VOUT A (analog voltage)
      → PAM8302 amp input
      → 3W speaker / 3.5mm jack output

## Course Restrictions — ALWAYS ENFORCE THESE
- Bare metal register-level C ONLY. No Arduino .ino, no HAL wrappers.
- No external libraries except: standard C library, AVR-libc,
  lab-provided libraries, FreeRTOS.
- If any external lib is used, add a detailed comment block explaining
  the protocol, data transfer structure, and library internals
  (required for ESE3500 final report appendix).
- No dynamic memory allocation — no malloc/free anywhere.
- No Li-ion or LiPo batteries. (Project uses AA batteries — compliant.)
- All peripheral access must be via direct register writes only.
  e.g., SPCR |= (1 << SPE) | (1 << MSTR), NOT SPI.begin()

## Required Course Topics Covered
- [x] Timers    — Timer1 interrupt @ 31250Hz drives audio sample output
- [x] Interrupts — INT1 for strum trigger; Timer1 ISR for DDS
- [x] ADC        — Whammy bar (ADC0), joystick (ADC1/ADC2)
- [x] Serial     — SPI to AD5686 DAC (SPCR/SPDR registers)
- [x] Advanced   — Digital signal processing: DDS wavetable synthesis

## Code Style
- C99, snake_case, explicit register widths (uint8_t, uint16_t, uint32_t)
- Every register write must have an inline comment explaining its effect:
    SPCR |= (1 << SPE) | (1 << MSTR) | (1 << SPR0); // Enable SPI, master mode, fosc/16
- Comment ISR-critical sections with cycle count estimates
- No blocking delays in ISR or audio path

## ISR Budget
- Timer1 audio ISR: ≤ 30 cycles total
- SPI write to AD5686: schedule outside ISR if cycle budget is tight
  (use double-buffering with a volatile sample register)
- All display, envelope decay, and UI logic lives in main loop via flags

## File Structure
src/
  main.c          # init, main loop, flag dispatch
  synth.c / .h    # DDS engine, Timer1 ISR, 256-entry sine wavetable (PROGMEM)
  dac.c / .h      # AD5686 SPI driver (register-level), satisfies serial req.
  inputs.c / .h   # button debounce, ADC scan, INT1 strum handler
  notes.c / .h    # note → phase increment table (PROGMEM, E2–E6)
  envelope.c / .h # attack/release state machine, triggered by strum

## Power Notes
- 8× AA = ~12V nominal → LTC1174 steps down to 5V for MCU + peripherals
- PAM8302 can run on 5V directly — check datasheet for input voltage range
- Keep analog (DAC/amp) and digital (MCU) grounds clean — comment any
  power domain decisions in hardware notes

## GitHub / Documentation
- All sprint progress in README.md (commits = proof of work)
- Comment any external library usage with protocol explanation
- GitHub Pages website required for final report