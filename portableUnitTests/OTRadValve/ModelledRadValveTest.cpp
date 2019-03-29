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

Author(s) / Copyright (s): Damon Hart-Davis 2016--2018
                           Deniz Erbilgin   2017
*/

/*
 * OTRadValve ModelledRadValve tests.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>

#include <OTV0P2BASE_QuickPRNG.h>
#include "OTRadValve_AbstractRadValve.h"
#include "OTRadValve_ModelledRadValve.h"


// Test for basic implementation of turn-up to/from turn-down delay to reduce valve hunting.
// Except when trying to respond as quickly as possible to a BAKE request,
// the valve should resist changing directions (between open/close) too quickly.
// That is, check that pauses between turn up and turn down are enforced.
TEST(ModelledRadValve,UpDownDelay)
{
    for(int ub = 0; ub <= 1; ++ub)
        {
        const bool useBAKE = (0 != ub);

        OTRadValve::ModelledRadValveState<> rs;
        ASSERT_FALSE(rs.isFiltering);
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());

        // Attempt to cycle the valve back and forth between max open/closed.
        // Ensure that (without BAKE) there is a pause before reversing.

        // Start with the valve fully open.
        uint8_t valvePC = 100;
        // Set sensible ambient room temperature (18C) and target of much higher.
        OTRadValve::ModelledRadValveInputState is(18 << 4);
        is.targetTempC = 25;
        // Backfill entire temperature history to avoid filtering coming on.
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        ASSERT_FALSE(rs.isFiltering);
        // Valve should still be open fully.
        EXPECT_EQ(100, valvePC);
        // No turn up or turn down should yet be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());
        // Now set the target well below ambient, and spin again for a while.
        // The valve should be closed and exactly 100% of cumulative travel.
        is.targetTempC = 14;
        // Backfill entire temperature history to avoid filtering coming on.
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        ASSERT_FALSE(rs.isFiltering);
        // The valve should have started to close.
        const uint8_t vPC1 = valvePC;
        EXPECT_GT(100, valvePC);
        // Immediate open (turn up) should be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_TRUE(rs.dontTurnup());
        // Temporarily set the target well above ambient, and spin for a while.
        is.targetTempC = 32;
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        // The valve should not open, because turn-up is prohibited.
        EXPECT_EQ(vPC1, valvePC);
        // Immediate open (turn up) should still be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_TRUE(rs.dontTurnup());
        EXPECT_TRUE(rs.dontTurnup());

        if(useBAKE)
            {
            // Verify that BAKE can override turn-up prohibition.
            is.inBakeMode = true;
            rs.tick(valvePC, is, NULL);
            EXPECT_EQ(100, valvePC) << " valve should have fully opened for BAKE regardless of dontTurnup()";
            // Immediate open (turn up) should still nominally be prohibited.
            EXPECT_TRUE(rs.dontTurnup());
            // Turn down should now simultaneously be prohibited.
            EXPECT_TRUE(rs.dontTurndown());
            break;
            }

        // Resume lower temperature and valve close.
        is.targetTempC = 10;
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        ASSERT_FALSE(rs.isFiltering);
        // The valve should have resumed closing.
        if(vPC1 > 0) { EXPECT_GT(vPC1, valvePC); }
        for(int i = 20; (--i > 0) && (valvePC > 0); )
            { rs.tick(valvePC, is, NULL); ASSERT_FALSE(rs.isFiltering); }
        EXPECT_EQ(0, valvePC);
        // Immediate open (turn up) should still be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        for(int i = OTRadValve::DEFAULT_ANTISEEK_VALVE_REOPEN_DELAY_M+2; --i > 0; )
            { rs.tick(valvePC, is, NULL); ASSERT_FALSE(rs.isFiltering); }
        // No turn up or turn down should now be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());
        // Now set the target well above ambient again, and spin for a while.
        // The valve should be open and exactly 200% of cumulative travel.
        is.targetTempC = 27;
        // Backfill entire temperature history to avoid filtering coming on.
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        // The valve should have started to close.
        const uint8_t vPC2 = valvePC;
        EXPECT_LT(0, valvePC);
        ASSERT_FALSE(rs.isFiltering);
        // Temporarily set the target well below ambient, and spin for a while.
        is.targetTempC = 10;
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        // The valve should not close, because turn-down is prohibited.
        EXPECT_EQ(vPC2, valvePC);
        // Immediate close (turn down) should still be prohibited.
        EXPECT_TRUE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());
        // Resume higher temperature and valve close.
        is.targetTempC = 22;
        rs._backfillTemperatures(rs.computeRawTemp16(is));
        rs.tick(valvePC, is, NULL);
        ASSERT_FALSE(rs.isFiltering);
        // The valve should have resumed opening.
        if(vPC2 < 100) { EXPECT_LT(vPC2, valvePC); }
        for(int i = 20; (--i > 0) && (valvePC < 100); )
            { rs.tick(valvePC, is, NULL); ASSERT_FALSE(rs.isFiltering); }
        EXPECT_EQ(100, valvePC);
        // Immediate close (turn down) should now be prohibited.
        EXPECT_TRUE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());
        for(int i = OTRadValve::DEFAULT_ANTISEEK_VALVE_RECLOSE_DELAY_M+2; --i > 0; )
            { rs.tick(valvePC, is, NULL); ASSERT_FALSE(rs.isFiltering); }
        // No turn up or turn down should now be prohibited.
        EXPECT_FALSE(rs.dontTurndown());
        EXPECT_FALSE(rs.dontTurnup());
        }
}

// Test the basic behaviour of the cumulative movement counter.
// Try without backing valve (eg using built-in estimate)
// and backing valve(s) to test tracking of backing valve itself.
TEST(ModelledRadValve,cumulativeMovementPC)
{
    for(int v = 0; v < 2; ++v)
        {
        const bool noBackingValve = (0 == v);
SCOPED_TRACE(testing::Message() << "no backing valve " << noBackingValve);
        // Trivial mock valve.
        OTRadValve::RadValveMock rvm;
        // Select backing valve to use if any.
        OTRadValve::AbstractRadValve * const arv = noBackingValve ? NULL : &rvm;

        // Start with the valve fully open.
        const uint8_t initialValvePC = 100;
        uint8_t valvePC = initialValvePC;
        if(NULL != arv) { arv->set(initialValvePC); }

        // Set sensible ambient room temperature (18C),
        // with target much higher.
        OTRadValve::ModelledRadValveInputState is(18 << 4);
        is.targetTempC = 25;
        OTRadValve::ModelledRadValveState<> rs;
        // Spin on the tick for many hours' worth;
        // there is no need for the valve to move.
        for(int i = 1000; --i > 0; ) { rs.tick(valvePC, is, arv); }
        EXPECT_EQ(100, valvePC);
        EXPECT_EQ(0, rs.cumulativeMovementPC);
        if(NULL != arv) { EXPECT_NEAR(valvePC, arv->get(), 1) << "backing valve should be close"; }
        // Now set the target well below ambient, and spin for a while.
        // The valve should be closed,
        // with exactly 100% of cumulative travel.
        is.targetTempC = 10;
        for(int i = 1000; --i > 0; ) { rs.tick(valvePC, is, arv); }
        EXPECT_EQ(0, valvePC);
        EXPECT_EQ(100, rs.cumulativeMovementPC);
        if(NULL != arv) { EXPECT_NEAR(valvePC, arv->get(), 1) << "backing valve should be close"; }
        // Now set the target well above ambient again, and spin for a while.
        // The valve should be open. with exactly 200% of cumulative travel.
        is.targetTempC = 26;
        for(int i = 1000; --i > 0; ) { rs.tick(valvePC, is, arv); }
        EXPECT_EQ(100, valvePC);
        EXPECT_EQ(200, rs.cumulativeMovementPC);
        if(NULL != arv) { EXPECT_NEAR(valvePC, arv->get(), 1) << "backing valve should be close"; }
        }
    }

// Simple test of integration of ModelledRadValve and underlying components.
// This is a mini-integration test to look for eg glue-logic issues.
// In particular this would have caught a prior serious bug where
// something inappropriate (the temperature target) was overwriting
// the % open value, and may catch other similar gross errors.
namespace MRVEI
    {
    // Instances with linkage to support the test.
    static OTRadValve::ValveMode valveMode;
    static OTV0P2BASE::TemperatureC16Mock roomTemp;
    static OTRadValve::TempControlSimpleVCP<OTRadValve::DEFAULT_ValveControlParameters> tempControl;
    static OTV0P2BASE::PseudoSensorOccupancyTracker occupancy;
    static OTV0P2BASE::SensorAmbientLightAdaptiveMock ambLight;
    static OTRadValve::NULLActuatorPhysicalUI physicalUI;
    static OTRadValve::NULLValveSchedule schedule;
    static OTV0P2BASE::NULLByHourByteStats byHourStats;
    }
TEST(ModelledRadValve,MRVExtremesInt)
{
    // Reset static state to make tests re-runnable.
    MRVEI::valveMode.setWarmModeDebounced(false);
    MRVEI::roomTemp.set(OTV0P2BASE::TemperatureC16Mock::DEFAULT_INVALID_TEMP);
    MRVEI::occupancy.reset();
    MRVEI::ambLight.set(0, 0, false);

    // Simple-as-possible instance.
    typedef OTRadValve::DEFAULT_ValveControlParameters parameters;
    OTRadValve::ModelledRadValveComputeTargetTempBasic<
       parameters,
        &MRVEI::valveMode,
        decltype(MRVEI::roomTemp),                    &MRVEI::roomTemp,
        decltype(MRVEI::tempControl),                 &MRVEI::tempControl,
        decltype(MRVEI::occupancy),                   &MRVEI::occupancy,
        decltype(MRVEI::ambLight),                    &MRVEI::ambLight,
        decltype(MRVEI::physicalUI),                  &MRVEI::physicalUI,
        decltype(MRVEI::schedule),                    &MRVEI::schedule,
        decltype(MRVEI::byHourStats),                 &MRVEI::byHourStats
        > cttb;
    OTRadValve::ModelledRadValve mrv
      (
      &cttb,
      &MRVEI::valveMode,
      &MRVEI::tempControl,
      NULL // No physical valve behind this test.
      );

    // Check a few parameters for sanity before the tests proper.
    EXPECT_FALSE(mrv.inGlacialMode());
    EXPECT_FALSE(mrv.isInErrorState());
    EXPECT_TRUE(mrv.isInNormalRunState());

    // Set up a room well below temperature, but occupied and light,
    // with the device in WARM mode.
    MRVEI::valveMode.setWarmModeDebounced(true);
    MRVEI::roomTemp.set(parameters::FROST << 4);
    MRVEI::occupancy.markAsOccupied();
    MRVEI::ambLight.set(255, 0, false);
    // Spin for at most a few minutes (at one tick per minute)
    // and the valve should be fully open.
    for(int i = 10; --i > 0; ) { mrv.read(); }
    EXPECT_EQ(100, mrv.get());

    // Bring the room well over temperature, still occupied and light,
    // and still in WARM mode.
    MRVEI::roomTemp.set((parameters::TEMP_SCALE_MAX + 1) << 4);
    // Spin for some minutes (at one tick per minute)
    // and the valve should be fully closed.
    // This may take longer than the first response
    // because of filtering and movement reduction algorithms.
    for(int i = 30; --i > 0; ) { mrv.read(); }
    EXPECT_EQ(0, mrv.get());

    // Bring the room well below temperature, still occupied and light,
    // and still in WARM mode.
    MRVEI::roomTemp.set((parameters::TEMP_SCALE_MIN - 1) << 4);
    // Spin for some minutes (at one tick per minute)
    // and the valve should be fully open.
    // This may take longer than the first response
    // because of filtering and movement reduction algorithms.
    for(int i = 30; --i > 0; ) { mrv.read(); }
    EXPECT_EQ(100, mrv.get());
}

// Test the logic in ModelledRadValveState for starting from extreme positions.
//
// Adapted 2016/10/16 from test_VALVEMODEL.ino testMRVSExtremes().
TEST(ModelledRadValve,MRVSExtremes)
{
    // If true then be more verbose.
    const static bool verbose = false;

    // Test that if the real temperature is zero
    // and the initial valve position is anything less than 100%
    // then after one tick (with mainly defaults)
    // that the valve is being opened (and more than glacially),
    // ie that when below any possible legal target FROST/WARM/BAKE temperature
    // the valve will open monotonically,
    // and also test that the fully-open state is reached
    // in a bounded number of ticks ie in bounded (and reasonable) time.
    // 'Reasonable' being about the default same minimum on time.
    static constexpr uint8_t maxNormalFullResponseMins =
        OTRadValve::DEFAULT_ANTISEEK_VALVE_REOPEN_DELAY_M;
    if(verbose) { fprintf(stderr, "open...\n"); }
    OTRadValve::ModelledRadValveInputState is0(0);
    is0.targetTempC = OTV0P2BASE::randRNG8NextBoolean() ? 5 : 25;
    OTRadValve::ModelledRadValveState<> rs0;
    const uint8_t valvePCOpenInitial0 = OTV0P2BASE::randRNG8() % 100;
    volatile uint8_t valvePCOpen = valvePCOpenInitial0;
    // Must fully open in reasonable time.
    for(int i = maxNormalFullResponseMins; --i >= 0; )
        {
        // Simulates one minute on each iteration.
        // Futz some input parameters that should not matter.
        is0.widenDeadband = OTV0P2BASE::randRNG8NextBoolean();
        is0.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
        const uint8_t oldValvePos = valvePCOpen;
        rs0.tick(valvePCOpen, is0, NULL);
        const uint8_t newValvePos = valvePCOpen;
        ASSERT_TRUE(newValvePos > 0);
        ASSERT_TRUE(newValvePos <= 100);
        ASSERT_TRUE(newValvePos > oldValvePos);
        // Should open to at least minimum-really-open-% on first step.
        if(oldValvePos < is0.minPCReallyOpen)
            { ASSERT_TRUE(is0.minPCReallyOpen <= newValvePos); }
//        ASSERT_TRUE(rs0.valveMoved == (oldValvePos != newValvePos));
        if(100 == newValvePos) { break; }
        }
    ASSERT_EQ(100, valvePCOpen);
    ASSERT_EQ(100 - valvePCOpenInitial0, rs0.cumulativeMovementPC);

    // Equally test that if the temperature is much higher than any legit target
    // the valve will monotonically close to 0% in bounded time.
    // Check for superficially correct linger behaviour where supported:
    //   * minPCOpen-1 % must be hit (lingering close)
    //     if starting anywhere above that.
    //   * Once in linger all reductions should be by 1%
    //     until possible final jump to 0.
    //   * Check that linger was long enough
    //     (if linger threshold is higher enough to allow it).
    // Also check for some correct initialisation
    // and 'velocity'/smoothing behaviour.
    if(verbose) { fprintf(stderr, "close...\n"); }
    OTRadValve::ModelledRadValveInputState is1(100<<4);
    is1.targetTempC = OTV0P2BASE::randRNG8NextBoolean() ? 5 : 25;
    OTRadValve::ModelledRadValveState<> rs1;
    ASSERT_TRUE(!rs1.initialised); // Initialisation not yet complete.
    const uint8_t valvePCOpenInitial1 = 1 + (OTV0P2BASE::randRNG8() % 100);
    valvePCOpen = valvePCOpenInitial1;
    const bool lookForLinger = rs1.SUPPORT_LINGER &&
        (valvePCOpenInitial1 >= is1.minPCReallyOpen);
    bool hitLinger = false; // True if the linger value was hit.
    uint8_t lingerMins = 0; // Approx mins spent in linger.
    // Must fully close in reasonable time.
    for(int i = maxNormalFullResponseMins; --i >= 0; )
        {
        // Simulates one minute on each iteration.
        // Futz some input parameters that should not matter.
        is1.widenDeadband = OTV0P2BASE::randRNG8NextBoolean();
        is1.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
        const uint8_t oldValvePos = valvePCOpen;
        rs1.tick(valvePCOpen, is1, NULL);
        const uint8_t newValvePos = valvePCOpen;
        EXPECT_TRUE(rs1.initialised); // Initialisation must have completed.
        EXPECT_TRUE(newValvePos < 100);
        EXPECT_TRUE(newValvePos < oldValvePos);
        if(hitLinger) { ++lingerMins; }
        if(hitLinger && (0 != newValvePos)) { ASSERT_EQ(oldValvePos - 1, newValvePos); }
        if(newValvePos == is1.minPCReallyOpen-1) { hitLinger = true; }
//        ASSERT_TRUE(rs1.valveMoved == (oldValvePos != newValvePos));
        if(0 == newValvePos) { break; }
        }
    EXPECT_EQ(0, valvePCOpen);
    EXPECT_EQ(valvePCOpenInitial1, rs1.cumulativeMovementPC);
    if(rs1.SUPPORT_LINGER)
        { EXPECT_TRUE(hitLinger == lookForLinger); }
    if(lookForLinger)
        { EXPECT_GE(lingerMins, OTV0P2BASE::fnmin(is1.minPCReallyOpen, OTRadValve::DEFAULT_MAX_RUN_ON_TIME_M)) << ((int)is1.minPCReallyOpen); }
    // Filtering should not have been engaged
    // and velocity should be zero (temperature is flat).
    for(int i = OTRadValve::ModelledRadValveState<>::filterLength; --i >= 0; )
        { ASSERT_EQ(100<<4, rs1.prevRawTempC16[i]); }
    EXPECT_EQ(100<<4, rs1.getSmoothedRecent());
    //  AssertIsEqual(0, rs1.getVelocityC16PerTick());
    EXPECT_TRUE(!rs1.isFiltering);
}

// Test the logic in ModelledRadValveState for starting from extreme positions.
// Checks that outside the proportional range
// the valve is driven immediately fully closed or fully open.
//
// Adapted 2016/10/16 from test_VALVEMODEL.ino testMRVSExtremes().
TEST(ModelledRadValve,MRVSExtremes2)
{
    // Try a range of (whole-degree) offsets...
    static constexpr uint8_t maxOffset = OTV0P2BASE::fnmax(10, 2*OTRadValve::ModelledRadValveState<>::_proportionalRange);
    for(int offset = -maxOffset; offset <= +maxOffset; ++offset)
        {
SCOPED_TRACE(testing::Message() << "offset " << offset);
        for(int w = 0; w <= 1; ++w)
            {
            const bool wide = (0 != w);
SCOPED_TRACE(testing::Message() << "wide " << wide);
            OTRadValve::ModelledRadValveInputState is(100<<4);
            is.targetTempC = 19;
            is.setReferenceTemperatures(int_fast16_t(is.targetTempC + offset) << 4);
            // Futz the wide deadband parameter by default.
            is.widenDeadband = wide;
            // Well outside the potentially-proportional range,
            // valve should unconditionally be driven immediately off/on
            // by gross temperature error.
            if(OTV0P2BASE::fnabs(offset) > OTRadValve::ModelledRadValveState<>::_proportionalRange)
                {
                // Where adjusted reference temperature is (well) below target,
                // valve should be driven open.
                OTRadValve::ModelledRadValveState<> rs3a;
                uint8_t valvePCOpen = 0;
                rs3a.tick(valvePCOpen, is, NULL);
                EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 1);
                // Where adjusted reference temperature is (well) above target,
                // valve should be driven closed.
                OTRadValve::ModelledRadValveState<> rs3b;
                valvePCOpen = 100;
                rs3b.tick(valvePCOpen, is, NULL);
                EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 1);
                continue;
                }

            // Somewhat outside the normal expected deadband (<= 1C)
            // valve should (eventually) be driven fully on/off,
            // regardless of wide deadband setting,
            // as long as not filtering (which would push up the top end)
            // and as long as no non-setback temperature is set (likewise).
            if(OTV0P2BASE::fnabs(offset) > 1)
                {
                static constexpr uint8_t maxResponseMins = 100;
                OTRadValve::ModelledRadValveState<> rs3a;
                uint8_t valvePCOpen = 0;
                for(int i = maxResponseMins; --i >= 0; )
                    { rs3a.tick(valvePCOpen, is, NULL); }
                EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 2);
                // Where adjusted reference temperature is (well) above target,
                // valve should be driven closed.
                OTRadValve::ModelledRadValveState<> rs3b;
                valvePCOpen = 100;
                for(int i = maxResponseMins; --i >= 0; )
                    { rs3b.tick(valvePCOpen, is, NULL); }
                EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 2);
                continue;
                }

            // Just outside the normal expected deadband (<= 1C)
            // valve should (eventually) be driven fully on/off,
            // regardless of wide deadband setting,
            // as long as not filtering (which would push up the top end)
            // and as long as no non-setback temperature is set (likewise).
            if(OTV0P2BASE::fnabs(offset) > 0)
                {
                static constexpr uint8_t maxResponseMins = 100;
                OTRadValve::ModelledRadValveState<> rs3a;
                uint8_t valvePCOpen = 0;
                for(int i = maxResponseMins; --i >= 0; )
                    { rs3a.tick(valvePCOpen, is, NULL); }
                EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 2);
                // Where adjusted reference temperature is (well) above target,
                // valve should be driven closed.
                OTRadValve::ModelledRadValveState<> rs3b;
                valvePCOpen = 100;
                for(int i = maxResponseMins; --i >= 0; )
                    { rs3b.tick(valvePCOpen, is, NULL); }
                // When very close from above,
                // it is enough to get below the boiler call-for-heat threshold.
                if(wide && (1 == offset))
                    { EXPECT_GT(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen); }
                else
                    { EXPECT_NEAR((offset < 0) ? 100 : 0, valvePCOpen, 2); }
                continue;
                }
            }
        }
}

// Test of ModelledRadValveComputeTargetTempBasic algorithm for computing the target temperature.
namespace MRVCTTB
    {
    // Instances with linkage to support the test.
    static OTRadValve::ValveMode valveMode;
    static OTV0P2BASE::TemperatureC16Mock roomTemp;
    static OTRadValve::TempControlSimpleVCP<OTRadValve::DEFAULT_ValveControlParameters> tempControl;
    static OTV0P2BASE::PseudoSensorOccupancyTracker occupancy;
    static OTV0P2BASE::SensorAmbientLightAdaptiveMock ambLight;
    static OTRadValve::NULLActuatorPhysicalUI physicalUI;
    static OTRadValve::NULLValveSchedule schedule;
    static OTV0P2BASE::NULLByHourByteStats byHourStats;
    }
TEST(ModelledRadValve,ModelledRadValveComputeTargetTempBasic)
{
    // Reset static state to make tests re-runnable.
    MRVCTTB::valveMode.setWarmModeDebounced(false);
    MRVCTTB::roomTemp.set(OTV0P2BASE::TemperatureC16Mock::DEFAULT_INVALID_TEMP);
    MRVCTTB::occupancy.reset();
    MRVCTTB::ambLight.set(0, 0, false);

    // Simple-as-possible instance.
    OTRadValve::ModelledRadValveComputeTargetTempBasic<
        OTRadValve::DEFAULT_ValveControlParameters,
        &MRVCTTB::valveMode,
        decltype(MRVCTTB::roomTemp),                    &MRVCTTB::roomTemp,
        decltype(MRVCTTB::tempControl),                 &MRVCTTB::tempControl,
        decltype(MRVCTTB::occupancy),                   &MRVCTTB::occupancy,
        decltype(MRVCTTB::ambLight),                    &MRVCTTB::ambLight,
        decltype(MRVCTTB::physicalUI),                  &MRVCTTB::physicalUI,
        decltype(MRVCTTB::schedule),                    &MRVCTTB::schedule,
        decltype(MRVCTTB::byHourStats),                 &MRVCTTB::byHourStats
        > cttb0;
    EXPECT_FALSE(MRVCTTB::valveMode.inWarmMode());
    const uint8_t f = OTRadValve::DEFAULT_ValveControlParameters::FROST;
    EXPECT_EQ(f, cttb0.computeTargetTemp()) << "should start in FROST mode";
    MRVCTTB::valveMode.setWarmModeDebounced(true);
    EXPECT_TRUE(MRVCTTB::occupancy.isLikelyUnoccupied());
    const uint8_t w = OTRadValve::DEFAULT_ValveControlParameters::WARM;
    EXPECT_GT(w, cttb0.computeTargetTemp()) << "no signs of activity";
    // Signal some occupancy.
    MRVCTTB::occupancy.markAsOccupied();
    EXPECT_FALSE(MRVCTTB::occupancy.isLikelyUnoccupied());
    // Should now be at WARM target.
    EXPECT_EQ(w, cttb0.computeTargetTemp());
    // Make the room light.
    MRVCTTB::ambLight.set(255, 0, false);
    MRVCTTB::ambLight.read();
    EXPECT_FALSE(MRVCTTB::ambLight.isRoomDark());
    EXPECT_EQ(0, MRVCTTB::ambLight.getDarkMinutes());
    EXPECT_EQ(w, cttb0.computeTargetTemp());
    // Mark long-term vacancy with holiday mode.
    MRVCTTB::occupancy.setHolidayMode();
    EXPECT_GT(w, cttb0.computeTargetTemp()) << "holiday mode should allow setback";
    // Make the room dark (and marked as dark for a long time).
    MRVCTTB::ambLight.set(0, 12*60U, false);
    MRVCTTB::ambLight.read();
    EXPECT_TRUE(MRVCTTB::ambLight.isRoomDark());
    EXPECT_NEAR(12*60U, MRVCTTB::ambLight.getDarkMinutes(), 1);
    const uint8_t sbFULL = OTRadValve::DEFAULT_ValveControlParameters::SETBACK_FULL;
    EXPECT_EQ(w-sbFULL, cttb0.computeTargetTemp()) << "room dark for a reasonable time AND holiday mode should allow full setback";
    MRVCTTB::valveMode.startBake();
    const uint8_t bu = OTRadValve::DEFAULT_ValveControlParameters::BAKE_UPLIFT;
    EXPECT_EQ(w+bu, cttb0.computeTargetTemp()) << "BAKE should win and force full uplift from WARM";
}

// Test the logic in ModelledRadValveState to open fast from well below target (TODO-593).
// This is to cover the case where the use manually turns on/up the valve
// and expects quick response from the valve and the remote boiler
// (which may require >= DEFAULT_VALVE_PC_MODERATELY_OPEN to start).
// This relies on no widened deadband being set.
// It may also require filtering (from gyrating temperatures) not to be on.
//
// Adapted 2016/10/16 from test_VALVEMODEL.ino testMRVSOpenFastFromCold593().
TEST(ModelledRadValve,MRVSOpenFastFromCold593)
{
    // Test that if the ambient temperature is several degrees below target
    // and the initial valve position is 0/closed
    // (or any below OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN)
    // then after one tick
    // that the valve is open to at least DEFAULT_VALVE_PC_MODERATELY_OPEN.
    // Starting temp >5C below target.
    // Should work with or without explicitly requesting as fast response.
    for(int fr = 0; fr <= 1; ++fr)
        {
        const bool fastResponse = (0 != fr);
SCOPED_TRACE(testing::Message() << "fastResponse " << fastResponse);
        OTRadValve::ModelledRadValveInputState is0(10 << 4);
        is0.targetTempC = 18; // Modest target temperature.
        OTRadValve::ModelledRadValveState<> rs0;
        is0.fastResponseRequired = fastResponse;
        is0.widenDeadband = false;
        volatile uint8_t valvePCOpen = OTV0P2BASE::randRNG8() % OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN;
        // Futz some input parameters that should not matter.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean();
        is0.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
        // Run the algorithm one tick.
        rs0.tick(valvePCOpen, is0, NULL);
        const uint8_t newValvePos = valvePCOpen;
        EXPECT_GE(newValvePos, OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN);
        EXPECT_LE(newValvePos, 100);
    //    EXPECT_TRUE(rs0.valveMoved);
        if(rs0.eventsSupported) { EXPECT_EQ(OTRadValve::ModelledRadValveState<>::MRVE_OPENFAST, rs0.getLastEvent()); }
        }
}

// Test normal speed to open/close when already reasonably close to target.
// Test with and without explicit request for fast response.
// Note that full close may not be needed once not calling for heat,
// which may in principle save as much as 50% of movement.
TEST(ModelledRadValve,MRVSNormalResponseTime)
{
    for(int d = 0; d <= 1; ++d)
        {
        const bool below = (0 != d);
SCOPED_TRACE(testing::Message() << "below " << below);
        for(int fr = 0; fr <= 1; ++fr)
            {
            const bool fastResponseRequired = (0 != fr);
SCOPED_TRACE(testing::Message() << "fastResponseRequired " << fastResponseRequired);
            // Modest target temperature.
            const uint8_t targetTempC = 18;
            // Have ambient temperature a little way from target.
            const int16_t oC16 = OTRadValve::ModelledRadValveInputState::refTempOffsetC16;
            const int16_t ambientTempC16 = (targetTempC << 4) +
                (below ? -(oC16-1) : +(oC16-1));
            OTRadValve::ModelledRadValveInputState is0(ambientTempC16);
            OTRadValve::ModelledRadValveState<> rs0;
            is0.targetTempC = targetTempC;
            is0.glacial = false;
            is0.widenDeadband = false;
            is0.fastResponseRequired = fastResponseRequired;
            // Start in some non-extreme position
            // too far to meet fast response goals if glacial.
            const uint8_t valvePCOpenInitial = 50;
            volatile uint8_t valvePCOpen = valvePCOpenInitial;
            // Futz some input parameters that should not matter.
            rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean();
            is0.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
            // Check that target is not reached in a single tick.
            rs0.tick(valvePCOpen, is0, NULL);
//            EXPECT_NE(below ? 100 : 0, valvePCOpen);
            // Ensure that after a bounded time valve is fully open/closed.
            // Time limit is much lower when a fast response is requested.
            // Units are nominally minutes.
            // This should never take longer than 'glacial' 1% per tick.
            const uint8_t timeLimit = fastResponseRequired ?
                rs0.fastResponseTicksTarget-1 : 100;
            for(int i = 0; i < timeLimit; ++i)
                { rs0.tick(valvePCOpen, is0, NULL); }
            // Nominally expect valve to be completely open/closed,
            // but allow for nearly-fully open (strong-call-for-heat_
            // and 'below call-for-heat'
            // for some algorithm variants.
            if(below)
                { EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN, valvePCOpen) << "moved " << (valvePCOpen - valvePCOpenInitial); }
            else
                { EXPECT_GE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen) << "moved " << (valvePCOpen - valvePCOpenInitial); }
            }
        }
}

// Test that valve does not hover indefinitely with boiler on unless full open.
// This is to avoid futile/expensive/noisy running of boiler indefinitely
// with the valve at a steady temperature (close to target),
// possibly not actually letting water through or getting any heat.
// This tests the valve at a range of temperatures around the target
// to ensure that with steady temperatures
// the call for heat eventually stops,
// or that the call for heat continues
// but with valve fully open.  (TODO-1096)
// Tested with and without wide deadband.
// The legacy algorithm pre 2016/12/30 fails this test
// and can hover badly potentially fail to heat anything but the valve.
TEST(ModelledRadValve,MRVSNoHoverWithBoilerOn)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Modest target temperature.
    const uint8_t targetTempC = 19;
    // Temperature range / max offset in each direction in C.
    const uint8_t tempMaxOffsetC =
        OTV0P2BASE::fnmax(10,
        2+OTRadValve::ModelledRadValveState<>::_proportionalRange);
    ASSERT_GT(targetTempC, tempMaxOffsetC) << "avoid underflow to < 0C";
    for(int16_t ambientTempC16 = (targetTempC - (int)tempMaxOffsetC) << 4;
        ambientTempC16 <= (targetTempC + (int)tempMaxOffsetC) << 4;
        ++ambientTempC16)
        {
        OTRadValve::ModelledRadValveInputState is0(ambientTempC16);
        OTRadValve::ModelledRadValveState<> rs0;
        is0.targetTempC = targetTempC;
        // Futz some input parameters that should not matter.
        is0.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
        is0.fastResponseRequired = OTV0P2BASE::randRNG8NextBoolean();
        // Randomly try with/out wide deadband; may matter, though should not.
        is0.widenDeadband = OTV0P2BASE::randRNG8NextBoolean();
        // Randomly try with/out glacial; may matter, though should not.
        is0.glacial = OTV0P2BASE::randRNG8NextBoolean();
        // Shouldn't be sensitive to initial filtering state.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean();
        // Start valve in a random position.
        const uint8_t valvePCOpenInitial = unsigned(random()) % 101;
        volatile uint8_t valvePCOpen = valvePCOpenInitial;
        // Run for long enough even for glacial traverse of valve range.
        for(int i = 0; i < 100; ++i) { rs0.tick(valvePCOpen, is0, NULL); }
        // Make sure either fully open, or not calling for heat.
        const uint8_t p = valvePCOpen;
        const bool callForHeat = (p >= OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN);
        EXPECT_TRUE((100 == p) || !callForHeat) << int(p);
        // If ambient is (well) above target then there must be no call for heat.
        if(ambientTempC16 > ((targetTempC + 1) << 4))
            { EXPECT_FALSE(callForHeat) << int(p); }
        }
}


// Check for correct engage/disengage of the filtering.
// In particular check that there is no flapping on/off eg when current ~ mean.
TEST(ModelledRadValve,MRVSFilteringOnOff)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Modest target temperature.
    const uint8_t targetTempC = 18;
    const int_fast16_t ambientTempC16 = targetTempC << 4;

    // Start in a random position.
    const uint8_t valvePCOpenInitial = unsigned(random()) % 101;
    volatile uint8_t valvePCOpen = valvePCOpenInitial;

    OTRadValve::ModelledRadValveInputState is0(ambientTempC16);
    OTRadValve::ModelledRadValveState<> rs0;
    ASSERT_FALSE(rs0.isFiltering) << "filtering must be off before first tick";
    is0.targetTempC = targetTempC;
    is0.glacial = false;
    is0.fastResponseRequired = false;
    // Futz some input parameters that should not matter.
    is0.hasEcoBias = OTV0P2BASE::randRNG8NextBoolean();
    is0.widenDeadband = OTV0P2BASE::randRNG8NextBoolean();
    // Mess with state of filtering before the tick; should not matter.
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    // After one tick of flat temperature values, filtering should be off.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);

    // Set the temperature values flat and tick again; filtering still off.
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    rs0._backfillTemperatures(ambientTempC16);
    is0.setReferenceTemperatures(ambientTempC16);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);

    // Check filtering NOT triggered by slowly rising or falling temperatures.
    // Rising...
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    rs0._backfillTemperatures(ambientTempC16);
    is0.setReferenceTemperatures(ambientTempC16);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    for(size_t i = 0; i < rs0.filterLength; ++i)
        {
        is0.setReferenceTemperatures(ambientTempC16 + i);
        rs0.tick(valvePCOpen, is0, NULL);
        EXPECT_FALSE(rs0.isFiltering);
        }
    // Falling...
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    rs0._backfillTemperatures(ambientTempC16);
    is0.setReferenceTemperatures(ambientTempC16);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    for(size_t i = 0; i < rs0.filterLength; ++i)
        {
        is0.setReferenceTemperatures(ambientTempC16 - i);
        rs0.tick(valvePCOpen, is0, NULL);
        EXPECT_FALSE(rs0.isFiltering);
        }

    // Check filtering triggered by fast rising or falling temperatures.
    // Pick delta just above chosen threshold.
    // Several ticks may be needed to engage the filtering.
    const uint8_t deltaH = (16 + (rs0.MIN_TICKS_1C_DELTA-1)) / rs0.MIN_TICKS_1C_DELTA;
    // Rising...
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    rs0._backfillTemperatures(ambientTempC16);
    is0.setReferenceTemperatures(ambientTempC16);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    for(size_t i = 0; i < rs0.filterLength; ++i)
        {
        is0.setReferenceTemperatures(ambientTempC16 + i*deltaH);
        rs0.tick(valvePCOpen, is0, NULL);
        }
    EXPECT_TRUE(rs0.isFiltering);
    // Falling...
    rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
    rs0._backfillTemperatures(ambientTempC16);
    is0.setReferenceTemperatures(ambientTempC16);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    for(size_t i = 0; i < rs0.filterLength; ++i)
        {
        is0.setReferenceTemperatures(ambientTempC16 - i*deltaH);
        rs0.tick(valvePCOpen, is0, NULL);
        }
    EXPECT_TRUE(rs0.isFiltering);

    if(rs0.FILTER_DETECT_JITTER)
        {
        // Check for filtering triggered by jittery temperature readings.
        // Set hugely-off point near one end; filtering should come on.
        const int16_t bigOffsetC16 = 5 << 4; // 5C perturbation.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
        rs0._backfillTemperatures(ambientTempC16);
        rs0.prevRawTempC16[2] += bigOffsetC16;
        rs0.tick(valvePCOpen, is0, NULL);
        // Should be able to see that mean is now very different to current temp.
        const uint8_t mtj = rs0.MAX_TEMP_JUMP_C16;
        EXPECT_GT(OTV0P2BASE::fnabsdiff(rs0.getSmoothedRecent(), ambientTempC16), mtj);
        EXPECT_TRUE(rs0.isFiltering);
        // Set hugely-off point near one end other way; filtering should come on.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
        rs0._backfillTemperatures(ambientTempC16);
        rs0.prevRawTempC16[2] -= bigOffsetC16;
        rs0.tick(valvePCOpen, is0, NULL);
        // Should be able to see that mean is now very different to current temp.
        EXPECT_GT(OTV0P2BASE::fnabsdiff(rs0.getSmoothedRecent(), ambientTempC16), mtj);
        EXPECT_TRUE(rs0.isFiltering);
        // Now set two hugely-off but opposite points.
        // Mean should barely be affected but filtering should stay on.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
        rs0._backfillTemperatures(ambientTempC16);
        rs0.prevRawTempC16[rs0.filterLength - 2] += bigOffsetC16;
        rs0.prevRawTempC16[2] -= bigOffsetC16;
        rs0.tick(valvePCOpen, is0, NULL);
        // Should be able to see that mean is unchanged.
        EXPECT_EQ(OTV0P2BASE::fnabsdiff(rs0.getSmoothedRecent(), ambientTempC16), 0);
        EXPECT_TRUE(rs0.isFiltering);
        // Reversing the direction should make no difference.
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean(); // Futz it.
        rs0._backfillTemperatures(ambientTempC16);
        rs0.prevRawTempC16[rs0.filterLength - 2] -= bigOffsetC16;
        rs0.prevRawTempC16[2] += bigOffsetC16;
        rs0.tick(valvePCOpen, is0, NULL);
        // Should be able to see that mean is unchanged.
        EXPECT_EQ(OTV0P2BASE::fnabsdiff(rs0.getSmoothedRecent(), ambientTempC16), 0);
        }
}

// Test that the cold draught detector works, with simple synthetic case.
// Check that a sufficiently sharp drop in temperature
// (when already below target temperature)
// inhibits further heating at least partly for a while.
// Note: in future there may exist variants with and without this detector.
TEST(ModelledRadValve,DraughtDetectorSimple)
{
    // If true then be more verbose.
    const static bool verbose = false;

    // Don't run the test if the option is not supported.
    if(!OTRadValve::ModelledRadValveState<>::SUPPORT_MRVE_DRAUGHT) { return; }

    // Run the test a few times to help ensure
    // that there is no dependency on the state of the PRNG, etc.
    for(int i = 8; --i >= 0; )
        {
        // Test that:
        // IF the real temperature is moderately-to-much below the target
        //   (allowing for any internal offsetting)
        //   and the initial valve position is anywhere [0,100]
        //   but the final temperature measurement shows a large drop
        //   (and ECO mode is enabled, and no fast response)
        // THEN after one tick
        //   the valve is open to less than DEFAULT_VALVE_PC_SAFER_OPEN
        //   to try to ensure no call for heat from the boiler.
        //
        // Starting temp as a little below target.
        const uint8_t targetC = OTRadValve::SAFE_ROOM_TEMPERATURE;
        const int_fast16_t roomTemp = (targetC << 4) - 15- (OTV0P2BASE::randRNG8() % 32);
if(verbose) { fprintf(stderr, "Start\n"); }
        OTRadValve::ModelledRadValveInputState is0(roomTemp);
        is0.targetTempC = targetC;
        OTRadValve::ModelledRadValveState<> rs0(is0);
        volatile uint8_t valvePCOpen = OTV0P2BASE::randRNG8() % 100;
if(verbose) { fprintf(stderr, "Valve %d%%.\n", valvePCOpen); }
        // Set necessary conditions to allow draught-detector.
        // (Not allowed to activate in comfort mode,
        // nor when user has just adjusted the controls.)
        is0.hasEcoBias = true;
        is0.fastResponseRequired = false;
        // Futz some input parameters that should not matter.
        is0.widenDeadband = OTV0P2BASE::randRNG8NextBoolean();
        rs0.isFiltering = OTV0P2BASE::randRNG8NextBoolean();
        // Set a new significantly lower room temp (drop >=0.5C), as if draught.
        const int_fast16_t droppedRoomTemp =
            roomTemp - 8 - (OTV0P2BASE::randRNG8() % 32);
        is0.setReferenceTemperatures(droppedRoomTemp);
        // Run the algorithm one tick.
        rs0.tick(valvePCOpen, is0, NULL);
if(verbose) { fprintf(stderr, "Valve %d%%.\n", valvePCOpen); }
        const uint8_t newValvePos = valvePCOpen;
        EXPECT_LT(newValvePos, OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN);
        ASSERT_EQ(OTRadValve::ModelledRadValveState<>::MRVE_DRAUGHT, rs0.getLastEvent());
        }
}

// Check expected valve response to one very small set of data points.
// These are manually interpolated from real world data (5s, ~20161231T1230).
//
// DHD20170117: in particular this should verify that
// filtering stays on long enough to carry when valve temps just below
// 'wellAboveTarget' threshold to let room cool gradually
// and not force the valve to close prematurely.
/*
{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":281,"H|%":65}
{"@":"E091B7DC8FEDC7A9","O":1,"vac|h":0,"B|cV":254}
{"@":"E091B7DC8FEDC7A9","L":37,"v|%":0,"tT|C":18}
{"@":"E091B7DC8FEDC7A9","tS|C":1,"vC|%":0,"gE":0}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":39}
{"@":"E091B7DC8FEDC7A9","v|%":100,"tT|C":19,"tS|C":0}
{"@":"E091B7DC8FEDC7A9","vC|%":100,"gE":0,"O":2}
{"@":"E091B7DC8FEDC7A9","H|%":67,"T|C16":280,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":52}
{"@":"E091B7DC8FEDC7A9","T|C16":284,"v|%":100,"L":49}
{"@":"E091B7DC8FEDC7A9","tT|C":19,"tS|C":0,"H|%":67}
{"@":"E091B7DC8FEDC7A9","T|C16":289,"vC|%":100}
{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":293,"H|%":67}
{"@":"E091B7DC8FEDC7A9","L":52,"O":2,"vac|h":0}
{"@":"E091B7DC8FEDC7A9","B|cV":254,"L":54,"v|%":100}
{"@":"E091B7DC8FEDC7A9","T|C16":302,"tT|C":19,"L":56}
{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":100,"gE":0}
{"@":"E091B7DC8FEDC7A9","T|C16":308,"H|%":65,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":60}
{"@":"E091B7DC8FEDC7A9","T|C16":314,"v|%":100,"L":66}
{"@":"E091B7DC8FEDC7A9","tT|C":19,"tS|C":0,"H|%":63}
{"@":"E091B7DC8FEDC7A9","T|C16":320,"vC|%":100}
{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":323,"H|%":62}
{"@":"E091B7DC8FEDC7A9","L":67,"O":2,"vac|h":0}
{"@":"E091B7DC8FEDC7A9","B|cV":254,"L":66,"v|%":100}
{"@":"E091B7DC8FEDC7A9","vC|%":151,"tT|C":19,"L":67}
{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":156,"gE":0}
{"@":"E091B7DC8FEDC7A9","T|C16":336,"H|%":60,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":115}
{"@":"E091B7DC8FEDC7A9","v|%":29,"tT|C":19,"tS|C":0}
{"@":"E091B7DC8FEDC7A9","vC|%":176,"gE":0,"H|%":59}
{"@":"E091B7DC8FEDC7A9","T|C16":344,"H|%":59,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":86}
{"@":"E091B7DC8FEDC7A9","v|%":0,"tT|C":19,"tS|C":0}
{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"H|%":58}
{"@":"E091B7DC8FEDC7A9","T|C16":346,"H|%":58,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":81}
{"@":"E091B7DC8FEDC7A9","L":68,"v|%":0,"tT|C":19}
{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":200,"gE":0}
{"@":"E091B7DC8FEDC7A9","L":57,"T|C16":346,"H|%":58}
{"@":"E091B7DC8FEDC7A9","O":2,"vac|h":0,"B|cV":254}
{"@":"E091B7DC8FEDC7A9","L":50,"v|%":0,"tT|C":19}
{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":200,"gE":0}
{"@":"E091B7DC8FEDC7A9","T|C16":344,"H|%":58,"O":2}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":56}
{"@":"E091B7DC8FEDC7A9","tT|C":18,"v|%":0,"tS|C":1}
{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"O":1}
{"@":"E091B7DC8FEDC7A9","T|C16":342,"H|%":58,"O":1}
{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":50}
{"@":"E091B7DC8FEDC7A9","v|%":0,"tT|C":18,"tS|C":1}
{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"L":47}
{"@":"E091B7DC8FEDC7A9","T|C16":339,"H|%":58,"O":1}
 */
TEST(ModelledRadValve,SampleValveResponse1)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Target temperature without setback.
    const uint8_t targetTempC = 19;

    // Valve starts fully shut.
    uint8_t valvePCOpen = 0;

    // Assume flat temperature before the sample started.
    //{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":281,"H|%":65}
    //{"@":"E091B7DC8FEDC7A9","O":1,"vac|h":0,"B|cV":254}
    //{"@":"E091B7DC8FEDC7A9","L":37,"v|%":0,"tT|C":18}
    //{"@":"E091B7DC8FEDC7A9","tS|C":1,"vC|%":0,"gE":0}
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":39}
    OTRadValve::ModelledRadValveInputState is0(281); // 281 ~ 17.6C.
    OTRadValve::ModelledRadValveState<> rs0;
    ASSERT_FALSE(rs0.isFiltering) << "filtering must be off before first tick";
    is0.fastResponseRequired = false;
    is0.hasEcoBias = true;

    // Non-set-back temperature.
    is0.maxTargetTempC = targetTempC;

    // Do one tick in quiescent state, set back one degree.
    is0.targetTempC = targetTempC - 1;
    is0.widenDeadband = true;
    is0.fastResponseRequired = false;
    // After tick, filtering should be off, valve still shut or nearly so.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(0, valvePCOpen, 2);

    // Turn light on, room occupied, setback goes, fast response required.
    is0.targetTempC = targetTempC;
    is0.widenDeadband = false;
    is0.fastResponseRequired = true;
    // After tick, filtering should be off.
    // Valve at least at/above call-for-heat threshold.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen) << int(valvePCOpen);
    // After a few more ticks, filtering still off, valve (near) fully open.
    rs0.tick(valvePCOpen, is0, NULL);
    rs0.tick(valvePCOpen, is0, NULL);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(100, valvePCOpen, 20);

    // Now respond to continuing occupancy, room below target temperature.
    // Valve not yet closing by the end of this phase.
    is0.targetTempC = targetTempC;
    is0.widenDeadband = false;
    is0.fastResponseRequired = false;

    //{"@":"E091B7DC8FEDC7A9","v|%":100,"tT|C":19,"tS|C":0}
    // ... carried temp from {"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":281,"H|%":65}
    // Temperatures below will be linearly interpolated where necessary.
    is0.setReferenceTemperatures(281);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","vC|%":100,"gE":0,"O":2}
    is0.setReferenceTemperatures(281);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","H|%":67,"T|C16":280,"O":2}
    is0.setReferenceTemperatures(282);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":52}
    is0.setReferenceTemperatures(283);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":284,"v|%":100,"L":49}
    is0.setReferenceTemperatures(284);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","tT|C":19,"tS|C":0,"H|%":67}
    is0.setReferenceTemperatures(287);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":289,"vC|%":100}
    is0.setReferenceTemperatures(290);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":293,"H|%":67}
    is0.setReferenceTemperatures(293);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","L":52,"O":2,"vac|h":0}
    is0.setReferenceTemperatures(296);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","B|cV":254,"L":54,"v|%":100}
    // Sometimes pretend that temp jumped enough here to trigger filtering,
    // else interpolate perfectly smooth rise harder to detect.
    is0.setReferenceTemperatures(OTV0P2BASE::randRNG8NextBoolean() ? 299 : 301);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    EXPECT_TRUE(rs0.isFiltering);
    //{"@":"E091B7DC8FEDC7A9","T|C16":302,"tT|C":19,"L":56}
    is0.setReferenceTemperatures(302);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":100,"gE":0}
    is0.setReferenceTemperatures(305);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","T|C16":308,"H|%":65,"O":2}
    is0.setReferenceTemperatures(308);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":60}
    is0.setReferenceTemperatures(311);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":314,"v|%":100,"L":66}
    is0.setReferenceTemperatures(314);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","tT|C":19,"tS|C":0,"H|%":63}
    is0.setReferenceTemperatures(317);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":320,"vC|%":100}
    is0.setReferenceTemperatures(320);
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(100, valvePCOpen, 15);
    //{"@":"E091B7DC8FEDC7A9","gE":0,"T|C16":323,"H|%":62}
    is0.setReferenceTemperatures(323);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","L":67,"O":2,"vac|h":0}
    is0.setReferenceTemperatures(326);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","B|cV":254,"L":66,"v|%":100}
    is0.setReferenceTemperatures(329); // ~20.6C
    rs0.tick(valvePCOpen, is0, NULL);
    // Valve still (near) fully open.
    EXPECT_NEAR(100, valvePCOpen, 15);
    const uint8_t v1 = valvePCOpen;
    EXPECT_NEAR(307, rs0.getSmoothedRecent(), 5); // 307 ~ 19.2C.
    // Filtering should now be on,
    EXPECT_TRUE(rs0.isFiltering);
    // DHD20170109: older code needed this to be propagated to widenDeadband.
//    is0.widenDeadband = rs0.isFiltering;

    // Valve is about to start closing...

    //{"@":"E091B7DC8FEDC7A9","vC|%":151,"tT|C":19,"L":67}
    is0.setReferenceTemperatures(332);
    rs0.tick(valvePCOpen, is0, NULL);
    // In trace valve had closed below call-for-heat threshold.
//    EXPECT_GT(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
    EXPECT_LT(0, valvePCOpen);
    //{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":156,"gE":0}
    is0.setReferenceTemperatures(334); // 334 ~ 20.9C.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(312, rs0.getSmoothedRecent(), 5); // 334 ~ 19.5C.
//    EXPECT_NEAR(44, valvePCOpen, 5);
    EXPECT_LT(0, valvePCOpen);
    const uint8_t v2 = valvePCOpen;
    EXPECT_GE(v1, v2) << "valve should not be re-opening";
    //{"@":"E091B7DC8FEDC7A9","T|C16":336,"H|%":60,"O":2}
    is0.setReferenceTemperatures(336);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":115}
    is0.setReferenceTemperatures(338);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","v|%":29,"tT|C":19,"tS|C":0}
    is0.setReferenceTemperatures(340);
    rs0.tick(valvePCOpen, is0, NULL);
    // Note that newer algorithms may result in slower/less closing by now.
//    EXPECT_NEAR(29, valvePCOpen, 20);
    EXPECT_LT(0, valvePCOpen);
    const uint8_t v3 = valvePCOpen;
    EXPECT_GE(v2, v3) << "valve should not be re-opening";
    //{"@":"E091B7DC8FEDC7A9","vC|%":176,"gE":0,"H|%":59}
    is0.setReferenceTemperatures(342);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":344,"H|%":59,"O":2}
    is0.setReferenceTemperatures(344); // 344 ~ 21.5C.
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":86}
    is0.setReferenceTemperatures(345);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","v|%":0,"tT|C":19,"tS|C":0}
    is0.setReferenceTemperatures(345); // 345 ~ 21.6C.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_NEAR(331, rs0.getSmoothedRecent(), 5); // 312 ~ 20.7C.
//    // Valve fully closed in original; must be below call-for-heat threshold.
//    EXPECT_GT(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
    //{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"H|%":58}
    is0.setReferenceTemperatures(346); // 346 ~ 21.6C.
    rs0.tick(valvePCOpen, is0, NULL);
//    // Valve fully closed in original; must be below call-for-heat threshold.
//    EXPECT_GT(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
    //{"@":"E091B7DC8FEDC7A9","T|C16":346,"H|%":58,"O":2}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
//    // Valve fully closed in original; must be below call-for-heat threshold.
//    EXPECT_GT(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
    const uint8_t v4 = valvePCOpen;
    EXPECT_GE(v3, v4) << "valve should not be re-opening";
    // Filtering still on.
    EXPECT_TRUE(rs0.isFiltering);

    // For algorithms improved since that involved in this trace (20161231)
    // the valve should not yet be fully closed.  (TODO-1099)
    EXPECT_LT(0, valvePCOpen);

    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":81}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","L":68,"v|%":0,"tT|C":19}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":200,"gE":0}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","L":57,"T|C16":346,"H|%":58}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","O":2,"vac|h":0,"B|cV":254}
    is0.setReferenceTemperatures(346);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","L":50,"v|%":0,"tT|C":19}
    is0.setReferenceTemperatures(345);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","tS|C":0,"vC|%":200,"gE":0}
    is0.setReferenceTemperatures(345);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":344,"H|%":58,"O":2}
    is0.setReferenceTemperatures(344);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":56}
    is0.setReferenceTemperatures(344); // 344 ~ 21.6C.
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","tT|C":18,"v|%":0,"tS|C":1}
    is0.setReferenceTemperatures(343);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"O":1}
    is0.setReferenceTemperatures(343);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":342,"H|%":58,"O":1}
    is0.setReferenceTemperatures(342);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vac|h":0,"B|cV":254,"L":50}
    is0.setReferenceTemperatures(342);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","v|%":0,"tT|C":18,"tS|C":1}
    is0.setReferenceTemperatures(341);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","vC|%":200,"gE":0,"L":47}
    is0.setReferenceTemperatures(340);
    rs0.tick(valvePCOpen, is0, NULL);
    //{"@":"E091B7DC8FEDC7A9","T|C16":339,"H|%":58,"O":1}
    is0.setReferenceTemperatures(339);
    rs0.tick(valvePCOpen, is0, NULL);
    // Filtering should still be on if filter has a minimum on-time.
    const bool slf = rs0.SUPPORT_LONG_FILTER;
    EXPECT_EQ(slf, (0 != rs0.isFiltering));

    // For algorithms improved since that involved in this trace (20161231)
    // the valve should not yet be fully closed.  (TODO-1099)
    EXPECT_LT(0, valvePCOpen);
    const uint8_t v5 = valvePCOpen;
    EXPECT_GE(v4, v5) << "valve should not be re-opening";
    EXPECT_NEAR(344, rs0.getSmoothedRecent(), 5); // 344 ~ 21.5C.
    // If (supporting long filtering and thus) filter is still on
    // then smoothed recent should be below the wellAboveTarget threshold
    // and the valve should still be calling for heat.
    EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);

    // Set back temperature significantly (a FULL setback)
    // and verify that valve is not immediately fully closed,
    // though could even close a little if the ambient stays steady.
    const uint8_t valveOpenBeforeSetback = valvePCOpen;
    const uint8_t setbackTarget = targetTempC -
        OTRadValve::DEFAULT_ValveControlParameters::SETBACK_FULL;
    is0.targetTempC = setbackTarget;
    rs0.tick(valvePCOpen, is0, NULL);
    const uint8_t valveOpenAfterSetback = valvePCOpen;
    EXPECT_GE(valveOpenBeforeSetback, valveOpenAfterSetback);
    EXPECT_LT(0, valveOpenAfterSetback);

    // Synthetically steadily run ambient temperature down towards target.
    // Valve should not need to close any further.
    // (It could even start to open again if target is overshot.)
    for(int16_t ambientC16 = 338; ambientC16 >= (setbackTarget << 4); --ambientC16)
        {
        is0.setReferenceTemperatures(ambientC16);
        rs0.tick(valvePCOpen, is0, NULL);
        EXPECT_LE(valveOpenAfterSetback, valvePCOpen);
        }

    // If (supporting long filtering and thus) filter is still on
    // then smoothed recent probably no longer below wellAboveTarget threshold
    // but the valve should not yet have closed.
    EXPECT_LT(0, valvePCOpen);
}

// Valve fully opening unexpectedly fast on occupancy setback decrease.
// Temperature is not totally stable.
// Room 5s, code tag: 20170105-valve-movement-reduction-2
//[ "2017-01-05T21:39:46Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"L":41,"v|%":19,"tT|C":18} ]
//[ "2017-01-05T21:40:56Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tS|C":1,"vC|%":21,"gE":0} ]
//[ "2017-01-05T21:41:50Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"L":44,"T|C16":295,"H|%":68} ]
//[ "2017-01-05T21:42:54Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"O":1,"vac|h":0,"B|cV":254} ]
//[ "2017-01-05T21:43:44Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"L":41,"v|%":19,"tT|C":18} ]
//[ "2017-01-05T21:44:52Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"tS|C":1,"vC|%":21,"gE":0} ]
//[ "2017-01-05T21:45:54Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"T|C16":294,"H|%":68,"O":1} ]
//[ "2017-01-05T21:46:52Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vac|h":0,"B|cV":254,"L":42} ]
//[ "2017-01-05T21:47:48Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"L":50,"v|%":19,"tT|C":18} ]
//[ "2017-01-05T21:48:46Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"tS|C":1,"vC|%":21,"gE":0} ]
//[ "2017-01-05T21:49:46Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":293,"H|%":69,"O":1} ]
//[ "2017-01-05T21:50:50Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"vac|h":0,"B|cV":254,"L":42} ]
//[ "2017-01-05T21:51:56Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"L":41,"v|%":19,"tT|C":18} ]
//[ "2017-01-05T21:52:58Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"tS|C":1,"vC|%":21,"gE":0} ]
//[ "2017-01-05T21:53:50Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"T|C16":292,"H|%":69,"O":2} ]
//[ "2017-01-05T21:54:56Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"vac|h":0,"B|cV":254,"L":41} ]
//[ "2017-01-05T21:55:56Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"v|%":100,"tT|C":19,"tS|C":0} ]
//[ "2017-01-05T21:56:56Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"vC|%":100,"gE":0,"H|%":71} ]
//[ "2017-01-05T21:57:52Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":296,"H|%":71,"O":2} ]
//[ "2017-01-05T21:58:50Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"vac|h":0,"B|cV":254,"L":41} ]
//[ "2017-01-05T21:59:50Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"T|C16":302,"v|%":100,"L":51} ]
//[ "2017-01-05T22:00:49Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"tT|C":19,"tS|C":0,"L":48} ]
TEST(ModelledRadValve,SampleValveResponse2)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Target temperature without setback.
    const uint8_t targetTempC = 19;

    // Valve starts not quite fully shut.
    uint8_t valvePCOpen = 19;

    // Assume flat temperature before the sample started.
    OTRadValve::ModelledRadValveInputState is0(295); // 295 ~ 18.4C.
    OTRadValve::ModelledRadValveState<> rs0;
    ASSERT_FALSE(rs0.isFiltering) << "filtering must be off before first tick";
    is0.fastResponseRequired = false;
    is0.hasEcoBias = true;

    // Non-set-back temperature.
    is0.maxTargetTempC = targetTempC;
    // Initially set back 1C.
    is0.targetTempC = targetTempC - 1;
    // Wide deadband because set back.
    is0.widenDeadband = true;

    //[ "2017-01-05T21:39:46Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"L":41,"v|%":19,"tT|C":18} ]
    //[ "2017-01-05T21:40:56Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tS|C":1,"vC|%":21,"gE":0} ]
    //[ "2017-01-05T21:41:50Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"L":44,"T|C16":295,"H|%":68} ]
    //[ "2017-01-05T21:42:54Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"O":1,"vac|h":0,"B|cV":254} ]
    //[ "2017-01-05T21:43:44Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"L":41,"v|%":19,"tT|C":18} ]
    //[ "2017-01-05T21:44:52Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"tS|C":1,"vC|%":21,"gE":0} ]
    // Do one tick in quiescent state, set back one degree.
    // After tick, filtering should be off, valve not much moved.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(19, valvePCOpen, 2);

    //[ "2017-01-05T21:45:54Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"T|C16":294,"H|%":68,"O":1} ]
    is0.setReferenceTemperatures(294);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:46:52Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vac|h":0,"B|cV":254,"L":42} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:47:48Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"L":50,"v|%":19,"tT|C":18} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:48:46Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"tS|C":1,"vC|%":21,"gE":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:49:46Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":293,"H|%":69,"O":1} ]
    is0.setReferenceTemperatures(293);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:50:50Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"vac|h":0,"B|cV":254,"L":42} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:51:56Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"L":41,"v|%":19,"tT|C":18} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:52:58Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"tS|C":1,"vC|%":21,"gE":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(21, valvePCOpen, 2);

    // Occupancy!
    // Setback gone.
    is0.targetTempC = targetTempC;
    // Wide deadband gone.
    is0.widenDeadband = false;
    // New occupancy should force a fast response,
    // but either way may take about typical heating system response time
    // before fully opening to have chance of avoiding travel to fully open.
    // Should be immediately at least calling for heat on first tick though
    // with fast response requested.
    const bool fRR = OTV0P2BASE::randRNG8NextBoolean();
    is0.fastResponseRequired = fRR;
    //[ "2017-01-05T21:53:50Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"T|C16":292,"H|%":69,"O":2} ]
    is0.setReferenceTemperatures(292); // 292 ~ 18.3C.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
    //[ "2017-01-05T21:54:56Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"vac|h":0,"B|cV":254,"L":41} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T21:55:56Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"v|%":100,"tT|C":19,"tS|C":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
}

// Valve closing all the way after transition to full setback; should hover.
// Room 1g, code tag: 20170105-valve-movement-reduction-2
//[ "2017-01-05T22:30:54Z", "", {"@":"FEDA88A08188E083","+":11,"L":2,"v|%":31,"tT|C":17} ]
//[ "2017-01-05T22:31:54Z", "", {"@":"FEDA88A08188E083","+":12,"tS|C":1,"vC|%":2211,"gE":0} ]
//[ "2017-01-05T22:33:02Z", "", {"@":"FEDA88A08188E083","+":13,"T|C16":295,"H|%":69,"O":1} ]
//[ "2017-01-05T22:33:54Z", "", {"@":"FEDA88A08188E083","+":14,"vac|h":0,"B|cV":262,"L":2} ]
//[ "2017-01-05T22:35:00Z", "", {"@":"FEDA88A08188E083","+":15,"v|%":31,"tT|C":17,"tS|C":1} ]
//[ "2017-01-05T22:35:48Z", "", {"@":"FEDA88A08188E083","+":0,"vC|%":2211,"gE":0} ]
//[ "2017-01-05T22:36:50Z", "", {"@":"FEDA88A08188E083","+":1,"T|C16":293,"H|%":69,"O":1} ]
//[ "2017-01-05T22:37:50Z", "", {"@":"FEDA88A08188E083","+":2,"T|C16":292,"vac|h":0,"L":2} ]
//[ "2017-01-05T22:38:56Z", "", {"@":"FEDA88A08188E083","+":3,"v|%":31,"tT|C":17,"tS|C":1} ]
//[ "2017-01-05T22:39:52Z", "", {"@":"FEDA88A08188E083","+":4,"H|%":70,"vC|%":2211} ]
//[ "2017-01-05T22:40:48Z", "", {"@":"FEDA88A08188E083","+":5,"gE":0,"T|C16":291,"H|%":70} ]
//[ "2017-01-05T22:42:02Z", "", {"@":"FEDA88A08188E083","+":6,"O":1,"vac|h":0,"B|cV":262} ]
//[ "2017-01-05T22:42:58Z", "", {"@":"FEDA88A08188E083","+":7,"L":2,"v|%":31,"tT|C":17} ]
//[ "2017-01-05T22:43:56Z", "", {"@":"FEDA88A08188E083","+":8,"T|C16":290,"tS|C":1} ]
//[ "2017-01-05T22:44:54Z", "", {"@":"FEDA88A08188E083","+":9,"vC|%":2211,"gE":0,"vac|h":1} ]
//[ "2017-01-05T22:45:56Z", "", {"@":"FEDA88A08188E083","+":10,"T|C16":289,"H|%":70,"O":1} ]
//[ "2017-01-05T22:46:48Z", "", {"@":"FEDA88A08188E083","+":11,"vac|h":1,"B|cV":262,"L":2} ]
//[ "2017-01-05T22:47:48Z", "", {"@":"FEDA88A08188E083","+":12,"v|%":31,"tT|C":17,"tS|C":1} ]
//[ "2017-01-05T22:48:52Z", "", {"@":"FEDA88A08188E083","+":13,"vC|%":2242,"gE":0,"H|%":71} ]
//[ "2017-01-05T22:50:02Z", "", {"@":"FEDA88A08188E083","+":14,"T|C16":287,"H|%":71,"O":1} ]
//[ "2017-01-05T22:50:48Z", "", {"@":"FEDA88A08188E083","+":15,"vac|h":1,"B|cV":262,"L":2} ]
//[ "2017-01-05T22:51:54Z", "", {"@":"FEDA88A08188E083","+":0,"v|%":0,"tT|C":12,"tS|C":6} ]
//[ "2017-01-05T22:52:54Z", "", {"@":"FEDA88A08188E083","+":1,"vC|%":2242,"gE":0} ]
//[ "2017-01-05T22:53:52Z", "", {"@":"FEDA88A08188E083","+":2,"T|C16":286,"H|%":71,"O":1} ]
//[ "2017-01-05T22:54:50Z", "", {"@":"FEDA88A08188E083","+":3,"vac|h":1,"B|cV":262,"L":2} ]
//[ "2017-01-05T22:56:01Z", "", {"@":"FEDA88A08188E083","+":4,"v|%":0,"tT|C":12,"tS|C":6} ]
//[ "2017-01-05T22:56:58Z", "", {"@":"FEDA88A08188E083","+":5,"vC|%":2242,"gE":0} ]
//[ "2017-01-05T22:57:47Z", "", {"@":"FEDA88A08188E083","+":6,"T|C16":284,"H|%":71,"O":1} ]
TEST(ModelledRadValve,SampleValveResponse3)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Target temperature without setback.
    const uint8_t targetTempC = 18;

    // Valve starts partly open.
    uint8_t valvePCOpen = 31;

    // Assume flat temperature before the sample started.
    OTRadValve::ModelledRadValveInputState is0(295); // 295 ~ 18.4C.
    OTRadValve::ModelledRadValveState<> rs0;
    ASSERT_FALSE(rs0.isFiltering) << "filtering must be off before first tick";
    is0.fastResponseRequired = false;
    is0.hasEcoBias = true;

    // Non-set-back temperature.
    is0.maxTargetTempC = targetTempC;
    // Initially set back 1C.
    is0.targetTempC = targetTempC - 1;
    // Wide deadband because set back.
    is0.widenDeadband = true;

    //[ "2017-01-05T22:30:54Z", "", {"@":"FEDA88A08188E083","+":11,"L":2,"v|%":31,"tT|C":17} ]
    //[ "2017-01-05T22:31:54Z", "", {"@":"FEDA88A08188E083","+":12,"tS|C":1,"vC|%":2211,"gE":0} ]
    //[ "2017-01-05T22:33:02Z", "", {"@":"FEDA88A08188E083","+":13,"T|C16":295,"H|%":69,"O":1} ]
    //[ "2017-01-05T22:33:54Z", "", {"@":"FEDA88A08188E083","+":14,"vac|h":0,"B|cV":262,"L":2} ]
    // Do one tick in quiescent state, set back one degree.
    // After tick, filtering should be off, valve not much moved.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(31, valvePCOpen, 2);
    //[ "2017-01-05T22:35:00Z", "", {"@":"FEDA88A08188E083","+":15,"v|%":31,"tT|C":17,"tS|C":1} ]
    is0.setReferenceTemperatures(294); // Interpolated.
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:35:48Z", "", {"@":"FEDA88A08188E083","+":0,"vC|%":2211,"gE":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:36:50Z", "", {"@":"FEDA88A08188E083","+":1,"T|C16":293,"H|%":69,"O":1} ]
    is0.setReferenceTemperatures(293);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:37:50Z", "", {"@":"FEDA88A08188E083","+":2,"T|C16":292,"vac|h":0,"L":2} ]
    is0.setReferenceTemperatures(292);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:38:56Z", "", {"@":"FEDA88A08188E083","+":3,"v|%":31,"tT|C":17,"tS|C":1} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:39:52Z", "", {"@":"FEDA88A08188E083","+":4,"H|%":70,"vC|%":2211} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:40:48Z", "", {"@":"FEDA88A08188E083","+":5,"gE":0,"T|C16":291,"H|%":70} ]
    is0.setReferenceTemperatures(291);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:42:02Z", "", {"@":"FEDA88A08188E083","+":6,"O":1,"vac|h":0,"B|cV":262} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:42:58Z", "", {"@":"FEDA88A08188E083","+":7,"L":2,"v|%":31,"tT|C":17} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:43:56Z", "", {"@":"FEDA88A08188E083","+":8,"T|C16":290,"tS|C":1} ]
    is0.setReferenceTemperatures(290);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:44:54Z", "", {"@":"FEDA88A08188E083","+":9,"vC|%":2211,"gE":0,"vac|h":1} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:45:56Z", "", {"@":"FEDA88A08188E083","+":10,"T|C16":289,"H|%":70,"O":1} ]
    is0.setReferenceTemperatures(289);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:46:48Z", "", {"@":"FEDA88A08188E083","+":11,"vac|h":1,"B|cV":262,"L":2} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:47:48Z", "", {"@":"FEDA88A08188E083","+":12,"v|%":31,"tT|C":17,"tS|C":1} ]
    // The valve should not have moved much or at all.
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(31, valvePCOpen, 5);

    // In original trace, large setback is applied, and valve fully closes.
    // The valve should at most slowly close so as to reduce movement/noise.
    // Now set back 6C.
    is0.targetTempC = targetTempC - 6;
    // Wide deadband because set back.
    is0.widenDeadband = true;

    //[ "2017-01-05T22:48:52Z", "", {"@":"FEDA88A08188E083","+":13,"vC|%":2242,"gE":0,"H|%":71} ]
    is0.setReferenceTemperatures(288); // Interpolated.
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:50:02Z", "", {"@":"FEDA88A08188E083","+":14,"T|C16":287,"H|%":71,"O":1} ]
    is0.setReferenceTemperatures(287);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:50:48Z", "", {"@":"FEDA88A08188E083","+":15,"vac|h":1,"B|cV":262,"L":2} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:51:54Z", "", {"@":"FEDA88A08188E083","+":0,"v|%":0,"tT|C":12,"tS|C":6} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:52:54Z", "", {"@":"FEDA88A08188E083","+":1,"vC|%":2242,"gE":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:53:52Z", "", {"@":"FEDA88A08188E083","+":2,"T|C16":286,"H|%":71,"O":1} ]
    is0.setReferenceTemperatures(286);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:54:50Z", "", {"@":"FEDA88A08188E083","+":3,"vac|h":1,"B|cV":262,"L":2} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:56:01Z", "", {"@":"FEDA88A08188E083","+":4,"v|%":0,"tT|C":12,"tS|C":6} ]
    is0.setReferenceTemperatures(285); // Interpolated.
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:56:58Z", "", {"@":"FEDA88A08188E083","+":5,"vC|%":2242,"gE":0} ]
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-05T22:57:47Z", "", {"@":"FEDA88A08188E083","+":6,"T|C16":284,"H|%":71,"O":1} ]
    is0.setReferenceTemperatures(284);
    rs0.tick(valvePCOpen, is0, NULL);
    // Still no filtering, valve still not closed.
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(31, valvePCOpen, 5);
}

// Valve closing all the way after transition to full setback; should hover.
// Room 5s, code tag: 20170112-valve-movement-reduction
//[ "2017-01-12T13:48:29Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"v|%":32,"tT|C":16,"tS|C":3} ]
//[ "2017-01-12T13:49:27Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vC|%":306,"gE":0,"L":14} ]
//[ "2017-01-12T13:50:23Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"O":2,"T|C16":289,"H|%":74} ]
//[ "2017-01-12T13:51:31Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"O":2,"vac|h":0,"B|cV":254} ]
//[ "2017-01-12T13:52:33Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"L":34,"v|%":69,"tT|C":19} ]
//[ "2017-01-12T13:53:33Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"tS|C":0,"vC|%":343,"gE":0} ]
//[ "2017-01-12T13:54:33Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"T|C16":295,"H|%":76,"O":2} ]
//[ "2017-01-12T13:55:23Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"vac|h":0,"B|cV":254,"L":34} ]
//[ "2017-01-12T13:56:31Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"T|C16":299,"v|%":69} ]
//[ "2017-01-12T13:57:21Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tT|C":19,"tS|C":0,"H|%":75} ]
//[ "2017-01-12T13:58:33Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":303,"vC|%":343} ]
//[ "2017-01-12T13:59:31Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"gE":0,"T|C16":305,"H|%":74} ]
//[ "2017-01-12T14:00:29Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"L":32,"O":2,"vac|h":0} ]
//[ "2017-01-12T14:01:31Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"B|cV":254,"L":33,"v|%":69} ]
//[ "2017-01-12T14:02:19Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"tT|C":19,"tS|C":0} ]
//[ "2017-01-12T14:03:31Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vC|%":343,"gE":0,"H|%":73} ]
//[ "2017-01-12T14:04:23Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"T|C16":322,"H|%":72,"O":2} ]
//[ "2017-01-12T14:05:23Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vac|h":0,"B|cV":254,"L":33} ]
//[ "2017-01-12T14:06:25Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":330,"v|%":69} ]
//[ "2017-01-12T14:07:23Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"tT|C":19,"tS|C":0,"H|%":70} ]
//[ "2017-01-12T14:08:21Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"T|C16":336,"vC|%":343} ]
//[ "2017-01-12T14:09:31Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"gE":0,"T|C16":339,"H|%":69} ]
//[ "2017-01-12T14:10:33Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"L":31,"O":2,"vac|h":0} ]
//[ "2017-01-12T14:11:21Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"B|cV":254,"L":31,"v|%":69} ]
//[ "2017-01-12T14:12:29Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"T|C16":347,"tT|C":19} ]
//[ "2017-01-12T14:13:25Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tS|C":0,"vC|%":346,"gE":0} ]
//[ "2017-01-12T14:14:21Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":352,"H|%":66,"O":2} ]
//[ "2017-01-12T14:15:19Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"vac|h":0,"B|cV":254,"L":32} ]
//[ "2017-01-12T14:16:23Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"v|%":49,"tT|C":19,"tS|C":0} ]
//[ "2017-01-12T14:17:19Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"vC|%":363,"gE":0,"v|%":46} ]
//[ "2017-01-12T14:18:19Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"T|C16":361,"H|%":65,"O":2} ]
//[ "2017-01-12T14:19:19Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vac|h":0,"B|cV":254,"L":32} ]
//[ "2017-01-12T14:20:31Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"v|%":44,"tT|C":19,"tS|C":0} ]
//[ "2017-01-12T14:21:25Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vC|%":368,"gE":0,"H|%":64} ]
//[ "2017-01-12T14:22:23Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":370,"H|%":63,"O":2} ]
//[ "2017-01-12T14:23:19Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"vac|h":0,"B|cV":254,"L":31} ]
//[ "2017-01-12T14:24:19Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"v|%":41,"tT|C":19,"tS|C":0} ]
//[ "2017-01-12T14:25:23Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"vC|%":371,"gE":0,"H|%":62} ]
//[ "2017-01-12T14:26:33Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"T|C16":378,"H|%":62,"O":2} ]
//[ "2017-01-12T14:27:33Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"vac|h":0,"B|cV":254,"L":31} ]
//[ "2017-01-12T14:28:21Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"v|%":37,"tT|C":19,"tS|C":0} ]
//[ "2017-01-12T14:29:27Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"vC|%":375,"gE":0,"H|%":61} ]
//[ "2017-01-12T14:30:33Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":380,"H|%":61,"O":1} ]
//[ "2017-01-12T14:31:31Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"vac|h":0,"B|cV":254,"L":10} ]
//[ "2017-01-12T14:32:19Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"v|%":32,"tT|C":18,"tS|C":1} ]
//[ "2017-01-12T14:33:25Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"vC|%":380,"gE":0} ]
//[ "2017-01-12T14:34:28Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"T|C16":379,"H|%":61,"O":1} ]
//[ "2017-01-12T14:35:31Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vac|h":0,"B|cV":252,"L":10} ]
//[ "2017-01-12T14:36:31Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"v|%":0,"tT|C":18,"tS|C":1} ]
//[ "2017-01-12T14:37:29Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vC|%":412,"gE":0} ]
//[ "2017-01-12T14:38:27Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":377,"H|%":61,"O":2} ]
//[ "2017-01-12T14:39:33Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"vac|h":0,"B|cV":252,"L":31} ]
//[ "2017-01-12T14:40:23Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"tT|C":19,"v|%":0,"tS|C":0} ]
TEST(ModelledRadValve,SampleValveResponse4)
{
    // Seed PRNG for use in simulator; --gtest_shuffle will force it to change.
    srandom((unsigned) ::testing::UnitTest::GetInstance()->random_seed());
    OTV0P2BASE::seedRNG8(random() & 0xff, random() & 0xff, random() & 0xff);

    // Target temperature without setback.
    const uint8_t targetTempC = 19;

    // Valve starts partly open.
    uint8_t valvePCOpen = 32;

    // Assume flat temperature before the sample started.
    OTRadValve::ModelledRadValveInputState is0(289);
    OTRadValve::ModelledRadValveState<> rs0;
    is0.fastResponseRequired = false;
    is0.hasEcoBias = true;

    // Non-set-back temperature.
    is0.maxTargetTempC = targetTempC;
    // Initially set back.
    is0.targetTempC = targetTempC - 3;
    // Wide deadband because set back.
    is0.widenDeadband = true;

    //[ "2017-01-12T13:48:29Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"v|%":32,"tT|C":16,"tS|C":3} ]
    //[ "2017-01-12T13:49:27Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vC|%":306,"gE":0,"L":14} ]
    //[ "2017-01-12T13:50:23Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"O":2,"T|C16":289,"H|%":74} ]
    //[ "2017-01-12T13:51:31Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"O":2,"vac|h":0,"B|cV":254} ]
    //[ "2017-01-12T13:52:33Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"L":34,"v|%":69,"tT|C":19} ]
    // Do one tick in quiescent state, set back.
    // After tick, filtering should be off, valve not much moved.
    rs0.tick(valvePCOpen, is0, NULL);
    EXPECT_FALSE(rs0.isFiltering);
    EXPECT_NEAR(32, valvePCOpen, 2);
    // Then remove the setback and have 'fast response required' for 3 ticks.
    // No wide deadband.
    is0.targetTempC = targetTempC;
    is0.fastResponseRequired = true;
    is0.widenDeadband = false;
    rs0.tick(valvePCOpen, is0, NULL);
    rs0.tick(valvePCOpen, is0, NULL);
    rs0.tick(valvePCOpen, is0, NULL);
    // Then fast response required off and a further tick.
    is0.fastResponseRequired = false;
    is0.setReferenceTemperatures(291); // Interpolated.
    rs0.tick(valvePCOpen, is0, NULL);
    // Valve should be at or over strong call-for-heat level.
    EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN, valvePCOpen);

    //[ "2017-01-12T13:53:33Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"tS|C":0,"vC|%":343,"gE":0} ]
    is0.setReferenceTemperatures(293); // Interpolated.
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:54:33Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"T|C16":295,"H|%":76,"O":2} ]
    is0.setReferenceTemperatures(295);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:55:23Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"vac|h":0,"B|cV":254,"L":34} ]
    is0.setReferenceTemperatures(297);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:56:31Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"T|C16":299,"v|%":69} ]
    is0.setReferenceTemperatures(299);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:57:21Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tT|C":19,"tS|C":0,"H|%":75} ]
    is0.setReferenceTemperatures(301);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:58:33Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":303,"vC|%":343} ]
    is0.setReferenceTemperatures(303);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T13:59:31Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"gE":0,"T|C16":305,"H|%":74} ]
    is0.setReferenceTemperatures(305);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:00:29Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"L":32,"O":2,"vac|h":0} ]
    is0.setReferenceTemperatures(308);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:01:31Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"B|cV":254,"L":33,"v|%":69} ]
    is0.setReferenceTemperatures(311);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:02:19Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"tT|C":19,"tS|C":0} ]
    is0.setReferenceTemperatures(315);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:03:31Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vC|%":343,"gE":0,"H|%":73} ]
    is0.setReferenceTemperatures(318);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:04:23Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"T|C16":322,"H|%":72,"O":2} ]
    is0.setReferenceTemperatures(322);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:05:23Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vac|h":0,"B|cV":254,"L":33} ]
    is0.setReferenceTemperatures(326);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:06:25Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":330,"v|%":69} ]
    is0.setReferenceTemperatures(330);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:07:23Z", "", {"@":"E091B7DC8FEDC7A9","+":11,"tT|C":19,"tS|C":0,"H|%":70} ]
    is0.setReferenceTemperatures(333);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:08:21Z", "", {"@":"E091B7DC8FEDC7A9","+":12,"T|C16":336,"vC|%":343} ]
    is0.setReferenceTemperatures(336);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:09:31Z", "", {"@":"E091B7DC8FEDC7A9","+":13,"gE":0,"T|C16":339,"H|%":69} ]
    is0.setReferenceTemperatures(339);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:10:33Z", "", {"@":"E091B7DC8FEDC7A9","+":14,"L":31,"O":2,"vac|h":0} ]
    is0.setReferenceTemperatures(342);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:11:21Z", "", {"@":"E091B7DC8FEDC7A9","+":15,"B|cV":254,"L":31,"v|%":69} ]
    is0.setReferenceTemperatures(345);
    rs0.tick(valvePCOpen, is0, NULL);

    // Valve should still at/above normal call-for-heat level.
    EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen);
//    EXPECT_NEAR(OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN, valvePCOpen, 5);

    //[ "2017-01-12T14:12:29Z", "", {"@":"E091B7DC8FEDC7A9","+":0,"T|C16":347,"tT|C":19} ]
    is0.setReferenceTemperatures(347);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:13:25Z", "", {"@":"E091B7DC8FEDC7A9","+":1,"tS|C":0,"vC|%":346,"gE":0} ]
    is0.setReferenceTemperatures(350);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:14:21Z", "", {"@":"E091B7DC8FEDC7A9","+":2,"T|C16":352,"H|%":66,"O":2} ]
    is0.setReferenceTemperatures(353);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:15:19Z", "", {"@":"E091B7DC8FEDC7A9","+":3,"vac|h":0,"B|cV":254,"L":32} ]
    is0.setReferenceTemperatures(355);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:16:23Z", "", {"@":"E091B7DC8FEDC7A9","+":4,"v|%":49,"tT|C":19,"tS|C":0} ]
    is0.setReferenceTemperatures(357);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:17:19Z", "", {"@":"E091B7DC8FEDC7A9","+":5,"vC|%":363,"gE":0,"v|%":46} ]
    is0.setReferenceTemperatures(359);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:18:19Z", "", {"@":"E091B7DC8FEDC7A9","+":6,"T|C16":361,"H|%":65,"O":2} ]
    is0.setReferenceTemperatures(361);
    rs0.tick(valvePCOpen, is0, NULL);

    EXPECT_NEAR(342, rs0.getSmoothedRecent(), 5); // 342 ~ 21.4C.
    // Should still be big dT/dt and thus filtering should be engaged.
    // (Longer-term delta across full filter length is even more impressive...)
    EXPECT_LT(8, OTV0P2BASE::fnabs(rs0.getRawDelta(rs0.MIN_TICKS_0p5C_DELTA)));
    EXPECT_TRUE(rs0.isFiltering);

    // Valve should still at/above normal call-for-heat level
    // providing the room is not too far above the target temperature.
    // Already below in the original trace.
    const int_fast16_t overshoot1 = is0.refTempC16 - (targetTempC*16);
    if(overshoot1 < 4 * 16)
        { EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen) << overshoot1; }
    EXPECT_NEAR(OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN, valvePCOpen, 25);
    // In any case the valve should not have fully closed.
    EXPECT_LT(0, valvePCOpen);

    //[ "2017-01-12T14:19:19Z", "", {"@":"E091B7DC8FEDC7A9","+":7,"vac|h":0,"B|cV":254,"L":32} ]
    is0.setReferenceTemperatures(364);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:20:31Z", "", {"@":"E091B7DC8FEDC7A9","+":8,"v|%":44,"tT|C":19,"tS|C":0} ]
    is0.setReferenceTemperatures(366);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:21:25Z", "", {"@":"E091B7DC8FEDC7A9","+":9,"vC|%":368,"gE":0,"H|%":64} ]
    is0.setReferenceTemperatures(368);
    rs0.tick(valvePCOpen, is0, NULL);
    //[ "2017-01-12T14:22:23Z", "", {"@":"E091B7DC8FEDC7A9","+":10,"T|C16":370,"H|%":63,"O":2} ]
    is0.setReferenceTemperatures(370); // 370 ~ 23.1C.
    rs0.tick(valvePCOpen, is0, NULL);

    EXPECT_NEAR(353, rs0.getSmoothedRecent(), 5); // 342 ~ 22.1C.

    // Valve should still at/above normal call-for-heat level
    // providing the room is not too far above the target temperature.
    // Already below in the original trace.
    const int_fast16_t overshoot2 = is0.refTempC16 - (targetTempC*16);
    if(overshoot2 < 4 * 16)
        { EXPECT_LE(OTRadValve::DEFAULT_VALVE_PC_SAFER_OPEN, valvePCOpen) << overshoot2; }
    EXPECT_NEAR(OTRadValve::DEFAULT_VALVE_PC_MODERATELY_OPEN, valvePCOpen, 30);
}


// C16 (Celsius*16) room temperature and target data samples, along with optional expected event from ModelledRadValve.
// Can be directly created from OpenTRV log files.
class C16DataSample
    {
    public:
        const uint8_t d, H, M, tC;
        const int16_t C16;
        const OTRadValve::ModelledRadValveState<>::event_t expected;

        // Day/hour/minute and light level and expected result.
        // An expected result of 0 means no particular event expected from this (anything is acceptable).
        C16DataSample(uint8_t dayOfMonth, uint8_t hour24, uint8_t minute,
                      uint8_t tTempC, int16_t tempC16,
                      OTRadValve::ModelledRadValveState<>::event_t expectedResult = OTRadValve::ModelledRadValveState<>::MRVE_NONE)
            : d(dayOfMonth), H(hour24), M(minute), tC(tTempC), C16(tempC16), expected(expectedResult)
            { }

        // Create/mark a terminating entry; all input values invalid.
        C16DataSample() : d(255), H(255), M(255), tC(255), C16(~0), expected(OTRadValve::ModelledRadValveState<>::MRVE_NONE) { }

        // Compute current minute for this record.
        long currentMinute() const { return((((d * 24L) + H) * 60L) + M); }

        // True for empty/termination data record.
        bool isEnd() const { return(d > 31); }
    };

// TODO: tests based on real data samples, for multiple aspects of functionality.
/* eg some or all of:
TODO-442:
1a) No prewarm (eg 'smart' extra heating in FROST mode) in a long-vacant room.
1b) Never a higher pre-warm/FROST-mode target temperature than WARM-mode target.
1c) Prewarm temperature must be set back from normal WARM target.

2a) Setback in WARM mode must happen in dark (quick response) or long vacant room.
2b) FULL setbacks (4C as at 20161016) must be possible in full eco mode.
2c) Setbacks are at most 2C in comfort mode (but there is a setback).
2d) Bigger setbacks are possible after a room has been vacant longer (eg for weekends).
2e) Setbacks should be targeted at times of expected low occupancy.
2f) Some setbacks should be possible in office environments with lights mainly or always on.
*/

// Nominally target up 0.25C--1C drop over a few minutes (limited by the filter length).
// TODO-621: in case of very sharp drop in temperature,
// assume that a window or door has been opened,
// by accident or to ventilate the room,
// so suppress heating to reduce waste.
//
// See one sample 'airing' data set:
//     http://www.earth.org.uk/img/20160930-16WWmultisensortempL.README.txt
//     http://www.earth.org.uk/img/20160930-16WWmultisensortempL.png
//     http://www.earth.org.uk/img/20160930-16WWmultisensortempL.json.xz
//
// 7h (hall, A9B2F7C089EECD89) saw a sharp fall and recovery, possibly from an external door being opened:
// 1C over 10 minutes then recovery by nearly 0.5C over next half hour.
// Note that there is a potential 'sensitising' occupancy signal available,
// ie sudden occupancy may allow triggering with a lower temperature drop.
//[ "2016-09-30T06:45:18Z", "", {"@":"A9B2F7C089EECD89","+":15,"T|C16":319,"H|%":65,"O":1} ]
//[ "2016-09-30T06:57:10Z", "", {"@":"A9B2F7C089EECD89","+":2,"L":101,"T|C16":302,"H|%":60} ]
//[ "2016-09-30T07:05:10Z", "", {"@":"A9B2F7C089EECD89","+":4,"T|C16":303,"v|%":0} ]
//[ "2016-09-30T07:09:08Z", "", {"@":"A9B2F7C089EECD89","+":5,"tT|C":16,"T|C16":305} ]
//[ "2016-09-30T07:21:08Z", "", {"@":"A9B2F7C089EECD89","+":8,"O":2,"T|C16":308,"H|%":64} ]
//[ "2016-09-30T07:33:12Z", "", {"@":"A9B2F7C089EECD89","+":11,"tS|C":0,"T|C16":310} ]
//
// Using an artificially high target temp in the test data to allow draught-mode detection.
static const C16DataSample sample7h[] =
    {
{ 0, 6, 45, 20, 319 },
{ 0, 6, 57, 20, 302, OTRadValve::ModelledRadValveState<>::MRVE_DRAUGHT },
{ 0, 7, 5, 20, 303 },
{ 0, 7, 9, 20, 305 },
{ 0, 7, 21, 20, 308 },
{ 0, 7, 33, 20, 310 },
{ }
    };
//
// 1g (bedroom, FEDA88A08188E083) saw a slower fall, assumed from airing:
// initially of .25C in 12m, 0.75C over 1h, bottoming out ~2h later down ~2C.
// Note that there is a potential 'sensitising' occupancy signal available,
// ie sudden occupancy may allow triggering with a lower temperature drop.
//
// Using an artificially high target temp in the test data to allow draught-mode detection.
//[ "2016-09-30T06:27:30Z", "", {"@":"FEDA88A08188E083","+":8,"tT|C":17,"tS|C":0} ]
//[ "2016-09-30T06:31:38Z", "", {"@":"FEDA88A08188E083","+":9,"gE":0,"T|C16":331,"H|%":67} ]
//[ "2016-09-30T06:35:30Z", "", {"@":"FEDA88A08188E083","+":10,"T|C16":330,"O":2,"L":2} ]
//[ "2016-09-30T06:43:30Z", "", {"@":"FEDA88A08188E083","+":12,"H|%":65,"T|C16":327,"O":2} ]
//[ "2016-09-30T06:59:34Z", "", {"@":"FEDA88A08188E083","+":0,"T|C16":325,"H|%":64,"O":1} ]
//[ "2016-09-30T07:07:34Z", "", {"@":"FEDA88A08188E083","+":2,"H|%":63,"T|C16":324,"O":1} ]
//[ "2016-09-30T07:15:36Z", "", {"@":"FEDA88A08188E083","+":4,"L":95,"tT|C":13,"tS|C":4} ]
//[ "2016-09-30T07:19:30Z", "", {"@":"FEDA88A08188E083","+":5,"vC|%":0,"gE":0,"T|C16":321} ]
//[ "2016-09-30T07:23:29Z", "", {"@":"FEDA88A08188E083","+":6,"T|C16":320,"H|%":63,"O":1} ]
//[ "2016-09-30T07:31:27Z", "", {"@":"FEDA88A08188E083","+":8,"L":102,"T|C16":319,"H|%":63} ]
// ...
//[ "2016-09-30T08:15:27Z", "", {"@":"FEDA88A08188E083","+":4,"T|C16":309,"H|%":61,"O":1} ]
//[ "2016-09-30T08:27:41Z", "", {"@":"FEDA88A08188E083","+":7,"vC|%":0,"T|C16":307} ]
//[ "2016-09-30T08:39:33Z", "", {"@":"FEDA88A08188E083","+":10,"T|C16":305,"H|%":61,"O":1} ]
//[ "2016-09-30T08:55:29Z", "", {"@":"FEDA88A08188E083","+":14,"T|C16":303,"H|%":61,"O":1} ]
//[ "2016-09-30T09:07:37Z", "", {"@":"FEDA88A08188E083","+":1,"gE":0,"T|C16":302,"H|%":61} ]
//[ "2016-09-30T09:11:29Z", "", {"@":"FEDA88A08188E083","+":2,"T|C16":301,"O":1,"L":175} ]
//[ "2016-09-30T09:19:41Z", "", {"@":"FEDA88A08188E083","+":4,"T|C16":301,"H|%":61,"O":1} ]
static const C16DataSample sample1g[] =
    {
{ 0, 6, 31, 20, 331 },
{ 0, 6, 35, 20, 330 },
{ 0, 6, 43, 20, 327, OTRadValve::ModelledRadValveState<>::MRVE_DRAUGHT },
{ 0, 6, 59, 20, 325 },
{ 0, 7, 7, 20, 324 },
{ 0, 7, 19, 20, 321, OTRadValve::ModelledRadValveState<>::MRVE_DRAUGHT },
{ 0, 7, 23, 20, 320 },
{ 0, 7, 31, 20, 319 },
//...
{ 0, 8, 15, 20, 309 },
{ 0, 8, 27, 20, 307 },
{ 0, 8, 39, 20, 305 },
{ 0, 8, 55, 20, 303 },
{ 0, 9, 7, 20, 302 },
{ 0, 9, 11, 20, 301 },
{ 0, 9, 19, 20, 301 },
{ }
    };

// TODO: standard driver and test cases from data above!












// Old notes from Unit_Tests.cpp as of 2016/10/29.

/*
TODO-442:
1a) *No prewarm (eg 'smart' extra heating in FROST mode) in a long-vacant room.
1b) *Never a higher pre-warm/FROST-mode target temperature than WARM-mode target.
1c) *Prewarm temperature must be set back from normal WARM target.

2a) *Setback in WARM mode must happen in dark (quick response) or long vacant room.
2b) *Setbacks of up to FULL (3C) must be possible in full eco mode.
2c) *Setbacks are at most 2C in comfort mode (but there is a setback).
2d) Bigger setbacks are possible after a room has been vacant longer (eg for weekends).
2e) Setbacks should be targeted at times of expected low occupancy.
2f) Some setbacks should be possible in office environments with lights mainly or always on.

Starred items are tested.
*/


// Test set derived from following status lines from a hard-to-regulate-smoothly unit DHD20141230
// (poor static balancing, direct radiative heat, low thermal mass, insufficiently insulated?):
/*
=F0%@9CC;X0;T12 30 W255 0 F255 0 W18 51 F20 36;S7 7 18;HC65 74;{"@":"414a","L":142,"B|mV":3315,"occ|%":0,"vC|%":0}
>W
=W0%@9CC;X0;T12 30 W255 0 F255 0 W18 51 F20 36;S7 7 18;HC65 74;{"@":"414a","L":142,"B|mV":3315,"occ|%":0,"vC|%":0}
=W0%@9CC;X0;T12 30 W255 0 F255 0 W18 51 F20 36;S7 7 18;HC65 74;{"@":"414a","L":135,"B|mV":3315,"occ|%":0,"vC|%":0}
=W10%@9CC;X0;T12 30 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":135,"B|mV":3315,"occ|%":0,"vC|%":10}
=W20%@9CC;X0;T12 31 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":20,"L":132,"B|mV":3315,"occ|%":0}
=W30%@10C0;X0;T12 32 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":30,"L":129,"B|mV":3315,"occ|%":0}
=W40%@10CB;X0;T12 33 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":131,"vC|%":40,"B|mV":3315,"occ|%":0}
=W45%@11C5;X0;T12 34 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":45,"L":131,"B|mV":3315,"occ|%":0}
=W50%@11CC;X0;T12 35 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":50,"L":139,"B|mV":3315,"occ|%":0}
=W55%@12C2;X0;T12 36 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":55,"L":132,"B|mV":3315,"occ|%":0}
=W60%@12C7;X0;T12 37 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":133,"vC|%":60,"B|mV":3315,"occ|%":0}
=W65%@12CB;X0;T12 38 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":65,"L":130,"B|mV":3315,"occ|%":0}
=W70%@12CF;X0;T12 39 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":70,"L":127,"B|mV":3315,"occ|%":0}
=W75%@13C2;X0;T12 40 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":75,"L":127,"B|mV":3315,"occ|%":0}
=W80%@13C5;X0;T12 41 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":124,"vC|%":80,"B|mV":3315,"occ|%":0}
=W85%@13C8;X0;T12 42 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":85,"L":121,"B|mV":3315,"occ|%":0}
=W90%@13CB;X0;T12 43 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":90,"L":120,"B|mV":3315,"occ|%":0}
=W95%@13CD;X0;T12 44 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":95,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14C0;X0;T12 45 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14C2;X0;T12 46 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14C4;X0;T12 47 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14C6;X0;T12 48 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14C8;X0;T12 49 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":119,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@14CA;X0;T12 50 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14CC;X0;T12 51 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":120,"B|mV":3315,"occ|%":0}
=W100%@14CE;X0;T12 52 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":124,"B|mV":3315,"occ|%":0}
=W100%@14CF;X0;T12 53 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":121,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@15C1;X0;T12 54 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":123,"B|mV":3315,"occ|%":0}
=W100%@15C3;X0;T12 55 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":125,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@15C4;X0;T12 56 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":125,"B|mV":3315,"occ|%":0}
=W100%@15C6;X0;T12 57 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":126,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@15C7;X0;T12 58 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":127,"B|mV":3315,"occ|%":0}
=W100%@15C9;X0;T12 59 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":128,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@15CA;X0;T13 0 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":129,"B|mV":3315,"occ|%":0}
=W100%@15CB;X0;T13 1 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":129,"B|mV":3315,"occ|%":0}
=W100%@15CD;X0;T13 2 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":129,"B|mV":3315,"occ|%":0}
=W100%@15CE;X0;T13 3 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":130,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@15CF;X0;T13 4 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":130,"B|mV":3315,"occ|%":0}
=W100%@16C1;X0;T13 5 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":130,"B|mV":3315,"occ|%":0}
=W100%@16C2;X0;T13 6 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":130,"B|mV":3315,"occ|%":0}
=W100%@16C3;X0;T13 7 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":131,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@16C4;X0;T13 8 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":131,"B|mV":3315,"occ|%":0}
=W100%@16C6;X0;T13 9 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":132,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@16C7;X0;T13 10 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":132,"B|mV":3315,"occ|%":0}
=W100%@16C8;X0;T13 11 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":132,"B|mV":3315,"occ|%":0}
=W100%@16C9;X0;T13 12 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":132,"B|mV":3315,"occ|%":0}
=W100%@16CA;X0;T13 13 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":133,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@16CB;X0;T13 14 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":134,"B|mV":3315,"occ|%":0}
=W100%@16CC;X0;T13 15 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":135,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@16CD;X0;T13 16 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":136,"B|mV":3315,"occ|%":0}
=W100%@16CE;X0;T13 17 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":137,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@16CF;X0;T13 18 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":137,"B|mV":3315,"occ|%":0}
=W100%@17C0;X0;T13 19 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":140,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@17C1;X0;T13 20 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":140,"B|mV":3315,"occ|%":0}
=W100%@17C2;X0;T13 21 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":139,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@17C3;X0;T13 22 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":133,"B|mV":3315,"occ|%":0}
=W100%@17C4;X0;T13 23 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":131,"vC|%":100,"B|mV":3315,"occ|%":0}
=W100%@17C5;X0;T13 24 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":130,"B|mV":3315,"occ|%":0}
=W100%@17C5;X0;T13 25 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":130,"B|mV":3315,"occ|%":0}
=W100%@17C6;X0;T13 26 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":128,"B|mV":3315,"occ|%":0}
=W100%@17C7;X0;T13 27 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":128,"B|mV":3315,"occ|%":0}
=W100%@17C8;X0;T13 28 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":100,"L":127,"B|mV":3315,"occ|%":0}
=W95%@17C9;X0;T13 29 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":105,"L":127,"B|mV":3315,"occ|%":0}
=W90%@17CA;X0;T13 30 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":110,"L":127,"B|mV":3315,"occ|%":0}
=W85%@17CB;X0;T13 31 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":125,"vC|%":115,"B|mV":3315,"occ|%":0}
=W80%@17CC;X0;T13 32 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":120,"L":125,"B|mV":3315,"occ|%":0}
=W75%@17CD;X0;T13 33 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":125,"L":125,"B|mV":3315,"occ|%":0}
=W70%@17CD;X0;T13 34 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":130,"L":126,"B|mV":3315,"occ|%":0}
=W65%@17CF;X0;T13 35 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":135,"L":126,"B|mV":3315,"occ|%":0}
=W60%@18C0;X0;T13 36 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":140,"L":126,"B|mV":3315,"occ|%":0}
=W55%@18C0;X0;T13 37 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":124,"vC|%":145,"B|mV":3315,"occ|%":0}
=W50%@18C1;X0;T13 38 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":150,"L":127,"B|mV":3315,"occ|%":0}
=W45%@18C2;X0;T13 39 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":155,"L":127,"B|mV":3315,"occ|%":0}
=W40%@18C3;X0;T13 40 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":160,"L":127,"B|mV":3315,"occ|%":0}
=W35%@18C3;X0;T13 41 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":165,"L":127,"B|mV":3315,"occ|%":0}
=W30%@18C4;X0;T13 42 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":170,"L":128,"B|mV":3315,"occ|%":0}
=W25%@18C5;X0;T13 43 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":130,"vC|%":175,"B|mV":3315,"occ|%":0}
=W20%@18C5;X0;T13 44 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":180,"L":131,"B|mV":3315,"occ|%":0}
=W15%@18C6;X0;T13 45 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":185,"L":131,"B|mV":3315,"occ|%":0}
=W15%@18C7;X0;T13 46 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":185,"L":132,"B|mV":3315,"occ|%":0}
=W9%@18C8;X0;T13 47 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":132,"B|mV":3315,"occ|%":0}
=W9%@18C3;X0;T13 48 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":134,"B|mV":3315,"occ|%":0}
=W9%@17C9;X0;T13 49 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":134,"B|mV":3315,"occ|%":0}
=W9%@17C1;X0;T13 50 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":135,"B|mV":3315,"occ|%":0}
=W9%@16CB;X0;T13 51 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":134,"vC|%":191,"B|mV":3315,"occ|%":0}
=W9%@16C6;X0;T13 52 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":132,"B|mV":3315,"occ|%":0}
=W9%@16C3;X0;T13 53 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":130,"vC|%":191,"B|mV":3315,"occ|%":0}
=W9%@16C0;X0;T13 54 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":191,"L":127,"B|mV":3315,"occ|%":0}
=W9%@15CD;X0;T13 55 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":125,"vC|%":191,"B|mV":3315,"occ|%":0}
=W10%@15CB;X0;T13 56 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":192,"L":123,"B|mV":3315,"occ|%":0}
=W20%@15CC;X0;T13 57 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":202,"L":119,"B|mV":3315,"occ|%":0}
=W30%@16C5;X0;T13 58 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":212,"L":118,"B|mV":3315,"occ|%":0}
=W40%@16CD;X0;T13 59 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":115,"vC|%":222,"B|mV":3315,"occ|%":0}
=W45%@17C4;X0;T14 0 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":227,"L":113,"B|mV":3315,"occ|%":0}
=W50%@17C8;X0;T14 1 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":232,"L":110,"B|mV":3315,"occ|%":0}
=W55%@17CC;X0;T14 2 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":237,"L":108,"B|mV":3315,"occ|%":0}
=W55%@17CF;X0;T14 3 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":105,"vC|%":237,"B|mV":3315,"occ|%":0}
=W55%@18C1;X0;T14 4 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":237,"L":102,"B|mV":3315,"occ|%":0}
=W50%@18C4;X0;T14 5 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":242,"L":100,"B|mV":3315,"occ|%":0}
=W45%@18C6;X0;T14 6 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":247,"L":98,"B|mV":3315,"occ|%":0}
=W40%@18C7;X0;T14 7 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":252,"L":98,"B|mV":3315,"occ|%":0}
=W9%@18C9;X0;T14 8 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":96,"B|mV":3315,"occ|%":0}
=W9%@18C9;X0;T14 8 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":96,"B|mV":3315,"occ|%":0}
=W9%@17CC;X0;T14 10 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":96,"B|mV":3315,"occ|%":0}
=W9%@17C4;X0;T14 11 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":94,"vC|%":283,"B|mV":3315,"occ|%":0}
=W9%@16CF;X0;T14 12 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":95,"B|mV":3315,"occ|%":0}
=W9%@16CB;X0;T14 13 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":91,"vC|%":283,"B|mV":3315,"occ|%":0}
=W9%@16C7;X0;T14 14 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":92,"B|mV":3315,"occ|%":0}
=W9%@16C5;X0;T14 15 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":95,"vC|%":283,"B|mV":3315,"occ|%":0}
=W9%@16C3;X0;T14 16 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":283,"L":98,"B|mV":3315,"occ|%":0}
=W10%@16C1;X0;T14 17 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":284,"L":101,"B|mV":3315,"occ|%":0}
=W20%@16C0;X0;T14 18 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":294,"L":104,"B|mV":3315,"occ|%":0}
=W30%@16C9;X0;T14 19 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":108,"vC|%":304,"B|mV":3315,"occ|%":0}
=W40%@17C2;X0;T14 20 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":314,"L":112,"B|mV":3315,"occ|%":0}
=W45%@17C8;X0;T14 21 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":319,"L":116,"B|mV":3315,"occ|%":0}
=W50%@17CE;X0;T14 22 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":324,"L":118,"B|mV":3315,"occ|%":0}
=W50%@18C2;X0;T14 23 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":121,"vC|%":324,"B|mV":3315,"occ|%":0}
=W50%@18C5;X0;T14 24 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":324,"L":125,"B|mV":3315,"occ|%":0}
=W45%@18C8;X0;T14 25 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":329,"L":127,"B|mV":3315,"occ|%":0}
=W40%@18CB;X0;T14 26 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":334,"L":127,"B|mV":3315,"occ|%":0}
=W9%@18CD;X0;T14 27 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":365,"L":127,"B|mV":3315,"occ|%":0}
=W8%@18C9;X0;T14 28 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":366,"L":130,"B|mV":3315,"occ|%":0}
=W7%@18C0;X0;T14 29 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":168,"vC|%":367,"B|mV":3315,"occ|%":0}
=W7%@17CA;X0;T14 30 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":367,"L":191,"B|mV":3315,"occ|%":0}
=W7%@17C4;X0;T14 31 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":367,"L":191,"B|mV":3315,"occ|%":0}
=W7%@17C0;X0;T14 32 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":367,"L":137,"B|mV":3315,"occ|%":0}
=W7%@16CD;X0;T14 33 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":163,"vC|%":367,"B|mV":3315,"occ|%":0}
=W7%@16CA;X0;T14 34 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":367,"L":140,"B|mV":3315,"occ|%":0}
=W7%@16C8;X0;T14 35 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":133,"vC|%":367,"B|mV":3315,"occ|%":0}
=W7%@16C6;X0;T14 36 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":367,"L":162,"B|mV":3315,"occ|%":0}
=W7%@16C5;X0;T14 37 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":126,"vC|%":367,"B|mV":3315,"occ|%":0}
=W10%@16C3;X0;T14 38 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":370,"L":118,"B|mV":3315,"occ|%":0}
=W20%@16C2;X0;T14 39 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":380,"L":111,"B|mV":3315,"occ|%":0}
=W30%@16C9;X0;T14 40 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":390,"L":108,"B|mV":3315,"occ|%":0}
=W40%@17C2;X0;T14 41 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":107,"vC|%":400,"B|mV":3315,"occ|%":0}
=W45%@17CA;X0;T14 42 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":405,"L":104,"B|mV":3315,"occ|%":0}
=W50%@17CF;X0;T14 43 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":410,"L":102,"B|mV":3315,"occ|%":0}
=W50%@18C4;X0;T14 44 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":410,"L":100,"B|mV":3315,"occ|%":0}
=W50%@18C7;X0;T14 45 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":410,"L":100,"B|mV":3315,"occ|%":0}
=W45%@18CA;X0;T14 46 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":415,"L":100,"B|mV":3315,"occ|%":0}
=W9%@18CD;X0;T14 47 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":97,"vC|%":451,"B|mV":3315,"occ|%":0}
=W8%@18CA;X0;T14 48 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":452,"L":103,"B|mV":3315,"occ|%":0}
=W7%@18C1;X0;T14 49 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":103,"B|mV":3315,"occ|%":0}
=W7%@17CB;X0;T14 50 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":101,"B|mV":3315,"occ|%":0}
=W7%@17C6;X0;T14 51 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":101,"B|mV":3315,"occ|%":0}
=W7%@17C2;X0;T14 52 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":97,"B|mV":3315,"occ|%":0}
=W7%@16CF;X0;T14 53 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":93,"vC|%":453,"B|mV":3315,"occ|%":0}
=W7%@16CD;X0;T14 54 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":93,"B|mV":3315,"occ|%":0}
=W7%@16CB;X0;T14 55 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":93,"B|mV":3315,"occ|%":0}
=W7%@16C9;X0;T14 56 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":453,"L":90,"B|mV":3315,"occ|%":0}
=W7%@16C8;X0;T14 57 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":88,"vC|%":453,"B|mV":3315,"occ|%":0}
=W10%@16C7;X0;T14 58 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":456,"L":86,"B|mV":3315,"occ|%":0}
=W20%@16CB;X0;T14 59 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":466,"L":83,"B|mV":3315,"occ|%":0}
=W30%@17C5;X0;T15 0 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":476,"L":81,"B|mV":3315,"occ|%":0}
=W40%@17CD;X0;T15 1 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":486,"L":81,"B|mV":3315,"occ|%":0}
=W40%@18C3;X0;T15 2 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":486,"L":81,"B|mV":3315,"occ|%":0}
=W40%@18C8;X0;T15 3 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":486,"L":81,"B|mV":3315,"occ|%":0}
=W35%@18CC;X0;T15 4 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":491,"L":78,"B|mV":3315,"occ|%":0}
=W9%@19C0;X0;T15 5 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":517,"L":78,"B|mV":3315,"occ|%":0}
=W8%@18CD;X0;T15 6 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":518,"L":78,"B|mV":3315,"occ|%":0}
=W7%@18C5;X0;T15 7 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":519,"L":78,"B|mV":3315,"occ|%":0}
=W6%@17CE;X0;T15 8 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":520,"L":80,"B|mV":3315,"occ|%":0}
=W6%@17CA;X0;T15 9 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":81,"vC|%":520,"B|mV":3315,"occ|%":0}
=W6%@17C6;X0;T15 10 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":520,"L":81,"B|mV":3315,"occ|%":0}
=W6%@17C1;X0;T15 12 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":520,"L":77,"B|mV":3315,"occ|%":0}
=W6%@16CF;X0;T15 13 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":75,"vC|%":520,"B|mV":3315,"occ|%":0}
=W6%@16CD;X0;T15 14 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":520,"L":75,"B|mV":3315,"occ|%":0}
=W6%@16CC;X0;T15 15 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":73,"vC|%":520,"B|mV":3315,"occ|%":0}
=W6%@16CB;X0;T15 16 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":520,"L":71,"B|mV":3315,"occ|%":0}
=W10%@16CA;X0;T15 17 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":524,"L":71,"B|mV":3315,"occ|%":0}
=W20%@16CA;X0;T15 18 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":534,"L":67,"B|mV":3315,"occ|%":0}
=W30%@17C4;X0;T15 19 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","L":64,"vC|%":544,"B|mV":3315,"occ|%":0}
=W40%@17CC;X0;T15 20 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":554,"L":63,"B|mV":3315,"occ|%":0}
=W45%@18C3;X0;T15 21 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":559,"L":61,"B|mV":3315,"occ|%":0}
=W45%@18C9;X0;T15 22 W255 0 F255 0 W18 51 F20 36;S18 7 18;HC65 74;{"@":"414a","vC|%":559,"L":59,"B|mV":3315,"occ|%":0}
...
*/



/*
FIXME: tests pending...  See also TODO-1028.

TODO: retest that lights on in middle of night does not instantly trigger occupancy and heating.

TODO: test fast response to manual UI use AND to probable occupancy, eg lights on, to be responsive.

TODO: test DHW temperature range and restricted max-open (13%) and glacial as per Bo's setup.

TODO: check that BAKE behaves as expected, in target lift amount, and duration, and reversion to WARM, and automatic cancellation on hitting raised target.

TODO: check correct response to sharp temp rise when rad comes on for all-in-one unit, eg with low-pass filtering.

TODO: standard driver and test cases from data above!

TODO: test ModelledRad valve as a whole, including its glue logic that has been buggy before (eg overwriting valve % with temperature!), integrated with sensor and valve mocks as required.

TODO: look at l24 data set for failure to deliver heat in the evenings.

 */

