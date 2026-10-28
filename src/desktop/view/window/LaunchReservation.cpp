#include "LaunchReservation.hpp"

#include "ReservationBackend.hpp"

#include "../../../Compositor.hpp"
#include "../../../config/ConfigValue.hpp"
#include "../../../debug/log/Logger.hpp"
#include "../../../helpers/MiscFunctions.hpp"
#include "../../../helpers/memory/Memory.hpp"
#include "../../../macros.hpp"
#include "../../../layout/LayoutManager.hpp"
#include "../../../layout/space/Space.hpp"
#include "../../../layout/target/WindowTarget.hpp"
#include "../../../managers/eventLoop/EventLoopManager.hpp"
#include "../../../managers/fullscreen/FullscreenController.hpp"
#include "../../state/FocusState.hpp"

#include <chrono>

using namespace Desktop::View;

static constexpr auto TICK = std::chrono::milliseconds(500);

CLaunchReservation& Desktop::View::launchReservations() {
    static CLaunchReservation instance;
    return instance;
}

CLaunchReservation::CLaunchReservation() {
    m_timer = makeShared<CEventLoopTimer>(TICK, [this](SP<CEventLoopTimer>, void*) { onTick(); }, this);
    g_pEventLoopManager->addTimer(m_timer);
}

void CLaunchReservation::open(pid_t pid, const std::string& token) {
    if (pid <= 1 || token.empty())
        return;

    // One waiting tile. A newer launch replaces an older one that never mapped.
    std::vector<PHLWINDOW> previous;
    for (const auto& slot : m_slots) {
        if (slot.window)
            previous.push_back(slot.window);
    }
    for (const auto& window : previous)
        dismiss(window, false);

    auto backend = makeUnique<CReservationBackend>(pid, token);
    auto* raw    = backend.get();
    auto window  = CWindow::create(std::move(backend));
    if (!window) {
        LOG(Log::ERR, "launch reservation: failed to create placeholder for pid {}", pid);
        return;
    }

    m_slots.push_back(SSlot{
        .pid    = pid,
        .token  = token,
        .window = window,
        .born   = Time::steadyNow(),
    });

    raw->notifyMapped();

    if (!window->mapped()) {
        LOG(Log::ERR, "launch reservation: placeholder for pid {} did not map", pid);
        dynamic_cast<CReservationBackend&>(window->backend()).finish(false);
        return;
    }

    LOG(Log::DEBUG, "launch reservation: holding a tile for pid {} token {}", pid, token);
}

bool CLaunchReservation::ownsToken(const std::string& token) const {
    for (const auto& slot : m_slots) {
        if (slot.token == token && slot.window && slot.window->mapped())
            return true;
    }
    return false;
}

CLaunchReservation::SSlot* CLaunchReservation::slotForToken(const std::string& token) {
    for (auto& slot : m_slots) {
        if (slot.token == token && slot.window && slot.window->mapped())
            return &slot;
    }
    return nullptr;
}

CLaunchReservation::SSlot* CLaunchReservation::slotForWindow(PHLWINDOW window) {
    for (auto& slot : m_slots) {
        if (slot.window == window)
            return &slot;
    }
    return nullptr;
}

PHLWINDOW CLaunchReservation::match(PHLWINDOW window) const {
    if (!window || window->backend().isReservation())
        return nullptr;

    const auto ENV = window->getEnv();
    if (const auto TOKEN = ENV.find("HYPRLAND_LAUNCH_TOKEN"); TOKEN != ENV.end()) {
        for (const auto& slot : m_slots) {
            if (slot.token == TOKEN->second && slot.window && slot.window->mapped())
                return slot.window;
        }
    }

    pid_t pid = window->backend().pid();
    for (size_t hop = 0; hop < 25 && pid > 1; ++hop) {
        for (const auto& slot : m_slots) {
            if (slot.pid == pid && slot.window && slot.window->mapped())
                return slot.window;
        }
        pid = getPPIDof(pid);
    }

    // app2unit starts the app under systemd, so the window is not a child of
    // the shell we spawned. The one waiting tile belongs to the next real window.
    if (m_slots.size() == 1 && m_slots.front().window && m_slots.front().window->mapped())
        return m_slots.front().window;

    return nullptr;
}

void CLaunchReservation::forget(pid_t pid) {
    std::erase_if(m_slots, [pid](const SSlot& slot) { return slot.pid == pid; });
}

void CLaunchReservation::dismiss(PHLWINDOW window, bool killProcess) {
    if (!window)
        return;

    const auto PID = window->backend().pid();
    forget(PID);

    if (!window->backend().isReservation())
        return;

    dynamic_cast<CReservationBackend&>(window->backend()).finish(killProcess);
}

void CLaunchReservation::retire(PHLWINDOW window) {
    if (!window)
        return;

    forget(window->backend().pid());
    window->dropLaunchReservation();
}

bool CLaunchReservation::claim(PHLWINDOW window) {
    const auto SLOT_WINDOW = match(window);
    if (!SLOT_WINDOW)
        return false;

    if (window->isFloating() || window->m_workspace != SLOT_WINDOW->m_workspace) {
        dismiss(SLOT_WINDOW, false);
        return false;
    }

    if (Fullscreen::controller()->isFullscreen(SLOT_WINDOW))
        Fullscreen::controller()->setFullscreenMode(SLOT_WINDOW, Fullscreen::FSMODE_NONE, Fullscreen::FSMODE_NONE);

    const auto SLOT_TARGET = SLOT_WINDOW->windowTarget();
    const auto INCOMING    = window->windowTarget();
    if (!SLOT_TARGET || !INCOMING || !SLOT_TARGET->space()) {
        dismiss(SLOT_WINDOW, false);
        return false;
    }

    // The placeholder already owns the dwindle/master node. Swap the target in
    // that node so the real window does not open a second split.
    INCOMING->setFloatingInitial(SLOT_TARGET->floating());
    SLOT_TARGET->givePlacementTo(INCOMING);
    SLOT_TARGET->releaseSpace();

    if (const auto SPACE = INCOMING->space()) {
        SPACE->swap(SLOT_TARGET, INCOMING);
        SPACE->recalculate();
    }

    retire(SLOT_WINDOW);
    return true;
}

bool CLaunchReservation::onActivation(const std::string& token, PHLWINDOW window) {
    const auto SLOT = slotForToken(token);
    if (!SLOT || !SLOT->window)
        return false;

    const auto SLOT_WINDOW = SLOT->window;
    if (!window || window->backend().isReservation() || !window->mapped())
        return true;

    // Already tiled somewhere: this is an existing window being focused, or a
    // window that took its own split. Drop the placeholder and leave it there.
    // A window that has not been placed yet is adopted by claim() on map.
    if (const auto TARGET = window->layoutTarget(); TARGET && TARGET->space())
        dismiss(SLOT_WINDOW, false);

    return true;
}

void CLaunchReservation::onTick() {
    static auto PTIMEOUT = CConfigValue<Config::INTEGER>("misc:launch_reservation_timeout");
    const auto  NOW      = Time::steadyNow();
    const auto  LIMIT    = std::chrono::seconds(std::max(1, sc<int>(*PTIMEOUT)));

    std::vector<std::pair<pid_t, bool>> expired;
    for (auto& slot : m_slots) {
        if (!slot.window || !slot.window->mapped()) {
            expired.emplace_back(slot.pid, false);
            continue;
        }

        if (NOW - slot.born > LIMIT)
            expired.emplace_back(slot.pid, true);
    }

    for (const auto& [pid, killProcess] : expired) {
        PHLWINDOW window;
        for (const auto& slot : m_slots) {
            if (slot.pid == pid)
                window = slot.window;
        }

        if (window)
            dismiss(window, killProcess);
        else
            forget(pid);
    }

    m_timer->updateTimeout(TICK);
}
