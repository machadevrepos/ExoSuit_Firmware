#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/bridge/time_sync.h>

int main()
{
    exo::bridge::TimeSyncEstimator sync;
    assert(!sync.initialized());
    assert(sync.add_sample(100U, 120U, 115U));
    assert(sync.initialized());
    assert(sync.sample_count() == 1U);
    assert(sync.offset_ms() == 5);
    assert(sync.round_trip_ms() == 20U);
    assert(sync.to_peer_time(200U) == 205U);
    assert(sync.to_local_time(205U) == 200U);

    assert(sync.add_sample(200U, 220U, 219U));
    assert(sync.sample_count() == 2U);
    assert(sync.offset_ms() == 7);
    assert(sync.uncertainty_ms() >= 10U);

    exo::bridge::TimeSyncEstimator wrap_sync;
    assert(wrap_sync.add_sample(0xFFFFFFF0U, 0x00000010U, 5U));
    assert(wrap_sync.round_trip_ms() == 32U);
    assert(wrap_sync.offset_ms() == 5);

    std::cout << "bridge time-sync checks passed\n";
    return 0;
}
