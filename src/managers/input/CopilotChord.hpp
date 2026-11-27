#pragma once

#include "../../devices/IKeyboard.hpp"

// ASUS Copilot is a firmware chord: Shift_L, Super_L, then F23
// (keysym XF86TouchpadOff). The bind consumes F23. Shift and Super
// would still be delivered to the focused client. Hold them briefly
// and drop the whole chord so that client sees nothing.

namespace CopilotChord {
    bool     intercept(SP<IKeyboard> keyboard, const IKeyboard::SKeyEvent& event, bool passEvent);
    bool     hidingMods();
    uint32_t stripHidden(SP<IKeyboard> keyboard, uint32_t xkbMods);
    void     flushPending();
}
