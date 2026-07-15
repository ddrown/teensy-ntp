#include <stdint.h>
#include "ClockHoldover.h"
#include "Elapsed.h"

void ClockHoldover::noteSampleReceived(uint32_t nowMillis) {
  everSeen_ = true;
  lastGoodMillis_ = nowMillis;
  lastPollMillis_ = nowMillis;
  inHoldover_ = false;
  holdoverElapsedMs_ = 0;
}

static uint32_t growDispersion(uint32_t base, uint32_t elapsedMs) {
  double growth = (double)HOLDOVER_PHI_PPM * 1e-6 * ((double)elapsedMs / 1000.0) * 65536.0;
  double total = (double)base + growth;
  if (total >= (double)UINT32_MAX) {
    return UINT32_MAX;
  }
  return (uint32_t)total;
}

HoldoverStatus ClockHoldover::poll(uint32_t nowMillis) {
  HoldoverStatus status = {false, 0};

  if (!everSeen_) {
    return status;
  }

  uint32_t sinceGood;
  if (elapsedWithin(nowMillis, lastGoodMillis_, HOLDOVER_STALE_MS, &sinceGood)) {
    // still within the trusted window -- not stale.
    lastPollMillis_ = nowMillis;
    inHoldover_ = false;
    holdoverElapsedMs_ = 0;
    return status;
  }

  // Stale (or the gap was too large/ambiguous to trust either way -- treat
  // that the same as stale, since we can't confirm freshness). Accumulate
  // holdover duration incrementally, one bounded poll-to-poll tick at a
  // time, rather than taking a single subtraction across the whole outage
  // -- see Elapsed.h for why that's unsafe for an open-ended outage. Uses a
  // saturating add so an outage running past UINT32_MAX ms (~49.7 days)
  // pins the estimate at its max instead of wrapping back to a small value
  // (which would make dispersion silently drop back toward "looks synced").
  uint32_t tick;
  if (elapsedWithin(nowMillis, lastPollMillis_, HOLDOVER_POLL_MAX_GAP_MS, &tick)) {
    holdoverElapsedMs_ = saturatingAddMs(holdoverElapsedMs_, tick);
  }
  lastPollMillis_ = nowMillis;
  inHoldover_ = true;

  // P and I are phase/accumulated-offset terms that go stale without fresh
  // offset samples -- drive the correction from the D (drift-rate) term
  // alone instead of freezing the last full P+I+D output.
  localClock_->setPpb(pid_->d_out() * 1000000000.0);

  status.inHoldover = true;
  status.dispersion = growDispersion(lastGoodDispersion_, holdoverElapsedMs_);
  return status;
}
