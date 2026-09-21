#include <exo/ble/node_upload_pump.h>

#include <assert.h>

static_assert(exo::NodeUploadPump::notification_blocks(247U) == 9U,
              "a 247-byte ATT notification needs nine 32-byte BLE blocks");
static_assert(exo::NodeUploadPump::upload_extra_blocks(247U, 4U) == 36U,
              "four complete 247-byte ATT notification buffers need 36 blocks");
static_assert(exo::NodeUploadPump::record_done_eligible(false, true, false, false),
              "a retained Uploading session must re-announce RecordDone after reconnect");

int main()
{
  exo::NodeUploadPump pump;

  // A ready upload starts immediately and only suppresses live preview while active.
  pump.start(10U, 2U);
  assert(pump.live_preview_suppressed());
  assert(pump.ready(10U));
  pump.on_send_result(exo::NodeUploadPump::SendResult::Busy, 10U);
  assert(!pump.ready(11U));
  assert(pump.metrics().busy_count == 1U);

  // Notification completion is the normal wake path after BLE_STATUS_BUSY.
  pump.on_notification_complete(3U);
  assert(pump.ready(11U));
  assert(pump.metrics().notification_complete_count == 3U);

  pump.on_send_result(exo::NodeUploadPump::SendResult::InsufficientResources, 12U);
  assert(!pump.ready(12U));
  assert(pump.metrics().resource_count == 1U);

  // TX pool availability is the normal wake path after resource exhaustion.
  pump.on_tx_pool_available(3U, 2U);
  assert(pump.ready(13U));
  assert(pump.metrics().tx_pool_event_count == 2U);
  assert(pump.metrics().tx_pool_buffers == 3U);

  // Sending consumes reliable-protocol credit; no notification is attempted at zero.
  pump.on_send_result(exo::NodeUploadPump::SendResult::Success, 14U);
  assert(pump.credit() == 1U);
  pump.on_send_result(exo::NodeUploadPump::SendResult::Success, 15U);
  assert(pump.credit() == 0U);
  assert(!pump.ready(16U));

  // Lost controller events may be recovered only by the long watchdog path.
  pump.set_credit(1U);
  pump.on_send_result(exo::NodeUploadPump::SendResult::Busy, 20U);
  assert(!pump.ready(20U + exo::NodeUploadPump::kWatchdogMs - 1U));
  assert(pump.ready(20U + exo::NodeUploadPump::kWatchdogMs));
  assert(pump.metrics().watchdog_wake_count == 1U);

  // Terminal GATT failures are not event-loss recoveries and must never retry.
  pump.on_send_result(exo::NodeUploadPump::SendResult::OtherFailure, 800U);
  assert(pump.terminal_error());
  assert(pump.metrics().terminal_error_count == 1U);
  assert(!pump.ready(800U + exo::NodeUploadPump::kWatchdogMs));
  assert(!pump.live_preview_suppressed());

  pump.stop();
  assert(!pump.live_preview_suppressed());
  assert(!pump.ready(1000U));
  assert(exo::NodeUploadPump::record_done_eligible(false, true, false, false));
  assert(!exo::NodeUploadPump::record_done_eligible(false, true, true, false));
  assert(!exo::NodeUploadPump::record_done_eligible(false, true, false, true));

  // TX-pool wakes carry a capacity budget: the pump tops up exactly what the
  // controller freed instead of re-probing the stack with a doomed
  // INSUFFICIENT_RESOURCES call per chunk (the 1-deep pipeline of the
  // 7-9 KB/s baseline).
  exo::NodeUploadPump budgeted;
  budgeted.start(0U, 6U);
  budgeted.on_send_result(exo::NodeUploadPump::SendResult::Busy, 0U);
  assert(!budgeted.ready(1U));
  budgeted.on_tx_pool_available(3U, 1U);
  assert(budgeted.ready(1U));
  for (int i = 0; i < 3; ++i) {
    assert(budgeted.ready(2U));
    budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success, 2U);
  }
  assert(budgeted.credit() == 3U);
  /* Budget spent: no further send until the next controller event, even
   * though reliable credit remains. */
  assert(!budgeted.ready(3U));
  budgeted.on_tx_pool_available(1U, 1U);
  assert(budgeted.ready(4U));
  budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success, 4U);
  assert(!budgeted.ready(5U));
  assert(budgeted.credit() == 2U);

  // A notification completion means the pool is non-empty but its exact free
  // count is unknown: the wake is unbounded and the burst runs until the
  // stack refuses (or credit runs out first).
  budgeted.on_notification_complete(1U);
  assert(budgeted.ready(6U));
  while (budgeted.credit() != 0U) {
    budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success, 6U);
  }
  assert(!budgeted.ready(7U));
  budgeted.set_credit(2U);
  assert(budgeted.ready(8U));

  // Budget spent with reliable credit remaining and NO controller event: the
  // 750 ms watchdog is the only recovery, and its wake must carry an unbounded
  // budget so the next send does not immediately re-block.
  budgeted.set_credit(4U);
  budgeted.on_tx_pool_available(2U, 10U);
  assert(budgeted.ready(10U));
  budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success, 10U);
  budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success, 10U);
  assert(budgeted.credit() == 2U);
  assert(!budgeted.ready(10U + exo::NodeUploadPump::kWatchdogMs - 1U));
  assert(budgeted.ready(10U + exo::NodeUploadPump::kWatchdogMs));
  assert(budgeted.metrics().watchdog_wake_count == 1U);
  budgeted.on_send_result(exo::NodeUploadPump::SendResult::Success,
                          10U + exo::NodeUploadPump::kWatchdogMs);
  assert(budgeted.credit() == 1U);
  /* With a lost watchdog unbounded-budget assignment this send would re-block
   * on the spent (0) budget and the assert below would fail. */
  assert(budgeted.ready(10U + exo::NodeUploadPump::kWatchdogMs + 1U));

  // start() carries an unbounded burst before any controller event: the first
  // send must not stall the pump after one chunk.
  exo::NodeUploadPump fresh;
  fresh.start(100U, 2U);
  assert(fresh.ready(100U));
  fresh.on_send_result(exo::NodeUploadPump::SendResult::Success, 100U);
  assert(fresh.credit() == 1U);
  assert(fresh.ready(101U));
  fresh.on_send_result(exo::NodeUploadPump::SendResult::Success, 101U);
  assert(fresh.credit() == 0U);
  assert(!fresh.ready(102U));

  // A zero-capacity TX-pool report must not reopen a blocked gate.
  exo::NodeUploadPump zeroed;
  zeroed.start(200U, 1U);
  zeroed.on_send_result(exo::NodeUploadPump::SendResult::InsufficientResources, 200U);
  assert(!zeroed.ready(201U));
  zeroed.on_tx_pool_available(0U, 1U);
  assert(!zeroed.ready(202U));
  zeroed.on_tx_pool_available(1U, 1U);
  assert(zeroed.ready(203U));

  // DLE commissioning retries only classified transient command failures.
  exo::NodeDleCommissioner dle;
  dle.start(100U);
  assert(dle.request_due(100U));
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Transient, 100U);
  assert(dle.state() == exo::NodeDleCommissioner::State::Degraded);
  assert(!dle.request_due(100U + exo::NodeDleCommissioner::kRetryMs - 1U));
  assert(dle.request_due(100U + exo::NodeDleCommissioner::kRetryMs));
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Success,
                        100U + exo::NodeDleCommissioner::kRetryMs);
  assert(dle.state() == exo::NodeDleCommissioner::State::Requested);
  dle.process(100U + exo::NodeDleCommissioner::kRetryMs + exo::NodeDleCommissioner::kCompletionTimeoutMs);
  assert(dle.state() == exo::NodeDleCommissioner::State::Degraded);
  dle.on_complete(251U, 251U);
  assert(dle.state() == exo::NodeDleCommissioner::State::Confirmed);
  assert(dle.negotiated_tx_octets() == 251U);

  dle.start(0U);
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Failed, 0U);
  assert(dle.state() == exo::NodeDleCommissioner::State::Failed);
  assert(!dle.request_due(10000U));

  // Transient retries are bounded rather than becoming a background poller.
  dle.start(0U);
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Transient, 0U);
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Transient,
                        exo::NodeDleCommissioner::kRetryMs);
  dle.on_request_result(exo::NodeDleCommissioner::RequestResult::Transient,
                        2U * exo::NodeDleCommissioner::kRetryMs);
  assert(dle.attempts() == exo::NodeDleCommissioner::kMaxAttempts);
  assert(dle.state() == exo::NodeDleCommissioner::State::Failed);
  assert(!dle.request_due(10000U));
  return 0;
}
