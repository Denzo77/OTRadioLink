/*
The OpenTRV project licenses this file to you
under the Apache Licence, Version 2.0 (the "Licence");
you may not use this file except in compliance
with the Licence. You may obtain a copy of the Licence at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the Licence is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the Licence for the
specific language governing permissions and limitations
under the Licence.

Author(s) / Copyright (s): Damon Hart-Davis 2013--2018
                           Deniz Erbilgin 2015
*/

/*
  Utilities to assist with minimal power usage,
  including interrupts and sleep.

  Mainly V0p2/AVR specific for now.
  */

#ifndef OTV0P2BASE_POWERMANAGEMENT_H
#define OTV0P2BASE_POWERMANAGEMENT_H

#include <stdint.h>

#ifdef ARDUINO_ARCH_AVR
#include <util/atomic.h>
#endif

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "OTV0P2BASE_ArduinoCompat.h"
#endif

#include "OTV0P2BASE_BasicPinAssignments.h"
#include "OTV0P2BASE_FastDigitalIO.h"
#include "OTV0P2BASE_Sensor.h"


namespace OTV0P2BASE
{


// Call from setup() for V0p2 board to turn off unused modules, set up timers and interrupts, etc.
// I/O pin setting is not done here.
void powerSetup();


// Selectively turn off all modules that need not run continuously on V0p2 board
// so as to minimise power without (ie over and above) explicitly entering a sleep mode.
// Suitable for start-up and for belt-and-braces use before main sleep on each cycle,
// to ensure that nothing power-hungry is accidentally left on.
// Any module that may need to run all the time should not be turned off here.
// May be called from panic(), so do not be too clever.
// Does NOT attempt to power down the radio, eg in case that needs to be left in RX mode.
// Does NOT attempt to power down the hardware serial/UART.
void minimisePowerWithoutSleep();


// Enable power to intermittent peripherals.
//   * waitUntilStable  wait long enough (and maybe test) for I/O power to become stable.
// Waiting for stable may only be necessary for those items hung from IO_POWER cap;
// items powered direct from IO_POWER_UP may need no such wait.
void power_intermittent_peripherals_enable(bool waitUntilStable = false);

// Disable/remove power to intermittent peripherals.
void power_intermittent_peripherals_disable();


#ifdef ARDUINO_ARCH_AVR
    // If ADC was disabled, power it up, and return true.
    // If already powered up then do nothing other than return false.
    // This does not power up the analogue comparator;
    // this needs to be manually enabled if required.
    // If this returns true then a matching powerDownADC() may be done.
    bool powerUpADCIfDisabled();
    // Power ADC down.
    // Likely shorter inline than just the call/return!
    inline void powerDownADC()
      {
      ADCSRA &= ~_BV(ADEN); // Do before power_[adc|all]_disable() to avoid freezing the ADC in an active state!
      PRR |= _BV(PRADC); // Disable the ADC.
      }
#elif defined(EFR32FG1P133F256GM48)
    // If ADC was disabled, power it up, and return true.
    // If already powered up then do nothing other than return false.
    // This does not power up the analogue comparator; this needs to be manually enabled if required.
    // If this returns true then a matching powerDownADC() may be advisable.
    bool powerUpADCIfDisabled();
    // Power ADC down.
    // Likely shorter inline than just the call/return!
    inline void powerDownADC()
      {
      // TODO
      }
#endif // ARDUINO_ARCH_AVR


#ifdef ARDUINO_ARCH_AVR
    // If true, default is to run the SPI bus a bit below maximum (eg for REV2 board).
    static const bool DEFAULT_RUN_SPI_SLOW = false;

    // TEMPLATED DEFINITIONS OF SPI power up/down.
    //
    // If SPI was disabled, power it up, enable it as master and with a sensible clock speed, etc, and return true.
    // If already powered up then do nothing other than return false.
    // If this returns true then a matching powerDownSPI() may be advisable.
    // The optional slowSPI flag, if true, attempts to run the bus slow, eg for when long or loaded with LED on SCK.
    template <uint8_t SPI_nSS, bool slowSPI>
    bool t_powerUpSPIIfDisabled()
        {
        ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
            {
            if(!(PRR & _BV(PRSPI))) { return(false); }

            // Ensure that nSS is HIGH ASAP and thus any slave deselected when powering up SPI.
            fastDigitalWrite(SPI_nSS, HIGH);
            // Ensure that nSS is an output to avoid forcing SPI to slave mode by accident.
            pinMode(SPI_nSS, OUTPUT);

            PRR &= ~_BV(PRSPI); // Enable SPI power.

            // Configure raw SPI to match better how it was used in PICAXE V0.09 code.
            // CPOL = 0, CPHA = 0
            // Enable SPI, set master mode, set speed.
            const uint8_t ENABLE_MASTER = _BV(SPE) | _BV(MSTR);
#if F_CPU <= 2000000 // Needs minimum prescale (x2) with slow (<=2MHz) CPU clock.
            SPCR = ENABLE_MASTER; // 2x clock prescale for <=1MHz SPI clock from <=2MHz CPU clock (500kHz SPI @ 1MHz CPU).
            if(!slowSPI) { SPSR = _BV(SPI2X); } // Slow will give 4x prescale for 250kHz bus at 1MHz CPU.
#elif F_CPU <= 8000000
            SPCR = ENABLE_MASTER; // 4x clock prescale for <=2MHz SPI clock from nominal <=8MHz CPU clock.
            SPSR = 0;
#else // Needs setting for fast (~16MHz) CPU clock.
            SPCR = _BV(SPR0) | ENABLE_MASTER; // 8x clock prescale for ~2MHz SPI clock from nominal ~16MHz CPU clock.
            SPSR = _BV(SPI2X);
#endif
            }
        return(true);
        }
    //
    // Power down SPI.
    template <uint8_t SPI_nSS, uint8_t SPI_SCK, uint8_t SPI_MOSI, uint8_t SPI_MISO, bool slowSPI>
    void t_powerDownSPI()
        {
        ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
            {
            // Ensure that nSS is HIGH ASAP and thus any slave deselected when powering up SPI.
            fastDigitalWrite(SPI_nSS, HIGH);

            SPCR &= ~_BV(SPE); // Disable SPI.
            PRR |= _BV(PRSPI); // Power down...

            // Ensure that nSS is an output to avoid forcing SPI to slave mode by accident.
            pinMode(SPI_nSS, OUTPUT);

            // Avoid pins from floating when SPI is disabled.
            // Try to preserve general I/O direction and restore previous output values for outputs.
            pinMode(SPI_SCK, OUTPUT);
            pinMode(SPI_MOSI, OUTPUT);
            pinMode(SPI_MISO, INPUT_PULLUP);

            // If sharing SPI SCK with LED indicator then return this pin to being an output (retaining previous value).
            //if(LED_HEATCALL == PIN_SPI_SCK) { pinMode(LED_HEATCALL, OUTPUT); }
            }
        }

    // STANDARD UNTEMPLATED DEFINITIONS OF SPI power up/down if PIN_SPI_nSS is defined.
    // If SPI was disabled, power it up, enable it as master and with a sensible clock speed, etc, and return true.
    // If already powered up then do nothing other than return false.
    // If this returns true then a matching powerDownSPI() may be advisable.
    inline bool powerUpSPIIfDisabled() { return(t_powerUpSPIIfDisabled<V0p2_PIN_SPI_nSS, DEFAULT_RUN_SPI_SLOW>()); }
    // Power down SPI.
    inline void powerDownSPI() { t_powerDownSPI<V0p2_PIN_SPI_nSS, V0p2_PIN_SPI_SCK, V0p2_PIN_SPI_MOSI, V0p2_PIN_SPI_MISO, DEFAULT_RUN_SPI_SLOW>(); }

    // RAII-style SPI if-necessary power up/down.
    // NOT TESTED YET: DO NOT USE!
    template <uint8_t SPI_nSS, uint8_t SPI_SCK, uint8_t SPI_MOSI, uint8_t SPI_MISO, bool slowSPI>
    class RAII_SPIPowerUpSPIIfDisabled final
        {
        public:
            const bool neededEnable;
            RAII_SPIPowerUpSPIIfDisabled()
              : neededEnable(t_powerUpSPIIfDisabled<SPI_nSS, slowSPI>()) { }
            ~RAII_SPIPowerUpSPIIfDisabled()
                {
                if(neededEnable) { t_powerDownSPI<SPI_nSS, SPI_SCK, SPI_MOSI, SPI_MISO, slowSPI>(); }
                }
        };

#endif // ARDUINO_ARCH_AVR


/************** Serial IO ************************/

#ifdef ARDUINO_ARCH_AVR
    // Default speed for primary hardware serial port for V0p2 (AVR) boards.
    static constexpr uint16_t V0P2_UART_BAUD_DEFAULT = 4800;

	// Check if serial is (already) powered up.
	inline bool _serialIsPoweredUp() { return(!(PRR & _BV(PRUSART0))); }

	// If serial (UART/USART0) was disabled, power it up, do Serial.begin(), and return true.
	// If already powered up then do nothing other than return false.
	// If this returns true then a matching powerDownSerial() may be advisable.
	// Defaults to V0p2 unit baud rate
	template <uint16_t baud = V0P2_UART_BAUD_DEFAULT>
	bool powerUpSerialIfDisabled()
	  {
	  if(_serialIsPoweredUp()) { return(false); }
	  PRR &= ~_BV(PRUSART0); // Enable the UART.
	  Serial.begin(baud); // Set it going.
	  return(true);
	  }
	// Flush any pending serial (UART/USART0) output and power it down.
	void powerDownSerial();
	#ifdef __AVR_ATmega328P__
	// Returns true if hardware USART0 buffer in ATMmega328P is non-empty; may occasionally return a spurious false.
	// There may still be a byte in the process of being transmitted when this is false.
	// This should not interfere with HardwareSerial's handling.
	#define serialTXInProgress() (!(UCSR0A & _BV(UDRE0)))
	// Does a Serial.flush() attempting to do some useful work (eg I/O polling) while waiting for output to drain.
	// Assumes hundreds of CPU cycles available for each character queued for TX.
	// Does not change CPU clock speed or disable or mess with USART0, though may poll it.
	void flushSerialProductive();
	// Does a Serial.flush() idling for 30ms at a time while waiting for output to drain.
	// Does not change CPU clock speed or disable or mess with USART0, though may poll it.
	// Sleeps in IDLE mode for up to 15ms at a time (using watchdog) waking early on interrupt
	// so the caller must be sure RX overrun (etc) will not be an issue.
	// Switches to flushSerialProductive() behaviour
	// if in danger of overrunning a minor cycle while idling.
	void flushSerialSCTSensitive();
	#else
	#define flushSerialProductive() Serial.flush()
	#define flushSerialSCTSensitive() Serial.flush()
	#endif
#endif // ARDUINO_ARCH_AVR


#if defined(ARDUINO_ARCH_AVR)  || defined(EFR32FG1P133F256GM48)
    // If TWI (I2C) was disabled, power it up, do Wire.begin(), and return true.
    // If already powered up then do nothing other than return false.
    // If this returns true then a matching powerDownTWI() may be advisable.
    bool powerUpTWIIfDisabled();
    // Power down TWI (I2C).
    void powerDownTWI();
#endif


// The 'low battery' warning API for the battery/supply voltage sensor.
// Note: read() can be called whenever battery voltage needs to be
// re-sampled, and derived classes should not rely on only
// regular calls to / polling of read(),
// but measuring voltage is not free in terms of either time or energy.
// When the battery is not low, read()/get() must return
// a non-zero value in any concrete implementation.
class SupplyVoltageLow : public OTV0P2BASE::Sensor<uint16_t>
  {
  protected:
    // True if last-measured voltage was low.
    // Initialise to cautious value.
    bool isLow = true;
    // True if last-measured voltage was very low.
    // Initialise to cautious value.
    bool isVeryLow = true;

  public:
    // Returns true if the supply voltage is low/marginal.
    // The threshold depends on the AVR and possibly on
    // other hardware components (eg sensors) in use.
    // Below this level actuators may not reliably operate
    // or may cause brown-outs and restarts.
    // Should always return true when isSupplyVoltageVeryLow() does.
    bool isSupplyVoltageLow() const { return(isLow); }
    // Returns true if the supply voltage is very low.
    // The threshold depends on the AVR and possibly on
    // other hardware components (eg sensors) in use.
    // Below this level sensors may not reliably operate.
    // Below this level actuators may not reliably operate
    // or may cause brown-outs and restarts.
    bool isSupplyVoltageVeryLow() const { return(isVeryLow); }
  };

// Sensor for supply (eg battery) voltage in centivolts.
// Uses centivolts (cV) rather than millivolts (mV)
// to save transmitting/logging an information-free final digit
// even at the risk of some units confusion, though UCUM compliant.
// To use this an instance should be defined (there is no overhead if not).
class SupplyVoltageCentiVolts final : public SupplyVoltageLow
  {
  public:
    // Default V0p2 very low-battery threshold suitable for 2xAA NiMH, with AVR BOD at 1.8V.
    // Set to be high enough for common sensors such as SHT21, ie >= 2.1V.
    static constexpr uint16_t BATTERY_VERY_LOW_cV = 210;

    // Default V0p2 low-battery threshold suitable for 2xAA NiMH, with AVR BOD at 1.8V.
    // Set to be high enough for safe motor operation without brownouts, etc.
    static constexpr uint16_t BATTERY_LOW_cV = 245;

    // Default V0p2 threshold above which assumed to be on mains power.
    static constexpr uint16_t MAINS_MIN_cV = 300;

    // Initial 'impossible' (and implying low supply voltage) rawInv.
    static constexpr uint16_t INITIAL_RAWINV = uint16_t(~0U);

  private:
    // Internal bandgap as fraction of Vcc [0,1023] for V0p2/AVR boards.
    // (Bandgap ref is (1.1V nominal, 1.0--1.2V) for V0p2/AVR boards.)
    // Initialise to cautious (impossibly low supply) value.
    uint16_t rawInv = INITIAL_RAWINV;
    // Last measured supply voltage (cV)
    // (nominally 0V--3.6V abs max) [0,360] for V0p2 boards.
    // Initialise to cautious (impossibly low supply) value.
    // Never expected to be updated or used in an ISR, so not volatile.
    uint16_t value = 0;

  public:
    // Force a read/poll of supply voltage and return the value sensed.
    // Expensive/slow.
    // NOT thread-safe or usable within ISRs (Interrupt Service Routines).
    virtual uint16_t read() override;

    // Return last value fetched by read(); undefined before first read()).
    // Fast.
    // NOT thread-safe nor usable within ISRs (Interrupt Service Routines).
    virtual uint16_t get() const override { return(value); }

    // Returns a suggested (JSON) tag/field/key name including units of get(); NULL means no recommended tag.
    // The lifetime of the pointed-to text must be at least that of the Sensor instance.
    virtual OTV0P2BASE::Sensor_tag_t tag() const override { return(V0p2_SENSOR_TAG_F("B|cV")); }

    // Get internal bandgap (1.1V nominal, 1.0--1.2V) as fraction of Vcc on V0p2/AVR platform.
    uint16_t getRawInv() const { return(rawInv); }

    // Returns true if the supply appears to be something that does not need monitoring.
    // This assumes that anything at/above 3V is mains (for a V0p2 board)
    // or at least a long way from needing monitoring.
    // If true then the supply voltage is not low either.
    bool isMains() const { return(value >= MAINS_MIN_cV); }
  };


} // OTV0P2BASE

#endif // OTV0P2BASE_POWERMANAGEMENT_H
