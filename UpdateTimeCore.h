#pragma once

#include <stdint.h>
#include "NtpTimestamp.h"
#include "ClockDiscipline.h"
#include "ClockHoldover.h"
#include "ClockPID.h"

// Pulled out of teensy-ntp.ino's updateTime() so the per-sample orchestration
// -- the PPS/GPS lag check, the holdover-triggered ClockPID reset, and how
// ClockDiscipline's result feeds ClockHoldover -- is host-testable instead of
// only reachable via the untested .ino. This is exactly the code that's
// already produced two real bugs (see DONE.md, "ClockPID buffer reset across
// holdover recovery and bootstrap-phase leap seconds", and TODO.md's
// still-open "MITM bench session follow-up" item), so it deserves the same
// treatment ClockDiscipline/ClockHoldover already got.
//
// Deliberately preserves teensy-ntp.ino's *current* behavior exactly,
// including the still-open noteSampleReceived() ordering question (called
// unconditionally, before checking discipline.rejected) -- fixing that is a
// distinct change from this extraction, tracked separately in TODO.md.
struct UpdateTimeOutcome {
  // True if ppsMillisAtCapture == 0 -- no PPS has ever been captured yet, so
  // there's nothing to do (mirrors updateTime()'s original first early
  // return). Nothing else in this struct is meaningful when this is true.
  bool noPpsYet = false;
  // True if the PPS-to-GPS-message lag exceeded the 950ms tolerance --
  // ppsToGPS is still meaningful (for the "LAG" log line), but discipline is
  // not, since ClockDiscipline::process() was never called.
  bool lagRejected = false;
  // Raw ppsToGPS lag (capturedAtMillis - ppsMillisAtCapture). Valid whenever
  // !noPpsYet -- pushed to WebContent unconditionally once past the
  // noPpsYet check, matching updateTime()'s original setPPSData() call
  // happening before the lag check runs.
  uint32_t ppsToGPS = 0;
  // Only meaningful when !noPpsYet && !lagRejected.
  DisciplineResult discipline = {};
};

UpdateTimeOutcome updateTimeCore(ClockDiscipline &discipline, ClockHoldover &holdover, ClockPID_c &pid,
                                  uint32_t ppsCounter, TaiNtpTime gpstime,
                                  uint32_t capturedAtMillis, uint32_t ppsMillisAtCapture, uint32_t nowMillis);
