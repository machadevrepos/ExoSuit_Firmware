#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/bridge/link_health.h>

int main()
{
    using exo::bridge::LinkHealth;
    using exo::bridge::LinkState;
    using exo::bridge::SequenceResult;

    // Starts Absent with no traffic, then Booting on the first frame, and
    // settles to Healthy once the boot hold window elapses.
    {
        LinkHealth health(15000U, 2000U, 3000U);
        assert(health.state() == LinkState::Absent);
        assert(health.age_ms(1000U) == 0xFFFFFFFFU);

        health.on_frame(SequenceResult::First, 1000U);
        assert(health.state() == LinkState::Booting);

        health.tick(1500U, false);
        assert(health.state() == LinkState::Booting);

        health.tick(3200U, false);
        assert(health.state() == LinkState::Healthy);
        assert(health.age_ms(3200U) == 2200U);
    }

    // Congestion is reported once booted, and clears the moment the caller
    // reports the link is no longer congested.
    {
        LinkHealth health(15000U, 2000U, 3000U);
        health.on_frame(SequenceResult::First, 0U);
        health.tick(2001U, true);
        assert(health.state() == LinkState::Congested);
        health.tick(2500U, false);
        assert(health.state() == LinkState::Healthy);
    }

    // An epoch change after the boot hold has elapsed reports Restarted,
    // bumps restart_count, and settles back to Healthy after its own hold.
    {
        LinkHealth health(15000U, 2000U, 3000U);
        health.on_frame(SequenceResult::First, 0U);
        health.tick(2500U, false);
        assert(health.state() == LinkState::Healthy);

        health.on_frame(SequenceResult::EpochChanged, 5000U);
        assert(health.state() == LinkState::Restarted);
        assert(health.restart_count() == 1U);

        health.tick(6000U, false);
        assert(health.state() == LinkState::Restarted);

        health.tick(8001U, false);
        assert(health.state() == LinkState::Healthy);
    }

    // Silence past the absent timeout reports Absent even with no epoch
    // change. A later frame that carries no First/EpochChanged edge (the
    // peer's own sequence tracker never lost continuity, only the UART did)
    // recovers straight to Healthy rather than re-entering Booting - only a
    // genuine First contact or peer epoch change should look like a boot.
    {
        LinkHealth health(15000U, 2000U, 3000U);
        health.on_frame(SequenceResult::First, 0U);
        health.tick(2500U, false);
        assert(health.state() == LinkState::Healthy);

        health.tick(20000U, false);
        assert(health.state() == LinkState::Absent);

        health.on_frame(SequenceResult::Gap, 20100U);
        assert(health.state() == LinkState::Healthy);
    }

    // Ordinary in-order/gap traffic never re-triggers Booting or Restarted
    // once the link is settled.
    {
        LinkHealth health(15000U, 2000U, 3000U);
        health.on_frame(SequenceResult::First, 0U);
        health.tick(2500U, false);
        health.on_frame(SequenceResult::InOrder, 3000U);
        assert(health.state() == LinkState::Healthy);
        health.on_frame(SequenceResult::Gap, 4000U);
        assert(health.state() == LinkState::Healthy);
        assert(health.restart_count() == 0U);
    }

    std::cout << "bridge link health checks passed\n";
    return 0;
}
