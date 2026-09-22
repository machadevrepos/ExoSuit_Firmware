#ifndef EXO_BRIDGE_QUEUES_H_
#define EXO_BRIDGE_QUEUES_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace exo::bridge {

template<size_t MaxSources, size_t MaxPayload>
class LatestValueSlots {
public:
    static_assert(MaxSources > 0U, "LatestValueSlots requires a source slot");
    static_assert(MaxPayload > 0U, "LatestValueSlots requires payload storage");

    struct Value {
        uint8_t source_id = 0U;
        uint32_t sequence = 0U;
        uint32_t captured_ms = 0U;
        uint32_t age_ms = 0U;
        uint16_t payload_length = 0U;
        uint8_t payload[MaxPayload]{};
    };

    bool publish(uint8_t source_id, const uint8_t *payload, uint16_t payload_length,
                 uint32_t sequence, uint32_t captured_ms)
    {
        if (source_id >= MaxSources || payload_length > MaxPayload ||
            (payload == nullptr && payload_length != 0U)) {
            return false;
        }
        Slot &slot = slots_[source_id];
        if (slot.pending) {
            ++overwrite_count_;
        } else {
            ++pending_count_;
        }
        slot.value.source_id = source_id;
        slot.value.sequence = sequence;
        slot.value.captured_ms = captured_ms;
        slot.value.age_ms = 0U;
        slot.value.payload_length = payload_length;
        if (payload_length != 0U) {
            std::memcpy(slot.value.payload, payload, payload_length);
        }
        slot.pending = true;
        return true;
    }

    bool pop_next(Value &out, uint32_t now_ms)
    {
        if (pending_count_ == 0U) return false;
        for (size_t offset = 0U; offset < MaxSources; ++offset) {
            const size_t index = (next_source_ + offset) % MaxSources;
            Slot &slot = slots_[index];
            if (!slot.pending) continue;
            out = slot.value;
            out.age_ms = now_ms - out.captured_ms;
            slot.pending = false;
            --pending_count_;
            next_source_ = (index + 1U) % MaxSources;
            return true;
        }
        return false;
    }

    size_t pending_count() const { return pending_count_; }
    uint32_t overwrite_count() const { return overwrite_count_; }

private:
    struct Slot {
        Value value{};
        bool pending = false;
    };

    Slot slots_[MaxSources]{};
    size_t pending_count_ = 0U;
    size_t next_source_ = 0U;
    uint32_t overwrite_count_ = 0U;
};

template<size_t Depth, size_t MaxPayload>
class ReliableFifo {
public:
    static_assert(Depth > 0U, "ReliableFifo requires queue depth");
    static_assert(MaxPayload > 0U, "ReliableFifo requires payload storage");

    struct Value {
        uint32_t sequence = 0U;
        uint16_t payload_length = 0U;
        uint8_t payload[MaxPayload]{};
    };

    bool push(uint32_t sequence, const uint8_t *payload, uint16_t payload_length)
    {
        if (payload_length > MaxPayload ||
            (payload == nullptr && payload_length != 0U)) {
            return false;
        }
        if (count_ == Depth) {
            ++rejected_count_;
            return false;
        }
        Value &entry = entries_[tail_];
        entry.sequence = sequence;
        entry.payload_length = payload_length;
        if (payload_length != 0U) {
            std::memcpy(entry.payload, payload, payload_length);
        }
        tail_ = (tail_ + 1U) % Depth;
        ++count_;
        if (count_ > high_watermark_) high_watermark_ = count_;
        return true;
    }

    bool peek(Value &out) const
    {
        if (count_ == 0U) return false;
        out = entries_[head_];
        return true;
    }

    bool pop(Value &out)
    {
        if (!peek(out)) return false;
        entries_[head_] = Value{};
        head_ = (head_ + 1U) % Depth;
        --count_;
        return true;
    }

    size_t count() const { return count_; }
    size_t high_watermark() const { return high_watermark_; }
    uint32_t rejected_count() const { return rejected_count_; }

private:
    Value entries_[Depth]{};
    size_t head_ = 0U;
    size_t tail_ = 0U;
    size_t count_ = 0U;
    size_t high_watermark_ = 0U;
    uint32_t rejected_count_ = 0U;
};

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_QUEUES_H_
