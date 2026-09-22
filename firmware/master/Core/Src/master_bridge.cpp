#include "master_bridge.h"

#include <cstring>

#include "main.h"
#include "rtc.h"
#include "usart.h"

#include <exo/ble/custom_app.h>
#include <exo/ble/exo_hub_central_client.h>
#include <exo/ble/exo_hub_leaf_bridge.h>
#include <exo/bridge/frame_codec.h>
#include <exo/bridge/queues.h>
#include <exo/bridge/sequence_tracker.h>
#include <exo/bridge/stream_decoder.h>
#include <exo/protocol/blepipe_proto.h>
#include <exo/protocol/live_bundle_v2.h>
#include <exo/types/topology.h>

namespace {

constexpr size_t kRxDmaBytes = 1024U;
constexpr size_t kLiveSources = 13U;
constexpr size_t kReliableDepth = 8U;
constexpr uint32_t kStatusPeriodMs = 5000U;

using Decoder = exo::bridge::StreamDecoder<>;
using LiveQueue = exo::bridge::LatestValueSlots<kLiveSources,
                                                exo::bridge::kMaxEncodedFrameLength>;
using ReliableQueue = exo::bridge::ReliableFifo<kReliableDepth,
                                                exo::bridge::kMaxEncodedFrameLength>;

uint8_t g_rx_dma[kRxDmaBytes]{};
size_t g_rx_read = 0U;
uint32_t g_boot_epoch = 1U;
uint32_t g_live_sequence = 1U;
uint32_t g_reliable_sequence = 1U;
uint32_t g_blepipe_sequence = 1U;
uint32_t g_last_status_ms = 0U;
bool g_tx_busy = false;
uint8_t g_tx_buffer[exo::bridge::kMaxEncodedFrameLength]{};
uint16_t g_tx_length = 0U;
Decoder g_decoder;
LiveQueue g_live_queue;
ReliableQueue g_reliable_queue;
exo::bridge::SequenceTracker g_rx_sequences;

uint32_t next_boot_epoch()
{
    uint32_t epoch = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
    ++epoch;
    if (epoch == 0U) epoch = 1U;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, epoch);
    return epoch;
}

uint8_t node_id_from_pipe(const blepipe_hdr_t &header)
{
    const uint16_t ids[] = {header.src_id, header.dst_id};
    for (const uint16_t id : ids) {
        if (id >= BLEPIPE_ID_LEAF_1 && id <= (BLEPIPE_ID_LEAF_1 + 11U)) {
            return static_cast<uint8_t>((id - BLEPIPE_ID_LEAF_1) + 1U);
        }
        if (id >= 1U && id <= 12U) {
            return static_cast<uint8_t>(id);
        }
    }
    return 0U;
}

exo::bridge::Lane lane_for_message(uint8_t msg_type)
{
    return msg_type == BLEPIPE_MSG_LEAF_SAMPLE
        ? exo::bridge::Lane::Live : exo::bridge::Lane::Reliable;
}

bool queue_encoded(exo::bridge::Lane lane, const uint8_t *packet,
                   uint16_t packet_length)
{
    if (packet == nullptr || packet_length == 0U ||
        packet_length > BLEPIPE_MAX_NOTIFY_PAYLOAD) return false;

    exo::bridge::Frame frame{};
    frame.lane = lane;
    frame.boot_epoch = g_boot_epoch;
    frame.sequence = lane == exo::bridge::Lane::Live
        ? g_live_sequence++ : g_reliable_sequence++;
    frame.payload = packet;
    frame.payload_length = packet_length;

    uint8_t encoded[exo::bridge::kMaxEncodedFrameLength]{};
    size_t encoded_length = 0U;
    if (exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) !=
        exo::bridge::Status::Ok) return false;

    if (lane == exo::bridge::Lane::Live) {
        blepipe_hdr_t header{};
        const uint8_t *body = nullptr;
        uint16_t body_length = 0U;
        const uint8_t source =
            blepipe_decode(packet, packet_length, &header, &body, &body_length) ==
                BLEPIPE_STATUS_OK ? node_id_from_pipe(header) : 0U;
        return g_live_queue.publish(source, encoded,
                                    static_cast<uint16_t>(encoded_length),
                                    frame.sequence, HAL_GetTick());
    }
    return g_reliable_queue.push(frame.sequence, encoded,
                                 static_cast<uint16_t>(encoded_length));
}

bool build_and_queue(uint8_t msg_type, uint16_t src_id, uint16_t dst_id,
                     const uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > BLEPIPE_MAX_APP_PAYLOAD) return false;
    blepipe_hdr_t header{};
    header.proto_ver = BLEPIPE_PROTO_VER;
    header.msg_type = msg_type;
    header.src_id = src_id;
    header.dst_id = dst_id;
    header.seq = g_blepipe_sequence++;
    header.timestamp_ms = HAL_GetTick();
    header.payload_len = payload_len;
    uint8_t packet[BLEPIPE_MAX_NOTIFY_PAYLOAD]{};
    size_t packet_length = 0U;
    if (blepipe_encode(packet, sizeof(packet), &header, payload, payload_len,
                       &packet_length) != BLEPIPE_STATUS_OK) return false;
    return queue_encoded(lane_for_message(msg_type), packet,
                         static_cast<uint16_t>(packet_length));
}

void ingest_live(uint8_t node_id, const uint8_t *payload, uint16_t length)
{
    if (node_id == 0U || payload == nullptr) return;
    if (length >= exo::kLiveBundleV2HeaderSize &&
        payload[0] == exo::kLiveBundleV2Marker) {
        exo::LiveBundleV2Message<96U> bundle{};
        if (!exo::live_bundle_v2_decode(payload, length, bundle)) return;
        if (bundle.bno_payload_length != 0U) {
            (void)exo_hub_leaf_stream_ingest_at(
                node_id, 1U, bundle.bno_payload, bundle.bno_payload_length,
                bundle.bno_acquired_ms);
        }
        if (bundle.icm_payload_length != 0U) {
            (void)exo_hub_leaf_stream_ingest_at(
                node_id, 2U, bundle.icm_payload, bundle.icm_payload_length,
                bundle.icm_acquired_ms);
        }
        return;
    }
    if (length >= 3U && payload[0] == 0x03U) {
        const uint8_t bno_length = payload[1];
        uint16_t offset = 2U;
        if (bno_length != 0U && offset + bno_length <= length) {
            (void)exo_hub_leaf_stream_ingest(node_id, 1U,
                                              payload + offset, bno_length);
        }
        offset = static_cast<uint16_t>(offset + bno_length);
        if (offset < length) {
            const uint8_t icm_length = payload[offset++];
            if (icm_length != 0U && offset + icm_length <= length) {
                (void)exo_hub_leaf_stream_ingest(node_id, 2U,
                                                  payload + offset, icm_length);
            }
        }
        return;
    }
    if (length > 1U) {
        (void)exo_hub_leaf_stream_ingest(node_id, payload[0], payload + 1U,
                                          static_cast<uint8_t>(length - 1U));
    }
}

void dispatch(const exo::bridge::Frame &frame, const uint8_t *packet)
{
    blepipe_hdr_t header{};
    const uint8_t *payload = nullptr;
    uint16_t payload_length = 0U;
    if (blepipe_decode(packet, frame.payload_length, &header, &payload,
                       &payload_length) != BLEPIPE_STATUS_OK) return;
    if (header.msg_type == BLEPIPE_MSG_TOPOLOGY_V2 &&
        payload_length == BLEPIPE_TOPOLOGY_V2_PAYLOAD_LEN) {
        blepipe_topology_v2_t topology{};
        if (blepipe_topology_v2_decode(payload, payload_length, &topology) ==
            BLEPIPE_STATUS_OK && topology.hub_id == static_cast<uint8_t>(exo::HubId::Lower)) {
            for (uint8_t node_id = 7U; node_id <= 12U; ++node_id) {
                const uint16_t bit = static_cast<uint16_t>(1U << node_id);
                if ((topology.present_source_mask & bit) != 0U) {
                    exo_hub_leaf_topology_touch(node_id);
                }
            }
        }
        return;
    }
    const uint8_t node_id = node_id_from_pipe(header);
    if (node_id == 0U) return;

    switch (header.msg_type) {
    case BLEPIPE_MSG_LEAF_SAMPLE:
        ingest_live(node_id, payload, payload_length);
        break;
    case BLEPIPE_MSG_RAW_FORWARD:
        /* The lower hub relays both RecordDone manifests and reliable chunks
         * on this lane. Feed the manifest path first; non-manifest chunks are
         * harmlessly rejected there and continue to the transfer coordinator. */
        (void)exo_hub_leaf_record_done_ingest(payload, payload_length);
        exo_hub_leaf_record_frame_ingest(node_id, payload, payload_length);
        (void)Custom_APP_SendRecordFrame(payload,
                                         static_cast<uint8_t>(payload_length));
        break;
    case BLEPIPE_MSG_COMMAND_RESP:
    case BLEPIPE_MSG_ACK:
    case BLEPIPE_MSG_NACK:
        exo_hub_leaf_control_ingest(node_id, header.msg_type, payload,
                                     payload_length);
        (void)Custom_APP_SendCmdNotify(packet,
                                       static_cast<uint8_t>(frame.payload_length));
        break;
    default:
        exo_hub_leaf_control_ingest(node_id, header.msg_type, payload,
                                     payload_length);
        (void)Custom_APP_SendRecoveryFrame(packet,
                                            static_cast<uint8_t>(frame.payload_length));
        break;
    }
}

void service_rx()
{
    if (huart1.hdmarx == nullptr) return;
    const size_t write_index =
        kRxDmaBytes - __HAL_DMA_GET_COUNTER(huart1.hdmarx);
    while (g_rx_read != write_index) {
        const uint8_t byte = g_rx_dma[g_rx_read];
        g_rx_read = (g_rx_read + 1U) % kRxDmaBytes;
        if (!g_decoder.push(byte)) continue;
        exo::bridge::Frame frame{};
        uint8_t packet[BLEPIPE_MAX_NOTIFY_PAYLOAD]{};
        if (!g_decoder.pop(frame, packet, sizeof(packet))) continue;
        const auto result = g_rx_sequences.observe(frame.boot_epoch,
                                                   frame.sequence);
        if (result == exo::bridge::SequenceResult::Duplicate ||
            result == exo::bridge::SequenceResult::Reordered) continue;
        dispatch(frame, packet);
    }
}

void service_tx()
{
    if (g_tx_busy) return;
    ReliableQueue::Value reliable{};
    if (g_reliable_queue.pop(reliable)) {
        std::memcpy(g_tx_buffer, reliable.payload, reliable.payload_length);
        g_tx_length = reliable.payload_length;
    } else {
        LiveQueue::Value live{};
        if (!g_live_queue.pop_next(live, HAL_GetTick())) return;
        std::memcpy(g_tx_buffer, live.payload, live.payload_length);
        g_tx_length = live.payload_length;
    }
    if (HAL_UART_Transmit_DMA(&huart1, g_tx_buffer, g_tx_length) == HAL_OK) {
        g_tx_busy = true;
    }
}

}  // namespace

extern "C" void exo_master_bridge_init(void)
{
    g_boot_epoch = next_boot_epoch();
    g_rx_read = 0U;
    g_tx_busy = false;
    g_live_sequence = 1U;
    g_reliable_sequence = 1U;
    g_blepipe_sequence = 1U;
    (void)HAL_UART_Receive_DMA(&huart1, g_rx_dma, sizeof(g_rx_dma));
}

extern "C" void exo_master_bridge_process(void)
{
    service_rx();
    service_tx();
    const uint32_t now = HAL_GetTick();
    if (now - g_last_status_ms >= kStatusPeriodMs) {
        g_last_status_ms = now;
        const Decoder::Counters &counters = g_decoder.counters();
        exo_ble_debug_printf("[BRIDGE][U9] epoch=%lu rx=%lu bad=%lu qlive=%u qrel=%u ovw=%lu relrej=%lu\r\n",
                             static_cast<unsigned long>(g_boot_epoch),
                             static_cast<unsigned long>(counters.frames),
                             static_cast<unsigned long>(counters.malformed + counters.oversized),
                             static_cast<unsigned>(g_live_queue.pending_count()),
                             static_cast<unsigned>(g_reliable_queue.count()),
                             static_cast<unsigned long>(g_live_queue.overwrite_count()),
                             static_cast<unsigned long>(g_reliable_queue.rejected_count()));
    }
}

extern "C" uint8_t exo_master_bridge_send_blepipe(uint8_t msg_type,
                                                   uint16_t src_id,
                                                   uint16_t dst_id,
                                                   const uint8_t *payload,
                                                   uint16_t payload_len)
{
    return build_and_queue(msg_type, src_id, dst_id, payload, payload_len) ? 1U : 0U;
}

extern "C" void exo_master_bridge_uart_error(UART_HandleTypeDef *uart)
{
    if (uart != &huart1) return;
    g_tx_busy = false;
    (void)HAL_UART_AbortReceive(uart);
    (void)HAL_UART_Receive_DMA(uart, g_rx_dma, sizeof(g_rx_dma));
    g_rx_read = 0U;
}
