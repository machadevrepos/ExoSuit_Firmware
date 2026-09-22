#ifndef EXO_BRIDGE_LINK_HEALTH_H_
#define EXO_BRIDGE_LINK_HEALTH_H_

#include <cstdint>

#include <exo/bridge/sequence_tracker.h>

namespace exo::bridge {

enum class LinkState : uint8_t {
    Absent = 0,
    Booting,
    Healthy,
    Congested,
    Restarted,
};

// Tracks the coarse liveness of a peer hub across the UART bridge link.
// Booting/Restarted are derived purely from SequenceTracker results and
// elapsed time - never from leaf/node presence - so a hub that owns zero
// commissioned leaves still settles to Healthy instead of reporting Booting
// forever. Callers feed every SequenceResult the bridge produced for a
// frame (see SequenceTracker::observe) via on_frame(), and call tick() once
// per processing cycle regardless of traffic so Absent and the end of a
// Booting/Restarted hold window are detected even with total silence.
class LinkHealth {
public:
    static constexpr uint32_t kDefaultAbsentTimeoutMs = 15000U;
    static constexpr uint32_t kDefaultBootHoldMs = 2000U;
    static constexpr uint32_t kDefaultRestartHoldMs = 3000U;

    LinkHealth() = default;
    LinkHealth(uint32_t absent_timeout_ms, uint32_t boot_hold_ms,
               uint32_t restart_hold_ms)
        : absent_timeout_ms_(absent_timeout_ms),
          boot_hold_ms_(boot_hold_ms),
          restart_hold_ms_(restart_hold_ms)
    {
    }

    void on_frame(SequenceResult result, uint32_t now_ms)
    {
        if (result == SequenceResult::First) {
            state_ = LinkState::Booting;
            edge_ms_ = now_ms;
        } else if (result == SequenceResult::EpochChanged) {
            state_ = LinkState::Restarted;
            edge_ms_ = now_ms;
            ++restart_count_;
        }
        have_frame_ = true;
        last_frame_ms_ = now_ms;
        settle(now_ms);
    }

    void tick(uint32_t now_ms, bool congested)
    {
        congested_ = congested;
        if (!have_frame_ || (now_ms - last_frame_ms_) >= absent_timeout_ms_) {
            state_ = LinkState::Absent;
            return;
        }
        settle(now_ms);
    }

    LinkState state() const { return state_; }
    uint32_t restart_count() const { return restart_count_; }

    // 0xFFFFFFFF sentinel means "never heard from the peer".
    uint32_t age_ms(uint32_t now_ms) const
    {
        return have_frame_ ? (now_ms - last_frame_ms_) : 0xFFFFFFFFU;
    }

private:
    void settle(uint32_t now_ms)
    {
        if (state_ == LinkState::Booting &&
            (now_ms - edge_ms_) < boot_hold_ms_) return;
        if (state_ == LinkState::Restarted &&
            (now_ms - edge_ms_) < restart_hold_ms_) return;
        state_ = congested_ ? LinkState::Congested : LinkState::Healthy;
    }

    uint32_t absent_timeout_ms_ = kDefaultAbsentTimeoutMs;
    uint32_t boot_hold_ms_ = kDefaultBootHoldMs;
    uint32_t restart_hold_ms_ = kDefaultRestartHoldMs;
    LinkState state_ = LinkState::Absent;
    bool have_frame_ = false;
    bool congested_ = false;
    uint32_t last_frame_ms_ = 0U;
    uint32_t edge_ms_ = 0U;
    uint32_t restart_count_ = 0U;
};

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_LINK_HEALTH_H_
