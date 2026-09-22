#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/ble/link_tune_state.h>

namespace {

void complete_fast_tune(exo::LinkTuneState &model, uint8_t link,
                        uint16_t handle, uint32_t generation, uint32_t now)
{
    exo::LinkTuneState::Request request = model.issue_next(now);
    assert(request.valid() && request.link == link && request.handle == handle);
    assert(request.procedure == exo::LinkTuneState::Procedure::Dle);
    assert(model.on_request_accepted(request, now));
    assert(model.on_dle_complete(handle, generation, 251U, 251U, now));

    request = model.issue_next(now);
    assert(request.valid() && request.procedure == exo::LinkTuneState::Procedure::Phy);
    assert(model.on_request_accepted(request, now));
    assert(model.on_phy_complete(handle, generation,
                                 exo::LinkTuneState::kStatusSuccess, 2U, 2U, now));
}

}  // namespace

int main()
{
    exo::LinkTuneState model;
    uint32_t now = 100U;
    for (uint8_t link = 0U; link < 6U; ++link) {
        assert(model.connect(link, static_cast<uint16_t>(0x0140U + link), now));
    }
    now += exo::LinkTuneState::kCommissioningStartDelayMs;
    for (uint8_t link = 0U; link < 6U; ++link) {
        complete_fast_tune(model, link, static_cast<uint16_t>(0x0140U + link),
                           model.telemetry(link).generation, now);
        assert(model.telemetry(link).state == exo::LinkTuneState::State::Ready);
    }
    assert(!model.issue_next(now).valid());
    std::cout << "six-link tune checks passed\n";
    return 0;
}
