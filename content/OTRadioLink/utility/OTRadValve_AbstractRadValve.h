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

Author(s) / Copyright (s): Damon Hart-Davis 2015--2018
*/

/*
 * OpenTRV abstract/base (thermostatic) radiator valve driver class.
 *
 * Also includes some common supporting base/interface classes.
 */

#ifndef ARDUINO_LIB_OTRADVALVE_ABSTRACTRADVALVE_H
#define ARDUINO_LIB_OTRADVALVE_ABSTRACTRADVALVE_H


#include <stddef.h>
#include <stdint.h>
#include <OTV0p2Base.h>
#include "OTRadValve_Parameters.h"


// Use namespaces to help avoid collisions.
namespace OTRadValve
    {


// Abstract class for motor drive.
// Supports abstract model plus remote (wireless) and local/direct
// implementations.
//
// Implementations may require read() called at a fixed rate,
// though should tolerate calls being skipped when time is tight
// for other operations,
// since read() may take substantial time (hundreds of milliseconds).
// Implementations must document when read() calls are critical,
// and/or expose alternative API for the time-critical elements.
//
// Some implementations may consume significant time in set()
// as well as or instead of read().
//
// Note that the 'value' of this actuator when set() is a target,
// and the get() which returns an adjusted target or actual position
// may never exactly match the value set().
// The default starting target value is 0 (fully closed).
// An alternative useful initial value is to start
// just below the call-for-heat threshold for passive frost protection.
class AbstractRadValve : public OTV0P2BASE::SimpleTSUint8Actuator
  {
  protected:
    // Prevent direct creation of naked instance of this base/abstract class.
    constexpr AbstractRadValve() { }
    constexpr AbstractRadValve(uint8_t initTarget)
      : OTV0P2BASE::SimpleTSUint8Actuator(initTarget) { }

  public:
    // Returns (JSON) tag/field/key name including units (%); never NULL.
    // Overriding this is not permitted, to save confusion later.
    virtual OTV0P2BASE::Sensor_tag_t tag() const override final
        { return(V0p2_SENSOR_TAG_F("v|%")); }

    // Returns true if this target valve open % value passed is valid, ie in range [0,100].
    virtual bool isValid(const uint8_t value) const override
        { return(value <= 100); }

    // Set new target valve percent open.
    // Ignores invalid values.
    // Implementations may reject attempts to directly set the value.
    // If this returns true then the new target value was accepted.
    //
    // Even if accepted this remains a target,
    // and the value returned by get() may never (exactly) match it.
    // Note that for simple synchronous implementations
    //
    // this may block for hundreds of milliseconds
    // to perform some or all of the actuation.
    virtual bool set(const uint8_t /*newValue*/) { return(false); }

    // Call when given user signal that valve has been fitted (ie is fully on).
    // By default does nothing (no valve fitting may be needed).
    virtual void signalValveFitted() { }

    // Waiting for indication that the valve head has been fitted to the tail.
    // By default returns false (no valve fitting may be needed).
    virtual bool isWaitingForValveToBeFitted() const { return(false); }

    // Returns true iff not in error state and not (re)calibrating/(re)initialising/(re)syncing.
    // By default there is no recalibration step.
    virtual bool isInNormalRunState() const { return(true); }

    // Returns true if in an error state.
    // May be recoverable by forcing recalibration.
    virtual bool isInErrorState() const { return(false); }

    // True if the controlled physical valve is thought to be at least partially open right now.
    // If multiple valves are controlled then
    // this is true only if all are at least partially open.
    // Used to help avoid running boiler pump against closed valves.
    // Must not be true while (re)calibrating.
    // The default is to use the check the current computed position
    // against the minimum open percentage.
    virtual bool isControlledValveReallyOpen() const
        { return(isInNormalRunState() && (value >= getMinPercentOpen())); }

    // True if this unit is actively calling for heat.
    // This implies that the temperature is (significantly) under target,
    // the valve is really open,
    // and this needs more heat than can be passively drawn from an already-running boiler.
    // The default is to return true when valve is safely open.
    // Thread-safe and ISR safe.
    virtual bool isCallingForHeat() const
        { return(isControlledValveReallyOpen() && (value >= DEFAULT_VALVE_PC_SAFER_OPEN)); }

    // True if the room/ambient temperature is below target, enough to likely call for heat.
    // This implies that the temperature is (significantly) under target,
    // the valve is really open,
    // and this needs more heat than can be passively drawn
    // from an already-running boiler.
    // The default is to return same as isCallingForHeat().
    // Thread-safe and ISR safe.
    virtual bool isUnderTarget() const
        { return(isCallingForHeat()); }

    // Get estimated minimum percentage open for significant flow for this device; strictly positive in range [1,99].
    // Defaults to 1 which is minimum possible legitimate value.
    virtual uint8_t getMinPercentOpen() const { return(1); }

    // Minimally wiggles the motor to give tactile/audible feedback.
    // May take a significant fraction of a second.
    // Finishes with the motor turned off
    // (if that doesn't break something).
    // May also be used to (re)calibrate any shaft/position encoder
    // and end-stop detection.
    // Logically const since nominally does not change
    // the final state of the valve.
    // By default does nothing.
    virtual void wiggle() const { }
  };


// Null radiator valve driver implementation.
// Never in normal (nor error) state.
class NULLRadValve final : public AbstractRadValve
  {
  public:
    // Always false for null implementation.
    virtual bool isInNormalRunState() const override { return(false); }
    // Does nothing.
    virtual uint8_t read() override { return(0); }
  };


// Mock/settable radiator valve driver implementation.
// Never in normal (nor error) state.
class RadValveMock final : public AbstractRadValve
  {
  public:
    // Returns % open value; no calculation/work is done.
    virtual uint8_t read() override { return(get()); }
    // Set new target valve percent open.
    // Ignores invalid values.
    // If this returns true then the new target value was accepted.
    virtual bool set(const uint8_t newValue) override
      { if(!isValid(newValue)) { return(false); } value = newValue; return(true); }
    // Reset to initial state.
    void reset() { set(0); }
  };


namespace BinaryRelayHelper {
    // Returns true when value is above or equal to DEFAULT_VALVE_PC_SAFER_OPEN
    // Intended for unit testing BinaryRelayDirect.
    static inline bool calcRelayState(uint8_t value) {
         return(DEFAULT_VALVE_PC_SAFER_OPEN <= value);
    }
}
#ifdef ARDUINO_ARCH_AVR
/**
 * @brief Actuator/driver for direct local control of electric heating, using
 *        an SSR or a relay.
 * 
 * @param RELAY_DigitalPin: The output pin to drive the relay with.
 * @param activeHigh: Set true if driving RELAY_DigitalPin high will turn the
 *                    relay on. Defaults to false, i.e. the relay circuit is
 *                    active low.
 */
template<uint8_t RELAY_DigitalPin, bool activeHigh = false>
class BinaryRelayDirect : public OTRadValve::AbstractRadValve
  {
  public:

    // Setup the relay pin.
    void setup() {
        // off position
        fastDigitalWrite(RELAY_DigitalPin, !activeHigh);
        pinMode(RELAY_DigitalPin, OUTPUT);
    }

    // Regular poll/update.
    virtual uint8_t read() override { return(value); }

    // Set new target %-open value (if in range) sets the output pin.
    // Returns true if the specified value is accepted.
    virtual bool set(const uint8_t newValue) override
      {
      if(newValue > 100) { return(false); }
      value = newValue;
      const bool isActive = BinaryRelayHelper::calcRelayState(newValue);
      fastDigitalWrite(RELAY_DigitalPin, (activeHigh ? isActive : !isActive));
      return(true);
      }

    // Get estimated minimum percentage open for significant flow for this
    // device; strictly positive in range [1,99].
    virtual uint8_t getMinPercentOpen() const override
      { return(DEFAULT_VALVE_PC_SAFER_OPEN); }

  };
#endif // ARDUINO_ARCH_AVR
// Generic callback handler for hardware valve motor driver.
class HardwareMotorDriverInterfaceCallbackHandler
  {
  public:
    // Called when end stop hit, eg by overcurrent detection.
    // Can be called while run() is in progress.
    // Is ISR-/thread- safe.
    virtual void signalHittingEndStop(bool opening) = 0;

    // Called when encountering leading edge of a mark in the shaft rotation in forward direction (falling edge in reverse).
    // Can be called while run() is in progress.
    // Is ISR-/thread- safe.
    virtual void signalShaftEncoderMarkStart(bool opening) = 0;

    // Called with each motor run sub-cycle tick.
    // Is ISR-/thread- safe.
    virtual void signalRunSCTTick(bool opening) = 0;
  };

// Trivial do-nothing implementation of HardwareMotorDriverInterfaceCallbackHandler.
class NullHardwareMotorDriverInterfaceCallbackHandler final : public HardwareMotorDriverInterfaceCallbackHandler
  {
  public:
    virtual void signalHittingEndStop(bool) override { }
    virtual void signalShaftEncoderMarkStart(bool) override { }
    virtual void signalRunSCTTick(bool) override { }
  };

// Minimal end-stop-noting implementation of HardwareMotorDriverInterfaceCallbackHandler.
// The field endStopHit should be cleared before starting/running the motor.
class EndStopHardwareMotorDriverInterfaceCallbackHandler final : public HardwareMotorDriverInterfaceCallbackHandler
  {
  public:
    bool endStopHit;
    virtual void signalHittingEndStop(bool) override { endStopHit = true; }
    virtual void signalShaftEncoderMarkStart(bool) override { }
    virtual void signalRunSCTTick(bool) override { }
  };


// Interface/base for low-level hardware motor driver.
class HardwareMotorDriverInterface
  {
  public:
    // Legal motor drive states.
    enum motor_drive : uint8_t
      {
      motorOff = 0, // Motor switched off (default).
      motorDriveClosing, // Drive towards the valve-closed position.
      motorDriveOpening, // Drive towards the valve-open position.
      motorStateInvalid // Higher than any valid state.
      };

  public:
    // Detect (poll) if end-stop is reached or motor current otherwise very high.
    virtual bool isCurrentHigh(HardwareMotorDriverInterface::motor_drive mdir = motorDriveOpening) const = 0;

    // Poll simple shaft encoder output; true if on mark, false if not or if unused for this driver.
    virtual bool isOnShaftEncoderMark() const { return(false); }

    // Call to actually run/stop motor.
    // May take as much as (say) 200ms eg to change direction.
    // Stopping (removing power) should typically be very fast, << 100ms.
    //   * maxRunTicks  maximum sub-cycle ticks to attempt to run/spin for);
    //     0 will run for shortest reasonable time and may raise or ignore stall current limits,
    //     ~0 will run as long as possible and may attempt to ride through sticky mechanics
    //     eg with some run time ignoring stall current entirely
    //   * dir  direction to run motor (or off/stop)
    //   * callback  callback handler
    virtual void motorRun(uint8_t maxRunTicks, motor_drive dir, HardwareMotorDriverInterfaceCallbackHandler &callback) = 0;
  };


    }

#endif
