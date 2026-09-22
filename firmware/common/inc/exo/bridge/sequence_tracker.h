#ifndef EXO_BRIDGE_SEQUENCE_TRACKER_H_
#define EXO_BRIDGE_SEQUENCE_TRACKER_H_

#include <cstdint>

namespace exo::bridge {

enum class SequenceResult : uint8_t {
    First = 0U,
    InOrder,
    Gap,
    Duplicate,
    Reordered,
    EpochChanged
};

class SequenceTracker {
public:
    SequenceResult observe(uint32_t boot_epoch, uint32_t sequence)
    {
        if (!initialized_) {
            initialized_ = true;
            boot_epoch_ = boot_epoch;
            last_sequence_ = sequence;
            return SequenceResult::First;
        }
        if (boot_epoch != boot_epoch_) {
            boot_epoch_ = boot_epoch;
            last_sequence_ = sequence;
            ++epoch_change_count_;
            return SequenceResult::EpochChanged;
        }

        const uint32_t delta = sequence - last_sequence_;
        if (delta == 0U) {
            ++duplicate_count_;
            return SequenceResult::Duplicate;
        }
        if (delta >= 0x80000000U) {
            ++reordered_count_;
            return SequenceResult::Reordered;
        }

        last_sequence_ = sequence;
        if (delta == 1U) return SequenceResult::InOrder;
        gap_count_ += delta - 1U;
        return SequenceResult::Gap;
    }

    void reset()
    {
        initialized_ = false;
        boot_epoch_ = 0U;
        last_sequence_ = 0U;
    }

    bool initialized() const { return initialized_; }
    uint32_t boot_epoch() const { return boot_epoch_; }
    uint32_t last_sequence() const { return last_sequence_; }
    uint32_t gap_count() const { return gap_count_; }
    uint32_t duplicate_count() const { return duplicate_count_; }
    uint32_t reordered_count() const { return reordered_count_; }
    uint32_t epoch_change_count() const { return epoch_change_count_; }

private:
    bool initialized_ = false;
    uint32_t boot_epoch_ = 0U;
    uint32_t last_sequence_ = 0U;
    uint32_t gap_count_ = 0U;
    uint32_t duplicate_count_ = 0U;
    uint32_t reordered_count_ = 0U;
    uint32_t epoch_change_count_ = 0U;
};

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_SEQUENCE_TRACKER_H_
