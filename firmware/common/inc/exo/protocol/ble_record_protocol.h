#ifndef BLE_RECORD_PROTOCOL_H_
#define BLE_RECORD_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#include <exo/types/topology.h>

namespace exo {

enum class RecordCommand : uint8_t {
    StartRecord = 0x01,
    RecordDone = 0x02,
    SessionChunk = 0x05,
    ChunkAck = 0x06,
    SessionCompleteAck = 0x07,
    LaneFrameV3 = 0x09,
    ReliableFrame = 0x0A,
    PrepareRecord = 0x0B,
    CommitPreparedRecord = 0x0C,
    AbortPreparedRecord = 0x0D,
    StopRecord = 0x0E,
    StartSession = 0x0F,
    RetrySource = 0x10,
    StartSessionV2 = 0x11
};

static constexpr uint8_t kRecordReliableProtoVersion = 6U;
static constexpr uint16_t kRecordReliableMagic = 0x5845U; /* "EX" little endian */
/* Chunk-size budget (all on the same BLE notification):
 *   notification payload  = MTU(247) - 3            = 244 B
 *   blepipe envelope      = BLEPIPE_HDR_LEN(20) + CRC(2) = 22 B
 *   reliable frame header = sizeof(RecordReliableFrameHeader) = 25 B
 *   => max chunk payload  = 244 - 22 - 25           = 197 B
 * 192 stays 5 B under the ceiling for MTU-negotiation margin. A value above
 * 197 makes the wrapped frame exceed 244 B and the stack SILENTLY DROPS every
 * chunk (zero accepted -> SessionStall at +30 s). 200 and 220 both did this.
 * See the static_assert in Node/Core/Src/main.c that locks this invariant. */
static constexpr uint16_t kRecordReliableDefaultChunkSize = 192U;
/* Matches the credit the Master actually grants per window (8). Every sender
 * and the browser presets must agree on this default or tuning gets confusing. */
/* Chunks the node may keep in flight before a credit refresh. Sized to cover
 * the ACK round-trip at the fast (15 ms) upload interval so the node never
 * stalls waiting for a window; 24 is the controller-sanitized maximum. */
static constexpr uint8_t kRecordReliableDefaultCredit = 24U;

enum class RecordSourceId : uint16_t {
    Master = 0U,
    Node1 = 1U,
    Node2 = 2U,
    Node3 = 3U,
    Node4 = 4U,
    Node5 = 5U,
    Node6 = 6U,
    Node7 = 7U,
    Node8 = 8U,
    Node9 = 9U,
    Node10 = 10U,
    Node11 = 11U,
    Node12 = 12U
};

enum class RecordReliableType : uint8_t {
    Manifest = 0x01,
    ManifestAck = 0x02,
    AckWindow = 0x03,
    NackRange = 0x04,
    Chunk = 0x05,
    Pause = 0x06,
    Resume = 0x07,
    Cancel = 0x08,
    VerifyOk = 0x09,
    VerifyFail = 0x0A,
    BusyNotOwner = 0x0B,
    SourceWaiting = 0x0C,
    CommitDone = 0x0D
};

enum RecordReliableFlags : uint16_t {
    kRecordFlagFinalChunk = 0x0001U,
    kRecordFlagRetransmit = 0x0002U,
    kRecordFlagStorageMissing = 0x0004U,
    kRecordFlagCrcMismatch = 0x0008U
};

constexpr uint16_t record_reliable_chunk_flags(bool final_chunk, bool retransmit)
{
    return static_cast<uint16_t>(
            (final_chunk ? static_cast<uint16_t>(kRecordFlagFinalChunk) : 0U) |
            (retransmit ? static_cast<uint16_t>(kRecordFlagRetransmit) : 0U));
}

enum class RecordLaneId : uint8_t {
    Control = 0x00,
    MasterData = 0x01,
    NodeData = 0x02,
    Retransmit = 0x03
};

enum class RecordLaneMsgType : uint8_t {
    RecordDone = 0x01,
    SessionChunk = 0x02,
    SessionBarrier = 0x03
};

#pragma pack(push, 1)
struct StartRecordMessage {
    RecordCommand command;
    uint32_t session_id;
    uint64_t start_timestamp_us;
    uint32_t requested_duration_ms;
};

struct StopRecordMessage {
    RecordCommand command;
    uint32_t session_id;
};

/* Browser -> Master: re-pull one source that was written off after a stall.
 * The node still holds its flash copy (abandoned sources are never erased), so
 * the Master can re-stage the file and drive a fresh reliable window. */
struct RetrySourceMessage {
    RecordCommand command;
    uint8_t node_id;
};

struct StartSessionMessage {
    RecordCommand command;
    uint32_t session_id;
    uint64_t start_timestamp_us;
    uint32_t safety_duration_ms;
    uint8_t selected_node_mask;
    uint8_t stream_interval_ms;
};

/* Versioned session command. Legacy StartSessionMessage remains unchanged. */
struct StartSessionV2Message {
    RecordCommand command;
    uint8_t protocol_version;
    uint32_t session_id;
    uint64_t start_timestamp_us;
    uint32_t safety_duration_ms;
    SourceMask selected_source_mask;
    uint8_t stream_interval_ms;
};

static constexpr uint8_t kStartSessionV2ProtocolVersion = 2U;
static constexpr uint16_t kStartSessionV2WireSize = 21U;

inline bool start_session_v2_encode(const StartSessionV2Message &message,
                                    uint8_t *dst, size_t dst_len)
{
    if (dst == nullptr || dst_len < kStartSessionV2WireSize ||
            message.command != RecordCommand::StartSessionV2 ||
            message.protocol_version != kStartSessionV2ProtocolVersion) {
        return false;
    }
    dst[0] = static_cast<uint8_t>(message.command);
    dst[1] = message.protocol_version;
    dst[2] = static_cast<uint8_t>(message.session_id);
    dst[3] = static_cast<uint8_t>(message.session_id >> 8U);
    dst[4] = static_cast<uint8_t>(message.session_id >> 16U);
    dst[5] = static_cast<uint8_t>(message.session_id >> 24U);
    for (uint8_t i = 0U; i < 8U; ++i) {
        dst[6U + i] = static_cast<uint8_t>(message.start_timestamp_us >> (8U * i));
    }
    dst[14] = static_cast<uint8_t>(message.safety_duration_ms);
    dst[15] = static_cast<uint8_t>(message.safety_duration_ms >> 8U);
    dst[16] = static_cast<uint8_t>(message.safety_duration_ms >> 16U);
    dst[17] = static_cast<uint8_t>(message.safety_duration_ms >> 24U);
    dst[18] = static_cast<uint8_t>(message.selected_source_mask);
    dst[19] = static_cast<uint8_t>(message.selected_source_mask >> 8U);
    dst[20] = message.stream_interval_ms;
    return true;
}

inline bool start_session_v2_decode(const uint8_t *src, size_t src_len,
                                    StartSessionV2Message &message)
{
    if (src == nullptr || src_len != kStartSessionV2WireSize ||
            src[0] != static_cast<uint8_t>(RecordCommand::StartSessionV2) ||
            src[1] != kStartSessionV2ProtocolVersion) {
        return false;
    }
    message.command = RecordCommand::StartSessionV2;
    message.protocol_version = src[1];
    message.session_id = static_cast<uint32_t>(src[2]) |
                         (static_cast<uint32_t>(src[3]) << 8U) |
                         (static_cast<uint32_t>(src[4]) << 16U) |
                         (static_cast<uint32_t>(src[5]) << 24U);
    message.start_timestamp_us = 0U;
    for (uint8_t i = 0U; i < 8U; ++i) {
        message.start_timestamp_us |= static_cast<uint64_t>(src[6U + i]) << (8U * i);
    }
    message.safety_duration_ms = static_cast<uint32_t>(src[14]) |
                                 (static_cast<uint32_t>(src[15]) << 8U) |
                                 (static_cast<uint32_t>(src[16]) << 16U) |
                                 (static_cast<uint32_t>(src[17]) << 24U);
    message.selected_source_mask = static_cast<SourceMask>(src[18]) |
                                   (static_cast<SourceMask>(src[19]) << 8U);
    message.stream_interval_ms = src[20];
    return true;
}

struct RecordDoneMessage {
    RecordCommand command;
    uint16_t node_id;
    uint32_t session_id;
    uint32_t actual_duration_ms;
    uint32_t total_size;
    uint32_t payload_crc32;
};

struct SessionChunkHeader {
    RecordCommand command;
    uint32_t session_id;
    uint32_t offset;
    uint16_t payload_size;
    uint16_t sequence;
};

struct ChunkAckMessage {
    RecordCommand command;
    uint32_t session_id;
    uint16_t sequence;
    uint32_t next_offset;
};

struct ChunkAckRangeMessage {
    RecordCommand command;
    uint32_t session_id;
    uint16_t flags;       /* bit0: missing range valid */
    uint32_t next_offset; /* highest contiguous offset received by host */
    uint32_t missing_start;
    uint16_t missing_len;
};

struct ChunkAckV3Message {
    RecordCommand command;
    uint8_t proto_version;
    uint16_t source_id;
    uint16_t flags; /* bit0: missing range valid */
    uint32_t session_id;
    uint32_t next_offset; /* highest contiguous offset received by host */
    uint32_t missing_start;
    uint16_t missing_len;
};

/* Compact ACK format (proto_version=4), no missing range payload */
struct ChunkAckCompactMessage {
    RecordCommand command;
    uint8_t proto_version; /* currently 4 */
    uint16_t flags;        /* bit0: recovery/request retransmit */
    uint32_t session_id;
    uint32_t next_offset;  /* highest contiguous offset received by host */
};

/* Compact ACK with explicit source id */
struct ChunkAckCompactSourceMessage {
    RecordCommand command;
    uint8_t proto_version; /* currently 4 */
    uint16_t flags;        /* bit0: recovery/request retransmit */
    uint32_t session_id;
    uint32_t next_offset;  /* highest contiguous offset received by host */
    uint16_t source_id;
};

struct RecordLaneFrameV3Header {
    RecordCommand command;   /* LaneFrameV3 */
    uint8_t proto_version;   /* currently 3 */
    uint32_t session_id;
    uint16_t source_id;
    uint8_t lane_id;
    uint8_t msg_type;
    uint16_t sequence;
    uint32_t offset;
    uint16_t payload_len;
    uint16_t flags;
};

struct RecordReliableFrameHeader {
    RecordCommand command;       /* ReliableFrame */
    uint8_t proto_version;       /* kRecordReliableProtoVersion */
    uint16_t magic;              /* kRecordReliableMagic */
    uint8_t frame_type;          /* RecordReliableType */
    uint16_t source_id;
    uint32_t session_id;
    uint32_t chunk_index;
    uint32_t byte_offset;
    uint16_t payload_len;
    uint16_t payload_crc16;
    uint16_t flags;
};

struct RecordReliableManifestPayload {
    uint8_t protocol_version;
    uint8_t reserved0;
    uint16_t source_id;
    uint32_t session_id;
    uint32_t file_size;
    uint16_t chunk_size;
    uint16_t total_chunks;
    uint32_t file_crc32;
    uint32_t duration_ms;
    uint32_t timestamp_lo;
    uint32_t flags;
};

struct RecordReliableManifestAckPayload {
    uint16_t source_id;
    uint32_t session_id;
    uint16_t accepted_chunk_size;
    uint8_t credit;
    uint8_t status;
};

struct RecordReliableAckWindowPayload {
    uint16_t source_id;
    uint32_t session_id;
    uint32_t next_chunk_index;
    uint8_t credit;
    uint8_t reserved0;
    uint16_t flags;
};

struct RecordReliableNackRangePayload {
    uint16_t source_id;
    uint32_t session_id;
    uint32_t first_chunk_index;
    uint16_t chunk_count;
    uint16_t flags;
};

struct RecordReliableVerifyPayload {
    uint16_t source_id;
    uint32_t session_id;
    uint32_t file_crc32;
    uint32_t first_bad_chunk;
    uint16_t flags;
};

struct SessionCompleteAckMessage {
    RecordCommand command;
    uint32_t session_id;
    uint32_t payload_crc32;
};
#pragma pack(pop)

} // namespace exo

#endif
