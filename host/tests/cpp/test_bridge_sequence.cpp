#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/bridge/sequence_tracker.h>

int main()
{
    exo::bridge::SequenceTracker tracker;
    assert(tracker.observe(7U, 100U) == exo::bridge::SequenceResult::First);
    assert(tracker.observe(7U, 101U) == exo::bridge::SequenceResult::InOrder);
    assert(tracker.observe(7U, 103U) == exo::bridge::SequenceResult::Gap);
    assert(tracker.gap_count() == 1U);
    assert(tracker.observe(7U, 103U) == exo::bridge::SequenceResult::Duplicate);
    assert(tracker.observe(7U, 102U) == exo::bridge::SequenceResult::Reordered);
    assert(tracker.observe(8U, 1U) == exo::bridge::SequenceResult::EpochChanged);
    assert(tracker.observe(8U, 2U) == exo::bridge::SequenceResult::InOrder);

    exo::bridge::SequenceTracker wrap_tracker;
    assert(wrap_tracker.observe(1U, 0xFFFFFFFFU) == exo::bridge::SequenceResult::First);
    assert(wrap_tracker.observe(1U, 0U) == exo::bridge::SequenceResult::InOrder);

    std::cout << "bridge sequence checks passed\n";
    return 0;
}
