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
*/

/*
 Occupancy pseudo-sensor that combines inputs from other sensors.
 */

#ifdef ARDUINO_ARCH_AVR
#include <util/atomic.h>
#endif

#include "OTV0P2BASE_SensorOccupancy.h"


namespace OTV0P2BASE
{


// Shift from minutes remaining to confidence.
// Will not work correctly with timeout > 100.
static constexpr int8_t OCCCP_SHIFT =
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 3) ? 5 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 6) ? 4 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 12) ? 3 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 25) ? 2 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 50) ? 1 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 100) ? 0 :
  ((PseudoSensorOccupancyTracker::OCCUPATION_TIMEOUT_M <= 200) ? -1 :
      -2)))))));

// Force a read/poll of the occupancy and return the % likely occupied [0,100].
// Full consistency of all views/actuators, especially short-term ones,
// may only be enforced directly after read().
// Potentially expensive/slow.
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
// Poll at a fixed rate.
uint8_t PseudoSensorOccupancyTracker::read()
  {
    // Update the various metrics in a thread-/ISR- safe way
    // (needs specific concurrency management, since read-modify-write).
    //
    // These are updated independently and each in a safe way.
    // Some races may remain but should be relatively harmless.
    // Eg an ill-timed ISR call to mark as occupied
    // can leave non-zero vacancy and non-zero occupationCountdownM
    // (ie some inconsistency) until next read() call repairs it.
    //
    // Safely run down occupation timer (or up vacancy time) if need be.
    // Note that vacancyM and vacancyH should never be directly touched
    // by ISR/thread calls.
    safeDecIfNZWeak(occupationCountdownM);
    // Safely run down the 'new occupancy' timer.
    safeDecIfNZWeak(newOccupancyCountdownM);
    // Compute as percentage.
    // Use snapshot of occupationCountdownM for calculation consistency.
    const uint8_t ocM = occupationCountdownM.load();
    if(ocM > 0) { vacancyM = 0; vacancyH = 0; }
    else if(vacancyH < 0xffU)
        { if(++vacancyM >= 60) { vacancyM = 0; ++vacancyH; } }
    const uint8_t newValue = (0 == ocM) ? 0 : OTV0P2BASE::fnmin(100U,
        100U - uint8_t((OCCUPATION_TIMEOUT_M - ocM) << OCCCP_SHIFT));
    value = newValue;
    return(newValue);
  }

// Call when very strong evidence of active room occupation has occurred.
// Do not call based on internal/synthetic events.
// Such evidence may include operation of buttons (etc) on the unit
// or PIR.
// Do not call from (for example) 'on' schedule change.
// Makes occupation immediately visible.
// Thread-safe and ISR-safe.
void PseudoSensorOccupancyTracker::markAsOccupied()
    {
    value = 100;
    // Mark new occupancy if was vacant.
    if(0 == occupationCountdownM.load())
       { newOccupancyCountdownM.store(NEW_OCCUPANCY_TIMEOUT_M); }
    // Mark with maximum occupancy confidence.
    occupationCountdownM.store(OCCUPATION_TIMEOUT_M);
    }

// Call when decent but not very strong evidence of active room occupation, such as a light being turned on, or voice heard.
// Do not call based on internal/synthetic events.
// Doesn't force the room to appear recently occupied.
// If the hardware allows this may immediately turn on the main GUI LED
// until normal GUI reverts it,
// at least periodically.
// Preferably do not call for manual control operation
// to avoid interfering with UI operation.
// Thread-safe and ISR-safe.
void PseudoSensorOccupancyTracker::markAsPossiblyOccupied()
  {
  // Update primary occupation metric in thread-safe way
  // (needs specific concurrency management, since read-modify-write).
  uint8_t ocM = occupationCountdownM.load();
  // Mark new occupancy if was vacant.
  if(0 == ocM)
     { newOccupancyCountdownM.store(NEW_OCCUPANCY_TIMEOUT_M); }
  // Mark with 'likely' occupancy confidence.
  const uint8_t oNew =
      OTV0P2BASE::fnmax(ocM, (uint8_t)(OCCUPATION_TIMEOUT_LIKELY_M));
  // May silently fail if concurrent activity on occupationCountdownM.
  occupationCountdownM.compare_exchange_strong(ocM, oNew);
  }

// Call when weak evidence of active room occupation, such rising RH% or CO2 or mobile phone RF levels while not dark.
// Do not call this based on internal/synthetic events.
// Is ignored if the room has been vacant for a while,
// so for example a weak indication of presence
// is not enough to cancel holiday mode.
// Doesn't force the room to appear recently occupied.
// Doesn't activate the new-occupation status.
// Not ISR-/thread- safe.
void PseudoSensorOccupancyTracker::markAsJustPossiblyOccupied()
  {
  // ISR may theoretically see a stale value for vacancyH;
  // optimised for non-ISR use.
  if(vacancyH > weakVacantHThrH) { return; }
  // Update primary occupation metric in thread-safe way
  // (needs specific concurrency management, since read-modify-write).
  uint8_t ocM = occupationCountdownM.load();
  // Mark with 'weak' occupancy confidence.
  const uint8_t oNew =
      OTV0P2BASE::fnmax(ocM, uint8_t(OCCUPATION_TIMEOUT_MAYBE_M));
  // May silently fail if concurrent activity on occupationCountdownM.
  occupationCountdownM.compare_exchange_strong(ocM, oNew);
  }
}
