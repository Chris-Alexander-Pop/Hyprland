#include "CopilotChord.hpp"

#include "../../managers/eventLoop/EventLoopManager.hpp"
#include "../../managers/eventLoop/EventLoopTimer.hpp"
#include "InputManager.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <unordered_map>
#include <vector>

using namespace std::chrono_literals;

static constexpr auto HOLD = 180ms;

enum class eKind : uint8_t {
    Other = 0,
    Shift,
    Super,
    Copilot,
};

struct SPending {
    bool                                shiftDown = false;
    bool                                superDown = false;
    bool                                shiftSent = false;
    bool                                superSent = false;
    bool                                swallow   = false;
    std::optional<IKeyboard::SKeyEvent> shiftPress;
    std::optional<IKeyboard::SKeyEvent> superPress;
    SP<CEventLoopTimer>                 timer;
    WP<IKeyboard>                       keyboard;
};

static std::unordered_map<IKeyboard*, SPending> g_pending;

static eKind                                    kindOf(SP<IKeyboard> keyboard, uint32_t evdevKey) {
    if (!keyboard)
        return eKind::Other;

    xkb_state* state = keyboard->m_xkbSymState ? keyboard->m_xkbSymState : keyboard->m_xkbState;
    if (!state)
        return eKind::Other;

    const auto sym = xkb_state_key_get_one_sym(state, evdevKey + 8);
    if (sym == XKB_KEY_Shift_L)
        return eKind::Shift;
    if (sym == XKB_KEY_Super_L)
        return eKind::Super;
    if (sym == XKB_KEY_XF86TouchpadOff || sym == XKB_KEY_F23)
        return eKind::Copilot;
    return eKind::Other;
}

static uint32_t clearMod(SP<IKeyboard> keyboard, uint32_t mask, const char* name) {
    if (!keyboard || !keyboard->m_xkbKeymap)
        return mask;

    const auto idx = xkb_keymap_mod_get_index(keyboard->m_xkbKeymap, name);
    if (idx == XKB_MOD_INVALID)
        return mask;
    return mask & ~(1u << idx);
}

static void disarm(SPending& pending) {
    if (pending.timer)
        pending.timer->updateTimeout(std::nullopt);
}

static void flushOne(IKeyboard* keyboard) {
    if (!keyboard)
        return;

    const auto it = g_pending.find(keyboard);
    if (it == g_pending.end())
        return;

    auto& pending = it->second;
    if (pending.swallow)
        return;

    const auto shift = pending.shiftPress;
    const auto super = pending.superPress;
    pending.shiftPress.reset();
    pending.superPress.reset();
    if (shift)
        pending.shiftSent = true;
    if (super)
        pending.superSent = true;
    disarm(pending);

    const auto locked = pending.keyboard.lock();
    if (!locked || (!shift && !super))
        return;

    if (shift)
        g_pInputManager->deliverKeyboardKey(*shift, locked);
    if (super)
        g_pInputManager->deliverKeyboardKey(*super, locked);
    g_pInputManager->onKeyboardMod(locked);
}

static void arm(SPending& pending) {
    if (!pending.timer) {
        pending.timer = makeShared<CEventLoopTimer>(
            std::nullopt,
            [kb = pending.keyboard](SP<CEventLoopTimer>, void*) {
                if (const auto locked = kb.lock())
                    flushOne(locked.get());
            },
            nullptr);
        if (g_pEventLoopManager)
            g_pEventLoopManager->addTimer(pending.timer);
    }

    pending.timer->updateTimeout(HOLD);
}

bool CopilotChord::hidingMods() {
    for (const auto& [_, pending] : g_pending) {
        if (pending.swallow || pending.shiftPress || pending.superPress)
            return true;
    }
    return false;
}

uint32_t CopilotChord::stripHidden(SP<IKeyboard> keyboard, uint32_t xkbMods) {
    xkbMods = clearMod(keyboard, xkbMods, XKB_MOD_NAME_SHIFT);
    xkbMods = clearMod(keyboard, xkbMods, XKB_MOD_NAME_LOGO);
    return clearMod(keyboard, xkbMods, "Mod4");
}

void CopilotChord::flushPending() {
    std::vector<IKeyboard*> keys;
    keys.reserve(g_pending.size());
    for (const auto& [keyboard, pending] : g_pending) {
        if (!pending.swallow && (pending.shiftPress || pending.superPress))
            keys.push_back(keyboard);
    }
    for (auto* keyboard : keys)
        flushOne(keyboard);
}

bool CopilotChord::intercept(SP<IKeyboard> keyboard, const IKeyboard::SKeyEvent& event, bool passEvent) {
    if (!keyboard)
        return false;

    const auto kind    = kindOf(keyboard, event.keycode);
    const bool pressed = event.state == WL_KEYBOARD_KEY_STATE_PRESSED;
    auto&      pending = g_pending[keyboard.get()];
    pending.keyboard   = keyboard;

    if (kind == eKind::Other) {
        if (pressed)
            flushOne(keyboard.get());
        return false;
    }

    if (!pressed) {
        if (kind == eKind::Shift) {
            pending.shiftDown = false;
            pending.shiftSent = false;
        } else if (kind == eKind::Super) {
            pending.superDown = false;
            pending.superSent = false;
        }

        if (pending.swallow) {
            if (!pending.shiftDown && !pending.superDown)
                pending.swallow = false;
            return true;
        }

        if (kind == eKind::Shift && pending.shiftPress) {
            const auto down = *pending.shiftPress;
            pending.shiftPress.reset();
            if (!pending.superPress)
                disarm(pending);
            g_pInputManager->deliverKeyboardKey(down, keyboard);
            g_pInputManager->deliverKeyboardKey(event, keyboard);
            g_pInputManager->onKeyboardMod(keyboard);
            return true;
        }

        if (kind == eKind::Super && pending.superPress) {
            const auto down = *pending.superPress;
            pending.superPress.reset();
            if (!pending.shiftPress)
                disarm(pending);
            g_pInputManager->deliverKeyboardKey(down, keyboard);
            g_pInputManager->deliverKeyboardKey(event, keyboard);
            g_pInputManager->onKeyboardMod(keyboard);
            return true;
        }

        return kind == eKind::Copilot || !passEvent;
    }

    if (kind == eKind::Shift)
        pending.shiftDown = true;
    else if (kind == eKind::Super)
        pending.superDown = true;

    if (kind == eKind::Copilot) {
        if (pending.shiftDown && pending.superDown) {
            pending.swallow = true;
            pending.shiftPress.reset();
            pending.superPress.reset();
            disarm(pending);
        }
        return true;
    }

    if (pending.swallow)
        return true;

    if (kind == eKind::Shift && pending.shiftSent)
        return false;
    if (kind == eKind::Super && pending.superSent)
        return false;

    if (!passEvent)
        return true;

    const bool wasQueued = pending.shiftPress.has_value() || pending.superPress.has_value();
    if (kind == eKind::Shift && !pending.shiftPress)
        pending.shiftPress = event;
    else if (kind == eKind::Super && !pending.superPress)
        pending.superPress = event;

    if (!wasQueued && (pending.shiftPress || pending.superPress))
        arm(pending);
    return true;
}
