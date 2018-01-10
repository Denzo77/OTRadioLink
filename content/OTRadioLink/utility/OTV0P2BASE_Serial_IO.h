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

Author(s) / Copyright (s): Damon Hart-Davis 2013--2016
                           Deniz Erbilgin 2015
*/

/*
 Serial (USB) I/O.
 
 For a V0p2 board, write to the hardware serial,
 otherwise (assuming non-embedded) write to stdout.

 Simple debug output to the serial port at its default (bootloader baud) rate.

 The debug support only enabled if V0P2BASE_DEBUG is defined,
 else does nothing, or at least as little as possible.
 */

#ifndef OTV0P2BASE_SERIAL_IO_H
#define OTV0P2BASE_SERIAL_IO_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "utility/OTV0P2BASE_ArduinoCompat.h"
#endif

#include <OTV0p2Base.h>

#include "OTV0P2BASE_Serial_LineType_InitChar.h"

namespace OTV0P2BASE
{


// Version (code/board) information printed as one line to serial (with line-end, and flushed); machine- and human- parseable.
// Format: "board VX.X REVY YYYY/Mmm/DD HH:MM:SS".
// Built as a macro to ensure __DATE__ and __TIME__ expanded in the scope of the caller.
#define V0p2Base_serialPrintlnBuildVersion_str_helper(x) #x
#define V0p2Base_serialPrintlnBuildVersion_str(x) V0p2Base_serialPrintlnBuildVersion_str_helper(x)
#define V0p2Base_serialPrintlnBuildVersion() \
  do { \
  OTV0P2BASE::serialPrintAndFlush(F("board V0.2 REV" \
      V0p2Base_serialPrintlnBuildVersion_str(V0p2_REV) \
      " ")); \
  /* Rearrange date into sensible most-significant-first order, and make it (nearly) fully numeric. */ \
  /* FIXME: would be better to have this in PROGMEM (Flash) rather than RAM, eg as F() constant. */ \
  constexpr char _YYYYMmmDD[] = \
    { \
    __DATE__[7], __DATE__[8], __DATE__[9], __DATE__[10], \
    '/', \
    __DATE__[0], __DATE__[1], __DATE__[2], \
    '/', \
    ((' ' == __DATE__[4]) ? '0' : __DATE__[4]), __DATE__[5], \
    '\0' \
    }; \
  OTV0P2BASE::serialPrintAndFlush(_YYYYMmmDD); \
  OTV0P2BASE::serialPrintlnAndFlush(F(" " __TIME__)); \
  } while(false)


#ifndef V0P2BASE_DEBUG
#define V0P2BASE_DEBUG_SERIAL_PRINT(s) // Do nothing.
#define V0P2BASE_DEBUG_SERIAL_PRINTFMT(s, format) // Do nothing.
#define V0P2BASE_DEBUG_SERIAL_PRINT_FLASHSTRING(fs) // Do nothing.
#define V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING(fs) // Do nothing.
#define V0P2BASE_DEBUG_SERIAL_PRINTLN() // Do nothing.
#define V0P2BASE_DEBUG_SERIAL_TIMESTAMP() // Do nothing.
#else

// Send simple string or numeric to serial port and wait for it to have been sent.
// Make sure that Serial.begin() has been invoked, etc.
#define V0P2BASE_DEBUG_SERIAL_PRINT(s) { OTV0P2BASE::serialPrintAndFlush(s); }
#define V0P2BASE_DEBUG_SERIAL_PRINTFMT(s, fmt) { OTV0P2BASE::serialPrintAndFlush((s), (fmt)); }
#define V0P2BASE_DEBUG_SERIAL_PRINT_FLASHSTRING(fs) { OTV0P2BASE::serialPrintAndFlush(F(fs)); }
#define V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING(fs) { OTV0P2BASE::serialPrintlnAndFlush(F(fs)); }
#define V0P2BASE_DEBUG_SERIAL_PRINTLN() { OTV0P2BASE::serialPrintlnAndFlush(); }
// Print timestamp with no newline in format: MinutesSinceMidnight:Seconds:SubCycleTime
//extern void _debug_serial_timestamp();
//#define V0P2BASE_DEBUG_SERIAL_TIMESTAMP() _debug_serial_timestamp()

#endif // V0P2BASE_DEBUG

#ifdef EFR32FG1P133F256GM48
// Implementation for EFR32
// Implementation of Print that simply throws output away.
// Usable in all environments including Arduino, eg also for unit tests.
class PrintEFR32 final : public Print
{
private:
    // Flag to prevent UART_Tx locking up when USART not set up first..
    bool isSetup = false;

    // The usart device we are using.
    // NOTE: This will not work with other ports.
    USART_TypeDef *dev = USART0;

    // What pins to multiplex the USART to.
    static constexpr uint32_t outputNo = 0;
public:
    // Start up serial dev
    void setup(const uint32_t baud);

    virtual size_t write(uint8_t c) override;
    virtual size_t write(const uint8_t *buf, size_t len) override;
};
extern PrintEFR32 Serial;
#endif


// Write a single (Flash-resident) string to serial followed by line-end and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintlnAndFlush(__FlashStringHelper const *line);

// Write a single (Flash-resident) string to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(__FlashStringHelper const *text);

// Write a single string to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(char const *text);

// Write a single (read-only) string to serial followed by line-end and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintlnAndFlush(char const *text);

// Write a single (Flash-resident) character to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(char c);

// Write a single (Flash-resident) number to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(int i, uint8_t fmt = 10); // Arduino print.h: #define DEC 10

// Write a single (Flash-resident) number to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(unsigned u, uint8_t fmt = 10); // Arduino print.h: #define DEC 10

// Write a single (Flash-resident) number to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintAndFlush(unsigned long u, uint8_t fmt = 10); // Arduino print.h: #define DEC 10

// Write line-end to serial and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialPrintlnAndFlush();

// Write a single (read-only) buffer of length len to serial followed by line-end and wait for transmission to complete.
// This enables the serial if required and shuts it down afterwards if it wasn't enabled.
void serialWriteAndFlush(char const *buf, uint8_t len);


#if defined(ARDUINO)
// Prints a single space to Serial (which must be up and running).
// Simple utility function helps reduce code size.
// Arduino only.
inline void Serial_print_space() { Serial.print(' '); }
#endif



} // OTV0P2BASE

#endif // OTV0P2BASE_SERIAL_IO_H
