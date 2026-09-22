#ifndef EXO_BRIDGE_STREAM_DECODER_H_
#define EXO_BRIDGE_STREAM_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <exo/bridge/frame_codec.h>

namespace exo::bridge {

/* Incremental delimiter-based decoder for a circular-DMA byte stream.  A
 * delimiter always terminates the current candidate, including after a
 * malformed or oversized candidate, so corruption cannot poison the next
 * frame. */
template<size_t MaxEncodedLength = kMaxEncodedFrameLength,
         size_t MaxPayloadLength = kMaxBlePipeFrameLength>
class StreamDecoder {
public:
    struct Counters {
        uint32_t frames = 0U;
        uint32_t empty_delimiters = 0U;
        uint32_t malformed = 0U;
        uint32_t oversized = 0U;
    };

    bool push(uint8_t byte)
    {
        if (byte == 0U) {
            if (length_ == 0U) {
                ++counters_.empty_delimiters;
                return false;
            }
            encoded_[length_++] = 0U;
            const Status status = decode(encoded_, length_, payload_,
                                         sizeof(payload_), &pending_);
            length_ = 0U;
            if (status != Status::Ok) {
                if (status == Status::TooLong) {
                    ++counters_.oversized;
                } else {
                    ++counters_.malformed;
                }
                pending_valid_ = false;
                return false;
            }
            pending_valid_ = true;
            ++counters_.frames;
            return true;
        }

        if (length_ >= MaxEncodedLength - 1U) {
            length_ = 0U;
            ++counters_.oversized;
            pending_valid_ = false;
            return false;
        }
        encoded_[length_++] = byte;
        return false;
    }

    bool pop(Frame &frame, uint8_t *payload, size_t payload_capacity)
    {
        if (!pending_valid_ || payload == nullptr ||
            pending_.payload_length > payload_capacity) {
            return false;
        }
        std::memcpy(payload, payload_, pending_.payload_length);
        frame = pending_;
        frame.payload = payload;
        pending_valid_ = false;
        return true;
    }

    const Counters &counters() const { return counters_; }

private:
    uint8_t encoded_[MaxEncodedLength]{};
    uint8_t payload_[MaxPayloadLength]{};
    size_t length_ = 0U;
    Frame pending_{};
    bool pending_valid_ = false;
    Counters counters_{};
};

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_STREAM_DECODER_H_
