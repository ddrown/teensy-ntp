#include "UpdateTimeCore.h"
#include "Elapsed.h"

UpdateTimeOutcome updateTimeCore(ClockDiscipline &discipline, ClockHoldover &holdover, ClockPID_c &pid,
                                  uint32_t ppsCounter, TaiNtpTime gpstime,
                                  uint32_t capturedAtMillis, uint32_t ppsMillisAtCapture, uint32_t nowMillis) {
  UpdateTimeOutcome outcome;

  if(ppsMillisAtCapture == 0) {
    outcome.noPpsYet = true;
    return outcome;
  }

  // Raw subtraction for display only -- if PPS has actually stopped for a
  // very long time this can itself wrap, but that's cosmetic; the
  // accept/reject decision below must not rely on it. See TODO.md, "existing
  // lag check is wraparound-unsafe for long outages".
  outcome.ppsToGPS = capturedAtMillis - ppsMillisAtCapture;

  uint32_t validatedLag;
  if(!elapsedWithin(capturedAtMillis, ppsMillisAtCapture, 950, &validatedLag)) { // allow 950ms between PPS and GPS message
    outcome.lagRejected = true;
    return outcome;
  }

  if(holdover.inHoldover()) {
    // The buffered history predates however long the outage was; mixing it
    // with fresh post-recovery samples overflows ClockPID's internal
    // remoteDuration*COUNTSPERSECOND math once the real gap between them is
    // large enough (confirmed on the bench: dChiSq overflow for several
    // resolves after a long holdover). Start this sample as a fresh
    // reference point instead of carrying the stale history forward. See
    // DONE.md, "ClockPID buffer reset across holdover recovery and
    // bootstrap-phase leap seconds".
    pid.reset_clock();
  }

  outcome.discipline = discipline.process(ppsCounter, gpstime);
  holdover.noteSampleReceived(nowMillis);

  return outcome;
}
