#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace VisitorsLabPlatforms {
bool Install(HMODULE module);

// GMS092 CField::UpdateDynamicObject (528050): speed is milliseconds per
// pixel. Reaching an endpoint clamps the position and reverses that axis.
// Keep the original discarded remainder and first-tick behavior.
struct Motion {
    int x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, speed = 0;
    bool reverseX = false, reverseY = false;
    std::uint32_t last = 0;
    bool started = false;
    bool Step(std::uint32_t now) {
        if (!started) { started = true; last = now; return false; }
        const auto elapsed = now - last;
        if (speed <= 0 || elapsed < static_cast<unsigned>(speed)) return false;
        last = now;
        const auto step = static_cast<std::int64_t>(elapsed / speed);
        const int oldX = x, oldY = y;
        auto advance = [step](int& position, int a, int b, bool& reverse, bool vertical) {
            if (a == b) return;
            const auto low = (std::min)(a, b), high = (std::max)(a, b);
            auto next = static_cast<std::int64_t>(position) + ((reverse != vertical) ? -step : step);
            if (next < low) { next = low; reverse = vertical; }
            if (next > high) { next = high; reverse = !vertical; }
            position = static_cast<int>(next);
        };
        advance(x, x1, x2, reverseX, false);
        advance(y, y1, y2, reverseY, true);
        return x != oldX || y != oldY;
    }
};
}
