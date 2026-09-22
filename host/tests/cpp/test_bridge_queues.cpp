#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/bridge/queues.h>

int main()
{
    using Live = exo::bridge::LatestValueSlots<13U, 8U>;
    Live live;
    const uint8_t first[] = {1U, 2U};
    const uint8_t latest[] = {9U, 8U, 7U};
    const uint8_t other[] = {4U};
    assert(live.publish(8U, first, sizeof(first), 10U, 100U));
    assert(live.publish(8U, latest, sizeof(latest), 11U, 110U));
    assert(live.publish(9U, other, sizeof(other), 12U, 120U));
    assert(live.overwrite_count() == 1U);
    assert(live.pending_count() == 2U);

    Live::Value value{};
    assert(live.pop_next(value, 150U));
    assert(value.source_id == 8U);
    assert(value.sequence == 11U);
    assert(value.payload_length == sizeof(latest));
    assert(value.payload[0] == 9U);
    assert(value.age_ms == 40U);
    assert(live.pop_next(value, 150U));
    assert(value.source_id == 9U);
    assert(value.age_ms == 30U);
    assert(!live.pop_next(value, 150U));

    using Reliable = exo::bridge::ReliableFifo<2U, 8U>;
    Reliable reliable;
    const uint8_t reliable_a[] = {0xA0U};
    const uint8_t reliable_b[] = {0xB0U, 0xB1U};
    assert(reliable.push(20U, reliable_a, sizeof(reliable_a)));
    assert(reliable.push(21U, reliable_b, sizeof(reliable_b)));
    assert(!reliable.push(22U, reliable_a, sizeof(reliable_a)));
    assert(reliable.high_watermark() == 2U);
    Reliable::Value reliable_value{};
    assert(reliable.peek(reliable_value));
    assert(reliable_value.sequence == 20U);
    assert(reliable.pop(reliable_value));
    assert(reliable_value.payload[0] == 0xA0U);
    assert(reliable.pop(reliable_value));
    assert(reliable_value.sequence == 21U);
    assert(!reliable.pop(reliable_value));

    std::cout << "bridge queue checks passed\n";
    return 0;
}
