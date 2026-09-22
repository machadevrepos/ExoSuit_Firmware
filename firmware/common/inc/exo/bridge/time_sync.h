#ifndef EXO_BRIDGE_TIME_SYNC_H_
#define EXO_BRIDGE_TIME_SYNC_H_

#include <cstdint>

namespace exo::bridge {

class TimeSyncEstimator {
public:
    bool add_sample(uint32_t local_send_ms, uint32_t local_receive_ms,
                    uint32_t peer_time_ms)
    {
        const uint32_t round_trip_ms = local_receive_ms - local_send_ms;
        const uint32_t midpoint_ms = local_send_ms + round_trip_ms / 2U;
        const int32_t sample_offset_ms = signed_delta(peer_time_ms, midpoint_ms);

        if (!initialized_) {
            offset_ms_ = sample_offset_ms;
            uncertainty_ms_ = round_trip_ms / 2U;
            initialized_ = true;
        } else {
            const int32_t difference_ms = sample_offset_ms - offset_ms_;
            offset_ms_ += difference_ms / 2;
            const uint32_t sample_uncertainty =
                round_trip_ms / 2U + absolute_value(difference_ms);
            uncertainty_ms_ = (uncertainty_ms_ + sample_uncertainty) / 2U;
        }
        round_trip_ms_ = round_trip_ms;
        ++sample_count_;
        return true;
    }

    bool initialized() const { return initialized_; }
    uint32_t sample_count() const { return sample_count_; }
    int32_t offset_ms() const { return offset_ms_; }
    uint32_t uncertainty_ms() const { return uncertainty_ms_; }
    uint32_t round_trip_ms() const { return round_trip_ms_; }

    uint32_t to_peer_time(uint32_t local_time_ms) const
    {
        return local_time_ms + static_cast<uint32_t>(offset_ms_);
    }

    uint32_t to_local_time(uint32_t peer_time_ms) const
    {
        return peer_time_ms - static_cast<uint32_t>(offset_ms_);
    }

private:
    static int32_t signed_delta(uint32_t newer, uint32_t older)
    {
        const uint32_t delta = newer - older;
        if (delta <= 0x7FFFFFFFU) return static_cast<int32_t>(delta);
        if (delta == 0x80000000U) return INT32_MIN;
        return -static_cast<int32_t>(0xFFFFFFFFU - delta + 1U);
    }

    static uint32_t absolute_value(int32_t value)
    {
        return value < 0 ? static_cast<uint32_t>(-(static_cast<int64_t>(value)))
                         : static_cast<uint32_t>(value);
    }

    bool initialized_ = false;
    int32_t offset_ms_ = 0;
    uint32_t uncertainty_ms_ = 0U;
    uint32_t round_trip_ms_ = 0U;
    uint32_t sample_count_ = 0U;
};

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_TIME_SYNC_H_
