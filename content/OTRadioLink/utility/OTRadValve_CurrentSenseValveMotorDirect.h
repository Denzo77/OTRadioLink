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

Author(s) / Copyright (s): Damon Hart-Davis 2015--2017
                           Deniz Erbilgin 2016
*/

/*
 * Hardware-independent logic to control a hardware valve base with proportional control.
 */

#ifndef ARDUINO_LIB_OTRADVALVE_CURRENTSENSEVALVEMOTORDIRECT_H_
#define ARDUINO_LIB_OTRADVALVE_CURRENTSENSEVALVEMOTORDIRECT_H_


#include <stdint.h>
#include "OTRadValve_AbstractRadValve.h"

#include "OTV0P2BASE_ErrorReport.h"


namespace OTRadValve
{


// This driver attempts to relatively quickly (within a minute or so)
// get the driven valve estimate close enough to the requested percentage open,
// after some initial housekeeping and (re)calibration.
//
// The definition of 'close enough' is intended to accommodate
// non-proportional drivers.
//
// See the closeEnoughToTarget() static method.
//
// Note that when the battery is low attempts to close the valve may be ignored,
// as this attempts to fail safe with the valve open (eg to prevent frost).

// Generic motor driver with end-stop detection only, aims only for full/closed.
// Unit testable.
// Designed to be embedded in a motor controller instance.
// This uses the sub-cycle clock for timing.
// This is sensitive to sub-cycle position,
// ie will work hard to avoid causing a main loop overrun.
// May report some key status on Serial,
// with any error line(s) starting with "!'.
class CurrentSenseValveMotorDirectBinaryOnly : public OTRadValve::HardwareMotorDriverInterfaceCallbackHandler
  {
  public:
    // Maximum time to move pin between fully retracted and extended and vv, seconds, strictly positive.
    // Set as a limit to allow a timeout when things go wrong.
    static const constexpr uint8_t MAX_TRAVEL_S = 4 * 60; // 4 minutes.

    // Assumed calls to read() before timeout (assuming o call each 2s).
    // If calls are received less often this will presumably take longer to perform movements,
    // so it is appropriate to use a 2s ticks approximation.
    static const constexpr uint8_t MAX_TRAVEL_WALLCLOCK_2s_TICKS = OTV0P2BASE::fnmax(4, MAX_TRAVEL_S / 2);

    // Time before starting to retract pin during initialisation, in seconds.
    // Long enough for to leave the CLI some time for setting things
    // such as setting secret keys.
    // Short enough not to be annoying waiting for the pin to retract
    // before fitting a valve.
    static const constexpr uint8_t initialRetractDelay_s = 30;

    // Runtime for dead-reckoning adjustments (from stopped) (ms).
    // Smaller values nominally allow greater precision when dead-reckoning,
    // but may force the calibration to take longer.
    // For TRV1.x 250ms+ seems good.
    static const constexpr uint8_t minMotorDRMS = 250;

    // Max consecutive end-stop hits to trust the stop really hit; strictly +ve.
    // Spurious apparent stalls may be caused by dirt, etc.
    // The calibration step may try even more steps for increased confidence.
    // Even small increases in this value may increase noise immunity a lot.
    // DHD20170116: was 3, and OK in real life; simulations suggest higher.
    static const constexpr uint8_t maxEndStopHitsToBeConfident = 4;

    // Computes minimum motor dead reckoning ticks given approx ms per tick (pref rounded down).
    // Keep inline in the header to allow compile-time computation.
    //
    // Was:
    //static const constexpr uint8_t minMotorDRTicks = OTV0P2BASE::fnmax((uint8_t)1, (uint8_t)(minMotorDRMS / OTV0P2BASE::SUBCYCLE_TICK_MS_RD));
    static constexpr uint8_t computeMinMotorDRTicks(const uint8_t subcycleTicksRoundedDown_ms)
        { return(OTV0P2BASE::fnmax((uint8_t)1, (uint8_t)(minMotorDRMS / subcycleTicksRoundedDown_ms))); }

    // Computes absolute limit in sub-cycle beyond which motor should not be started.
    // Should allow meaningful movement, stop, settle and no sub-cycle overrun.
    // Allows for up to 120ms enforced sleep either side of motor run for example.
    // This should not be so greedy as to (eg) make the CLI unusable:
    // running up to 90% of minor is pushing it for example.
    // Keep inline/constexpr to allow compile-time computation.
    static constexpr uint8_t computeSctAbsLimit(const uint8_t subcycleTicksRoundedDown_ms,
                                                const uint8_t gcst_max,
                                                const uint8_t minimumMotorRunupTicks)
        {
        return(gcst_max - OTV0P2BASE::fnmax((uint8_t) 1,
            (uint8_t) (((gcst_max + 1) / 4)
                    - minimumMotorRunupTicks - 1
                    - (240 / subcycleTicksRoundedDown_ms))));
        }

    // Basic/coarse states of driver, shared with derived classes.
    // There may be microstates within most these basic states.
    //
    // Power-up sequence will often require something like:
    //   * withdrawing the pin completely (to make valve easy to fit)
    //   * waiting for some user activation step
    //     such as pressing a button to indicate that the valve has been
    //     fitted
    //   * running an initial calibration for the valve
    //   * entering the normal state tracking the target %-open
    //     and periodically recalibrating/decalcinating.
    enum driverState : uint8_t
      {
      // Power-up state.
      init = 0,
      // Waiting to withdraw pin.
      initWaiting,
      // Retracting pin at power-up.
      valvePinWithdrawing,
      // Waiting for user signal that valve has been fitted.
      valvePinWithdrawn,
      // Calibrating full valve travel.
      valveCalibrating,
      // Normal operating state:
      // values lower than this indicate that power-up is not complete.
      valveNormal,
      // TODO: running decalcination cycle
      // (and can recalibrate and mitigate valve seating issues).
      valveDecalcinating,
      // Error state: can only normally be cleared by power-cycling.
      valveError
      };

  protected:
    // Hardware interface instance, passed by reference; never NULL.
    // Must have a lifetime exceeding that of this enclosing object.
    OTRadValve::HardwareMotorDriverInterface * const hw;

    // Pointer to function to get current sub-cycle time; never NULL.
    uint8_t (*const getSubCycleTimeFn)();

//    // Minimum percent at which valve is usually open [1,100];
//    const uint8_t minOpenPC;
//    // Minimum percent at which valve is usually moderately open [minOpenPC+1,00];
//    const uint8_t fairlyOpenPC;

    // Absolute limit in sub-cycle beyond which motor should not be started.
    // Should allow meaningful movement, stop, settle and no sub-cycle overrun.
    const uint8_t sctAbsLimit;
    // Minimum sub-cycle ticks for dead reckoning; strictly positive.
    const uint8_t minMotorDRTicks;
    // Absolute limit in sub-cycle beyond which motor should not be started for dead-reckoning pulse.
    // This should allow meaningful movement and no sub-cycle overrun.
    uint8_t computeSctAbsLimitDR() const { return(sctAbsLimit - minMotorDRTicks); }

    // Callback returns true if unnecessary activity should be suppressed to avoid disturbing occupants; can be NULL.
    // Eg when room dark and occupants may be sleeping.
    bool (*const minimiseActivityOpt)() = ((bool(*)())NULL);
    // Allows monitoring of supply voltage to avoid some activities with low batteries; can be NULL.
    // Non-const to allow call to read() to force re-measurement of supply.
    OTV0P2BASE::SupplyVoltageLow *const lowBattOpt = NULL;

    // Major state of driver.
    // On power-up (or full reset) should be 0/init.
    // Stored as a uint8_t to save a little space
    // and to make atomic operations easier.
    // Marked volatile so that individual reads
    // are ISR-/thread- safe without a mutex.
    // Hold a mutex to perform compound operations such as read/modify/write.
    // Change state with changeState() which will do some other book-keeping.
    volatile driverState state = init;
    // Change state and perform some book-keeping.
    inline void changeState(const driverState newState)
        { state = newState; clearPerState(); }

    // Data used only within one major state and not to be saved between states.
    // Thus it can be shared in a union to save space.
    // This can be cleared to all zeros with clearPerState(),
    // so starts each state zeroed.
    // Accommodates microstate needed by derived classes also.
    union
      {
      // State used while waiting to initially withdraw pin.
      struct { uint8_t ticksWaited; } initWaiting;
      // State used while calibrating.
      struct
        {
        // Put largest items first to allow for good struct packing.
        uint16_t ticksFromOpenToClosed;
        uint16_t ticksFromClosedToOpen;
        // Current micro-state, starting at zero.
        uint8_t calibState;
        // Measure of real time spent in current microstate.
        uint8_t wallclock2sTicks; // read() calls counted at ~2s intervals.
        // Number of times that end-stop has apparently been hit
        // in this direction this time.
        uint8_t endStopHitCount;
        // uint8_t runCount; // Completed round-trip calibration runs.
        } valveCalibrating;
      // State used while initially withdrawing pin.
      struct
        {
        // Measure of real time spent in current state.
        uint8_t wallclock2sTicks; // read() calls counted at ~2s intervals.
        // Number of times that end-stop has apparently been hit
        // in this direction this time.
        uint8_t endStopHitCount;
        } valvePinWithdrawing;
      // State used while waiting for the valve to be fitted.
      struct { volatile bool valveFitted; } valvePinWithdrawn;
      // State used in normal state.
      struct
        {
        // Number of times that end-stop has apparently been hit
        // in this direction this time.
        uint8_t endStopHitCount;
        } valveNormal;
      } perState;
    inline void clearPerState()
        { if(sizeof(perState) > 0) { memset(&perState, 0, sizeof(perState)); } }

    // Flag set on signalHittingEndStop() callback from end-top / stall / high-current input.
    // Marked volatile for thread-safe lock-free access (with care).
    volatile bool endStopDetected = false;

    // Current nominal percent open in range [0,100].
    // Initialised to open, reflecting initial state eg when valve fitted.
    uint8_t currentPC = 100;

    // Target % open in range [0,100].
    // Target just below call-for-heat threshold for passive frost protection.
    uint8_t targetPC = DEFAULT_VALVE_PC_SAFER_OPEN - 1;

//    // Run fast towards/to end stop as far as possible in this call.
//    // Terminates significantly before the end of the sub-cycle.
//    // Possibly allows partial recalibration, or at least re-homing.
//    // Returns true if end-stop has apparently been hit,
//    // else will require one or more further calls in new sub-cycles
//    // to hit the end-stop.
//    // May attempt to ride through stiff mechanics.
//    bool runFastTowardsEndStop(bool toOpen);

    // Run at 'normal' speed towards/to end for a fixed time/distance.
    // Terminates significantly before the end of the sub-cycle.
    // Runs at same speed as during calibration.
    // Does the right thing with dead-reckoning and/or position detection.
    // Returns true if end-stop has apparently been hit.
    bool runTowardsEndStop(bool toOpen);

//    // Run at 'normal' speed, else fast, towards/to end.
//    // Terminates significantly before the end of the sub-cycle.
//    // Runs at same speed as during calibration.
//    // Does the right thing with dead-reckoning and/or position detection.
//    // Returns true if end-stop has apparently been hit.
//    bool runTowardsEndStop(bool toOpen, bool normal)
//      { return(normal ? runTowardsEndStop(toOpen) : runFastTowardsEndStop(toOpen)); }

    // Compute and apply reconciliation/adjustment of ticks and intermediate position.
    // Uses computePosition() to compute new internal position.
    // Call after moving the valve in normal mode, eg by dead reckoning.
    // Does not ever move logically right to the end-stops:
    // use hitEndStop() for that,
    // Does nothing for non-proportional implementation.
    virtual void recomputeIntermediatePosition() { }

    // Returns true if valve is at an end stop.
    static constexpr bool isAtEndstop(const uint8_t valvePC)
        { return((0 == valvePC) || (100 == valvePC)); }

    // Reset just current percent-open value.
    void resetCurrentPC(bool hitEndstopOpen)
        { currentPC = hitEndstopOpen ? 100 : 0; }

    // Reset internal positional record when an end-stop is hit.
    // Updates current % open value for non-proportional implementation.
    virtual void hitEndstop(bool hitEndstopOpen)
        { resetCurrentPC(hitEndstopOpen); }

    // Do valveCalibrating for proportional drive; returns true to return from poll() immediately.
    // Calls changeState() directly if it needs to change state.
    // If this returns false, processing falls through to that for the non-proportional case.
    // Does nothing in the non-proportional-only implementation.
    virtual bool do_valveCalibrating_prop() { return(false); }

    // Do valveNormal start for proportional drive; returns true to return from poll() immediately.
    // Falls through to do drive to end stops or when in run-time binary-only mode.
    // Calls changeState() directly if it needs to change state.
    // If this returns false, processing falls through
    // to that for the non-proportional case.
    // Does nothing in the non-proportional-only implementation.
    virtual bool do_valveNormal_prop() { return(false); }

  public:
    // Create an instance, passing in a reference to the non-NULL hardware driver.
    // The hardware driver instance lifetime must be longer than this instance.
    //   * _getSubCycleTimeFn  pointer to function to get current sub-cycle time;
    //      never NULL
    //   * _minMotorDRTicks  minimum sub-cycle ticks for dead reckoning;
    //      strictly positive
    //   * _sctAbsLimit  absolute limit in sub-cycle beyond which
    //      motor should not be started
    //   * lowBattOpt  allows monitoring of supply voltage
    //     to avoid some activities with low batteries; can be NULL
    //   * minimiseActivityOpt  callback returns true if
    //     unnecessary activity and noise should be suppressed
    //     to avoid disturbing occupants,
    //     eg when room dark and occupants may be sleeping;
    //     can be NULL
    // Keep all the potentially slow calculations in-line here to allow them to be done at compile-time .
    CurrentSenseValveMotorDirectBinaryOnly(
        OTRadValve::HardwareMotorDriverInterface * const hwDriver,
        uint8_t (*_getSubCycleTimeFn)(),
        const uint8_t _minMotorDRTicks,
        const uint8_t _sctAbsLimit,
        OTV0P2BASE::SupplyVoltageLow *_lowBattOpt = NULL,
        bool (*const _minimiseActivityOpt)() = ((bool(*)())NULL))
//                                 uint8_t _minOpenPC = OTRadValve::DEFAULT_VALVE_PC_MIN_REALLY_OPEN,
//                                 uint8_t _fairlyOpenPC = OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN)
      : hw(hwDriver),
        getSubCycleTimeFn(_getSubCycleTimeFn),
//        minOpenPC(_minOpenPC), fairlyOpenPC(_fairlyOpenPC),
        sctAbsLimit(_sctAbsLimit),
        minMotorDRTicks(_minMotorDRTicks),
        minimiseActivityOpt(_minimiseActivityOpt), lowBattOpt(_lowBattOpt)
        { changeState(init); }

    // Poll.
    // Regular poll every 1s or 2s,
    // though tolerates missed polls eg because of other time-critical activity.
    // May block for hundreds of milliseconds.
    void poll();

    // Get major state, mostly for testing, not part of the official run-time API.
    driverState _getState() const { return(state); }

    // Get current estimated actual % open in range [0,100].
    uint8_t getCurrentPC() const { return(currentPC); }

    // Get current target % open in range [0,100].
    uint8_t getTargetPC() const { return(targetPC); }

    // Set current target % open in range [0,100].
    // Coerced into range.
    void setTargetPC(uint8_t newPC)
        { targetPC = OTV0P2BASE::fnmin(newPC, (uint8_t)100); }

    // Get estimated minimum percentage open for significant flow for this device; strictly positive in range [1,99].
    virtual uint8_t getMinPercentOpen() const
        { return(DEFAULT_VALVE_PC_MODERATELY_OPEN); }

    // True if the controlled physical valve is thought to be at least partially open right now.
    // If multiple valves are controlled then is this true
    // only if all are at least partially open.
    // Used to help avoid running boiler pump against closed valves.
    // Must not be true while (re)calibrating.
    // The default is to use the check the current computed position
    // against the minimum open percentage.
    virtual bool isControlledValveReallyOpen() const
        { return(isInNormalRunState() && (currentPC >= getMinPercentOpen())); }

    // Minimally wiggle the motor to give tactile feedback and/or show to be working.
    // May take a significant fraction of a second.
    // Finishes with the motor turned off.
    // Should also have enough movement/play to allow calibration of the shaft encoder.
    // May also help set some bounds on stall current,
    // eg if highly asymmetric at each end of travel.
    // May be ignored if not safe to do.
    // Nominally leaves the valve in the position that it started,
    // so logically 'const'.
    virtual void wiggle();

    // If true, proportional mode is never used and the valve is run to end stops instead.
    // Primarily public to allow whitebox unit testing.
    // Always true in this binary-only implementation.
    virtual bool isNonProportionalOnly() const { return(true); }

    // Called when end stop hit, eg by overcurrent detection.
    // Can be called while run() is in progress.
    // Is ISR-/thread- safe.
    virtual void signalHittingEndStop(bool /*opening*/) override final
        { endStopDetected = true; }

    // Called when encountering leading edge of a mark in the shaft rotation in forward direction (falling edge in reverse).
    // Not expected, and is ignored.
    // Is ISR-/thread- safe.
    virtual void signalShaftEncoderMarkStart(bool /*opening*/) override { }

    // Called with each motor run sub-cycle tick.
    // Not expected/needed, and is ignored.
    // Is ISR-/thread- safe.
    virtual void signalRunSCTTick(bool /*opening*/) override { }

    // Call when given user signal that valve has been fitted (ie is fully on).
    virtual void signalValveFitted()
        { if(isWaitingForValveToBeFitted()) { perState.valvePinWithdrawn.valveFitted = true; } }

    // Waiting for indication that the valve head has been fitted to the base.
    bool isWaitingForValveToBeFitted() const
        { return(state == valvePinWithdrawn); }

    // Returns true iff in normal running state.
    // True means not in error state and not
    // (re)calibrating/(re)initialising/(re)syncing.
    // May be false temporarily while decalcinating.
    bool isInNormalRunState() const { return(state == valveNormal); }

    // Returns true if in an error state.
    // May be recoverable by forcing recalibration.
    bool isInErrorState() const { return(state >= valveError); }
  };

// Base class for generic current-sensing (unit-testable) motor drivers.
// Designed to be embedded in a motor controller instance.
// Alias for convenience.
typedef CurrentSenseValveMotorDirectBinaryOnly CurrentSenseValveMotorDirectBase;

// Generic motor driver using end-stop detection and simple shaft-encoder.
// Unit-testable.
// Designed to be embedded in a motor controller instance.
// This uses the sub-cycle clock for timing.
// This is sensitive to sub-cycle position,
// ie will try to avoid causing a main loop overrun.
// May report some key status on Serial,
// with any error line(s) starting with "!'.
#define CurrentSenseValveMotorDirect_DEFINED
class CurrentSenseValveMotorDirect final : public CurrentSenseValveMotorDirectBinaryOnly
  {
  public:
    // Returns true when the current % open is 'close enough' to the target value.
    //
    // "Close enough" means:
    //   * fully open and fully closed should always be achieved
    //   * generally within an absolute tolerance (absTolerancePC)
    //     of the target value (eg 10--25%)
    //   * when target is below DEFAULT_VALVE_PC_SAFER_OPEN then any value
    //     at/below target is acceptable
    //   * when target is at or above DEFAULT_VALVE_PC_SAFER_OPEN then any value
    //     at/above target is acceptable
    // The absolute tolerance is partly guided by the fact that most TRV bases
    // are only anything like linear in throughput over a relatively small range.
    //
    // Too low a tolerance may result in many tracking errors / recalibrations.
    //
    // Too high a tolerance may result in excess valve movement
    // from the valve being pulled to end stops more than necessary.
    // DHD20170104: was 16 up to 20170104 based on empirical observations.
    // DHD20170116: set to 11 based on (unit test) simulations.
    static constexpr uint8_t absTolerancePC = 11;
    static constexpr bool closeEnoughToTarget(const uint8_t targetPC, const uint8_t currentPC)
        {
        return((targetPC == currentPC) ||
            (OTV0P2BASE::fnabsdiff(targetPC, currentPC) <= absTolerancePC) ||
            ((targetPC < OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN) && (currentPC <= targetPC)) ||
            ((targetPC >= OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN) && (currentPC >= targetPC)));
        }

    // Calibration parameters.
    // Data received during the calibration process,
    // and outputs derived from it.
    // Contains (unit-testable) computations so is public.
    class CalibrationParameters final
        {
        private:
          // Data gathered during calibration process.
          // Ticks counted (sub-cycle ticks for complete run from fully-open to fully-closed, end-stop to end-stop).
          uint16_t ticksFromOpenToClosed = 0;
          // Ticks counted (sub-cycle ticks for complete run from fully-closed to fully-open, end-stop to end-stop).
          uint16_t ticksFromClosedToOpen = 0;

          // Computed parameters based on measurements during calibration process.
          //
          // Approximate precision in % as min ticks / DR size in range [1,100].
          // Defaults to large value indicating proportional mode not possible.
          uint8_t approxPrecisionPC = bad_precision;
          // A reduced ticks open/closed in ratio to allow small conversions.
          uint8_t tfotcSmall = 0, tfctoSmall = 0;

        public:
          // (Re)populate structure and compute derived parameters.
          // Ensures that all necessary items are gathered at once and none forgotten!
          // Returns true in case of success.
          // If inputs unusable will return false and in which can will indicate not able to run proportional.
          //   * ticksFromOpenToClosed  system ticks counted when running from
          //     fully open to fully closed; should be positive
          //   * ticksFromClosedToOpen  system ticks counted when running from
          //     fully closed to fully open; should be positive
          //   * minMotorDRTicks  minimum number of motor ticks it makes sense
          //     to run motor for (eg due to inertia); strictly positive
          bool updateAndCompute(uint16_t ticksFromOpenToClosed, uint16_t ticksFromClosedToOpen, uint8_t minMotorDRTicks);

          // Get system ticks counted when running from fully open to fully closed; should be positive.
          uint16_t getTicksFromOpenToClosed() const { return(ticksFromOpenToClosed); }
          // Get system ticks counted when running from fully closed to fully open; should be positive.
          uint16_t getTicksFromClosedToOpen() const { return(ticksFromClosedToOpen); }

          // Approx precision in % as min ticks / DR size in range [0,100].
          // A return value of zero indicates that sub-percent precision is possible.
          uint8_t getApproxPrecisionPC() const { return(approxPrecisionPC); }

          // Get a reduced ticks open/closed in ratio to allow small conversions; at least a few bits; should be positive.
          uint8_t getTfotcSmall() const { return(tfotcSmall); }
          uint8_t getTfctoSmall() const { return(tfctoSmall); }

          // Compute reconciliation/adjustment of ticks, and compute % valve position [0,100].
          // Reconcile any reverse ticks (and adjust with forward ticks if needed).
          // Call after moving the valve in normal mode.
          // Unit testable.
          uint8_t computePosition(volatile uint16_t &ticksFromOpen,
                                  volatile uint16_t &ticksReverse) const;

          // Precision % threshold above which proportional mode is not possible.
          // This is partly determined by some of the calculations and
          // tolerances in the dead reckoning.
          // Should be high enough to allow as low as 8 or 9 pulses
          // from one end of travel to the other (in the quickest direction).
          static constexpr uint8_t max_usuable_precision = 15;
          // Precision % used to indicate an error condition (legal but clearly no good).
          static constexpr uint8_t bad_precision = 100;
          // If true, device cannot be run in proportional mode.
          bool cannotRunProportional() const
              { return(approxPrecisionPC > max_usuable_precision); }
        };

  private:
    // Calibration parameters gathered/computed from the calibration step.
    // Logically read-only other than during (re)calibration.
    CalibrationParameters cp;

    // Set when valve needs (re)calibration, eg because dead-reckoning found to be significantly wrong.
    // May also need recalibrating after (say) a few weeks
    // to allow for battery/speed droop.
    // Possibly ignore tracking errors for a minimum interval.
    // May simply switch to 'binary' on/off mode if the calibration is off.
    bool needsRecalibrating = true;

    // Report an apparent serious tracking error that will force recalibration.
    // Such a recalibration may not happen immediately.
    void reportTrackingError()
        {
        needsRecalibrating = true;
#ifdef OTV0P2BASE_ErrorReport_DEFINED
        // Report a warning since indicates problem with valve or algo,
        // and implies excess valve noise and energy consumption.
        // Report a warning rather than an error since recoverable.
        OTV0P2BASE::ErrorReporter.set(OTV0P2BASE::ErrorReport::WARN_VALVE_TRACKING);
#endif
        }

    // Current sub-cycle ticks from fully-open (reference) end of travel, towards fully closed.
    // This is nominally ticks in the open-to-closed direction
    // since those may differ from the other direction.
    // Reset during calibration and upon hitting an end-stop.
    // Recalibration, full or partial, may be forced
    // if this overflows or underflows significantly.
    // Significant underflow might be (say) the minimum valve-open percentage.
    // ISR-/thread- safe with a mutex.
    volatile uint16_t ticksFromOpen;
    // Reverse ticks not yet folded into ticksFromOpen;
    volatile uint16_t ticksReverse;
    // Maximum permitted value of ticksFromOpen (and ticksReverse).
    static const uint16_t MAX_TICKS_FROM_OPEN = uint16_t(~0);

//    // True if using positional/shaft encoder, else using crude dead-reckoning.
//    // Only defined once calibration is complete.
//    bool usingPositionalShaftEncoder() const { return(false); }

    // Compute and apply reconciliation/adjustment of ticks and intermediate position.
    // Uses computePosition() to compute new internal position.
    // Call after moving the valve in normal mode, eg by dead reckoning.
    // Does not ever move logically right to the end-stops:
    // use hitEndStop() for that,
    // Does nothing if calibration is not in place.
    virtual void recomputeIntermediatePosition() override
        {
        if(!needsRecalibrating)
            {
            currentPC = OTV0P2BASE::fnconstrain(
                cp.computePosition(ticksFromOpen, ticksReverse),
                    uint8_t(1), uint8_t(99));
            }
        }

    // Reset internal position markers when an end-stop is hit.
    virtual void hitEndstop(const bool hitEndstopOpen) override
        {
        resetCurrentPC(hitEndstopOpen);
        ticksReverse = 0;
        ticksFromOpen = hitEndstopOpen ? 0 : cp.getTicksFromOpenToClosed();
        }

  protected:
    // Do valveCalibrating for proportional drive; returns true to return from poll() immediately.
    // Calls changeState() directly if it needs to change state.
    // If this returns false, processing falls through to that
    // for the non-proportional case.
    // Does nothing in the non-proportional-only implementation.
    virtual bool do_valveCalibrating_prop() override;

    // Do valveNormal start for proportional drive; returns true to return from poll() immediately.
    // Falls through to drive to end stops, or when in run-time binary-only mode.
    // Calls changeState() directly if it needs to change state.
    // If this returns false, processing should fall through to
    // that for the non-proportional case.
    virtual bool do_valveNormal_prop() override;

  public:
    using CurrentSenseValveMotorDirectBinaryOnly::CurrentSenseValveMotorDirectBinaryOnly;

    // Get estimated minimum percentage open for significant flow for this device; strictly positive in range [1,99].
    virtual uint8_t getMinPercentOpen() const override;

    // Called when encountering leading edge of a mark in the shaft rotation in forward direction (falling edge in reverse).
    // Can be called while run() is in progress.
    // Is ISR-/thread- safe.
    virtual void signalShaftEncoderMarkStart(bool /*opening*/) override { /* TODO */ }

    // Called with each motor run sub-cycle tick.
    // FIXME: is ISR-/thread- safe ***on AVR only*** currently.
    virtual void signalRunSCTTick(bool opening) override;

    // True if (re)calibration should be deferred.
    // Potentially an expensive call in time and energy.
    // Primarily public to allow whitebox unit testing.
    bool shouldDeferCalibration();

    // If true, proportional mode is never used and the valve is run to end stops instead.
    // Primarily public to allow whitebox unit testing.
    // Always false in this proportional implementation.
    virtual bool isNonProportionalOnly() const override { return(false); }

    // If true, proportional mode is not being used and the valve is run to end stops instead.
    // Allows proportional-mode driver to fall back
    // to simpler behaviour in case of difficulties.
    bool inNonProportionalMode() const
        { return(needsRecalibrating || cp.cannotRunProportional()); }

    // Get (read-only) calibration parameters, primarily for testing.
    CalibrationParameters const &_getCP() const { return(cp); }
  };


}

#endif /* ARDUINO_LIB_OTRADVALVE_CURRENTSENSEVALVEMOTORDIRECT_H_ */
