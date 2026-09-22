#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "main.h"
#include "rtc.h"
#include "usart.h"

#include <exo/bridge/frame_codec.h>
#include <exo/bridge/queues.h>
#include <exo/bridge/sequence_tracker.h>
#include <exo/bridge/stream_decoder.h>
#include <exo/protocol/blepipe_proto.h>
#include <exo/ble/exo_hub_central_client.h>

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
uint32_t g_last_status_ms = 0U;
bool g_tx_busy = false;
uint8_t g_tx_buffer[exo::bridge::kMaxEncodedFrameLength]{};
uint16_t g_tx_length = 0U;
Decoder g_decoder;
LiveQueue g_live_queue;
ReliableQueue g_reliable_queue;
exo::bridge::SequenceTracker g_rx_sequences;

extern "C" void exo_ble_debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

uint32_t next_boot_epoch()
{
#if defined(RTC_BKP_DR0)
    uint32_t epoch = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
    ++epoch;
    if (epoch == 0U) epoch = 1U;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, epoch);
    return epoch;
#else
    return 1U;
#endif
}

uint8_t source_id_from_pipe(const uint8_t *packet, uint16_t length)
{
    blepipe_hdr_t header{};
    const uint8_t *payload = nullptr;
    uint16_t payload_length = 0U;
    if (blepipe_decode(packet, length, &header, &payload, &payload_length) != BLEPIPE_STATUS_OK) {
        return 0U;
    }
    if (header.src_id >= BLEPIPE_ID_LEAF_1 &&
        header.src_id <= BLEPIPE_ID_LEAF_1 + 11U) {
        return static_cast<uint8_t>((header.src_id - BLEPIPE_ID_LEAF_1) + 1U);
    }
    if (header.dst_id >= BLEPIPE_ID_LEAF_1 &&
        header.dst_id <= BLEPIPE_ID_LEAF_1 + 11U) {
        return static_cast<uint8_t>((header.dst_id - BLEPIPE_ID_LEAF_1) + 1U);
    }
    if (header.src_id <= 12U) return static_cast<uint8_t>(header.src_id);
    if (header.dst_id <= 12U) return static_cast<uint8_t>(header.dst_id);
    return 0U;
}

bool queue_blepipe(exo::bridge::Lane lane, const uint8_t *packet, uint16_t length)
{
    if (packet == nullptr || length == 0U || length > BLEPIPE_MAX_NOTIFY_PAYLOAD) return false;
    const uint8_t source = source_id_from_pipe(packet, length);
    exo::bridge::Frame frame{};
    frame.lane = lane;
    frame.boot_epoch = g_boot_epoch;
    frame.sequence = lane == exo::bridge::Lane::Live ? g_live_sequence++ : g_reliable_sequence++;
    frame.payload = packet;
    frame.payload_length = length;

    uint8_t encoded[exo::bridge::kMaxEncodedFrameLength]{};
    size_t encoded_length = 0U;
    if (exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) !=
        exo::bridge::Status::Ok) return false;

    if (lane == exo::bridge::Lane::Live) {
        return g_live_queue.publish(source, encoded, static_cast<uint16_t>(encoded_length),
                                    frame.sequence, HAL_GetTick());
    }
    return g_reliable_queue.push(frame.sequence, encoded,
                                 static_cast<uint16_t>(encoded_length));
}

void dispatch_incoming(const exo::bridge::Frame &frame, const uint8_t *payload)
{
    blepipe_hdr_t header{};
    const uint8_t *body = nullptr;
    uint16_t body_length = 0U;
    if (blepipe_decode(payload, frame.payload_length, &header, &body, &body_length) !=
        BLEPIPE_STATUS_OK) return;

    const uint16_t destination = header.dst_id;
    if (destination == BLEPIPE_ID_BROADCAST) {
        for (uint8_t node = 7U; node <= 12U; ++node) {
            (void)exo_hub_central_client_send_blepipe_to_node(
                node, header.msg_type, header.src_id, body, body_length);
        }
        return;
    }
    uint8_t node_id = 0U;
    if (destination >= BLEPIPE_ID_LEAF_1 &&
        destination <= BLEPIPE_ID_LEAF_1 + 11U) {
        node_id = static_cast<uint8_t>((destination - BLEPIPE_ID_LEAF_1) + 1U);
    } else if (destination >= 7U && destination <= 12U) {
        node_id = static_cast<uint8_t>(destination);
    }
    if (node_id >= 7U && node_id <= 12U) {
        (void)exo_hub_central_client_send_blepipe_to_node(
            node_id, header.msg_type, header.src_id,
            body, body_length);
    }
}

void service_rx()
{
    if (huart1.hdmarx == nullptr) return;
    const size_t write_index = kRxDmaBytes - __HAL_DMA_GET_COUNTER(huart1.hdmarx);
    while (g_rx_read != write_index) {
        const uint8_t byte = g_rx_dma[g_rx_read];
        g_rx_read = (g_rx_read + 1U) % kRxDmaBytes;
        if (g_decoder.push(byte)) {
            exo::bridge::Frame frame{};
            uint8_t payload[BLEPIPE_MAX_NOTIFY_PAYLOAD]{};
            if (g_decoder.pop(frame, payload, sizeof(payload))) {
                const exo::bridge::SequenceResult result =
                    g_rx_sequences.observe(frame.boot_epoch, frame.sequence);
                if (result != exo::bridge::SequenceResult::Duplicate &&
                    result != exo::bridge::SequenceResult::Reordered) {
                    dispatch_incoming(frame, payload);
                }
            }
        }
    }
}

void service_tx()
{
    if (g_tx_busy) return;
    exo::bridge::ReliableFifo<kReliableDepth,
                              exo::bridge::kMaxEncodedFrameLength>::Value reliable{};
    if (g_reliable_queue.pop(reliable)) {
        memcpy(g_tx_buffer, reliable.payload, reliable.payload_length);
        g_tx_length = reliable.payload_length;
    } else {
        LiveQueue::Value live{};
        if (!g_live_queue.pop_next(live, HAL_GetTick())) return;
        memcpy(g_tx_buffer, live.payload, live.payload_length);
        g_tx_length = live.payload_length;
    }
    if (HAL_UART_Transmit_DMA(&huart1, g_tx_buffer, g_tx_length) == HAL_OK) {
        g_tx_busy = true;
    }
}

}  // namespace

extern "C" void exo_lower_hub_bridge_init(void)
{
    g_boot_epoch = next_boot_epoch();
    g_rx_read = 0U;
    g_tx_busy = false;
    g_live_sequence = 1U;
    g_reliable_sequence = 1U;
    (void)HAL_UART_Receive_DMA(&huart1, g_rx_dma, sizeof(g_rx_dma));
    exo_ble_debug_printf("[BRIDGE][U11] init epoch=%lu baud=921600 rx=%u\r\n",
                         static_cast<unsigned long>(g_boot_epoch),
                         static_cast<unsigned>(sizeof(g_rx_dma)));
}

extern "C" void exo_lower_hub_bridge_process(void)
{
    service_rx();
    service_tx();
    const uint32_t now = HAL_GetTick();
    if (now - g_last_status_ms >= kStatusPeriodMs) {
        g_last_status_ms = now;
        const Decoder::Counters &counters = g_decoder.counters();
        exo_ble_debug_printf("[BRIDGE][U11] epoch=%lu rx=%lu bad=%lu qlive=%u qrel=%u ovw=%lu relrej=%lu\r\n",
                             static_cast<unsigned long>(g_boot_epoch),
                             static_cast<unsigned long>(counters.frames),
                             static_cast<unsigned long>(counters.malformed + counters.oversized),
                             static_cast<unsigned>(g_live_queue.pending_count()),
                             static_cast<unsigned>(g_reliable_queue.count()),
                             static_cast<unsigned long>(g_live_queue.overwrite_count()),
                             static_cast<unsigned long>(g_reliable_queue.rejected_count()));
    }
}

extern "C" uint8_t exo_lower_hub_bridge_forward(uint8_t lane,
                                                  const uint8_t *payload,
                                                  uint16_t payload_length)
{
    return queue_blepipe(static_cast<exo::bridge::Lane>(lane), payload, payload_length) ? 1U : 0U;
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart1) g_tx_busy = false;
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart1) {
        g_tx_busy = false;
        (void)HAL_UART_AbortReceive(uart);
        (void)HAL_UART_Receive_DMA(uart, g_rx_dma, sizeof(g_rx_dma));
        g_rx_read = 0U;
    }
}

extern "C" void exo_hub_leaf_control_ingest(uint8_t, uint8_t,
                                             const uint8_t *, uint16_t) {}
extern "C" uint8_t exo_hub_leaf_record_done_ingest(const uint8_t *, uint16_t) { return 0U; }
extern "C" void exo_hub_leaf_record_frame_ingest(uint8_t, const uint8_t *, uint16_t) {}
extern "C" uint8_t exo_hub_leaf_stream_ingest(uint8_t, uint8_t,
                                               const uint8_t *, uint8_t) { return 0U; }
extern "C" uint8_t exo_hub_leaf_stream_ingest_at(uint8_t, uint8_t,
                                                  const uint8_t *, uint8_t,
                                                  uint32_t) { return 0U; }
extern "C" void exo_hub_leaf_topology_touch(uint8_t) {}
extern "C" uint8_t exo_master_training_owns_node_link(uint8_t) { return 0U; }
extern "C" uint8_t exo_master_training_raw_download_debug_enabled(void) { return 0U; }
extern "C" void exo_master_training_note_suppressed_relay(void) {}
