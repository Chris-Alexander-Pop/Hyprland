#include "LayerPointerHold.hpp"

#include "../../config/ConfigValue.hpp"
#include "../../desktop/state/LayerState.hpp"
#include "../../desktop/state/ViewState.hpp"
#include "../../desktop/view/LayerSurface.hpp"
#include "../../desktop/view/WLSurface.hpp"
#include "../../desktop/view/window/Window.hpp"
#include "../../devices/IKeyboard.hpp"
#include "../../managers/SeatManager.hpp"
#include "../../output/Monitor.hpp"
#include "../../pointer/PointerController.hpp"
#include "../../pointer/PointerManager.hpp"
#include "../../pointer/cursor/CursorManager.hpp"
#include "../../protocols/core/Compositor.hpp"
#include "../../protocols/LayerShell.hpp"
#include "../../render/Renderer.hpp"
#include "../../state/MonitorState.hpp"
#include "InputManager.hpp"

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <ctime>
#include <cstdio>
#include <format>

using namespace Desktop::View;

static WP<CWLSurfaceResource> g_below;
static Vector2D               g_noted{};
static bool                   g_hasNoted   = false;
static bool                   g_simulating = false;
static Vector2D               g_freezePin{};
static bool                   g_haveFreezePin = false;
static bool                   g_freezeWas     = false;

static bool                   surfaceInTree(SP<CWLSurfaceResource> root, SP<CWLSurfaceResource> surface) {
    if (!root || !surface)
        return false;
    if (root == surface)
        return true;
    return !!root->findFirstPreorder([surface](SP<CWLSurfaceResource> candidate) { return candidate == surface; });
}

static bool nsEmpty(const std::string& ns) {
    return ns.empty() || ns == "[[EMPTY]]";
}

std::string LayerPointerHold::targetNamespace() {
    static auto PNS = CConfigValue<std::string>("misc:layer_hold_pointer_namespace");
    return *PNS;
}

bool LayerPointerHold::enabled() {
    static auto PHOLD = CConfigValue<Hyprlang::INT>("misc:layer_hold_pointer");
    return *PHOLD && !nsEmpty(targetNamespace());
}

bool LayerPointerHold::freeze() {
    static auto PFREEZE = CConfigValue<Hyprlang::INT>("misc:layer_hold_freeze");
    return enabled() && *PFREEZE;
}

void LayerPointerHold::syncFreezePin() {
    const bool now = freeze();
    if (now == g_freezeWas)
        return;

    g_freezeWas = now;
    if (now) {
        if (!g_pInputManager)
            return;
        g_freezePin     = g_pInputManager->getMouseCoordsInternal();
        g_haveFreezePin = true;
        Vector2D local;
        if (auto surf = surfaceBelowAt(g_freezePin, local))
            rememberBelow(surf);
        Pointer::mgr()->beginFreezeCursor();
        if (g_pHyprRenderer)
            g_pHyprRenderer->damageBox(CBox{g_freezePin - Vector2D{32, 32}, Vector2D{96, 96}});
        debugLog(std::format("pin-on {:.0f},{:.0f} mapped={}", g_freezePin.x, g_freezePin.y, layerMapped()));
        return;
    }

    debugLog(std::format("pin-off warp={:.0f},{:.0f} have={}", g_freezePin.x, g_freezePin.y, g_haveFreezePin));
    if (!g_haveFreezePin)
        return;

    if (g_pSeatManager)
        g_pSeatManager->resetHoldOverlay();
    const auto savedName = Pointer::mgr() ? Pointer::mgr()->freezeCursorName() : std::string{};
    if (g_pInputManager)
        g_pInputManager->setAppCursorName(savedName);
    if (Pointer::mgr())
        Pointer::mgr()->applyFreezeCursor();
    Pointer::pointerController()->warpTo(g_freezePin, true);
    Vector2D local;
    if (auto surf = LayerPointerHold::surfaceBelowAt(g_freezePin, local)) {
        if (g_pSeatManager)
            g_pSeatManager->enterHoldBelow(surf, local);
    }
    LayerPointerHold::clearNoted();
    if (g_pInputManager)
        g_pInputManager->simulateMouseMovement();
    Pointer::mgr()->endFreezeCursor();
    g_haveFreezePin = false;
}

std::optional<Vector2D> LayerPointerHold::freezePin() {
    if (!freeze())
        return std::nullopt;
    if (g_haveFreezePin)
        return g_freezePin;
    if (g_pInputManager)
        return g_pInputManager->getMouseCoordsInternal();
    return std::nullopt;
}

bool LayerPointerHold::isHeldLayer(PHLLS layer) {
    if (!layer || !layer->mapped())
        return false;
    return layer->m_namespace == targetNamespace();
}

bool LayerPointerHold::layerMapped() {
    if (!enabled())
        return false;

    for (const auto& layer : Desktop::layerState()->layers()) {
        if (isHeldLayer(layer))
            return true;
    }
    return false;
}

bool LayerPointerHold::isHeldSurface(SP<CWLSurfaceResource> surf) {
    if (!enabled() || !surf)
        return false;

    for (const auto& layer : Desktop::layerState()->layers()) {
        if (!isHeldLayer(layer))
            continue;
        if (surfaceInTree(layer->resource(), surf))
            return true;
    }
    return false;
}

bool LayerPointerHold::isHeldClient(wl_client* client) {
    if (!enabled() || !client)
        return false;

    for (const auto& layer : Desktop::layerState()->layers()) {
        if (!isHeldLayer(layer))
            continue;
        const auto res = layer->resource();
        if (res && res->client() == client)
            return true;
    }
    return false;
}

bool LayerPointerHold::isBelowSurface(SP<CWLSurfaceResource> surf) {
    auto below = g_below.lock();
    return below && surf && surfaceInTree(below, surf);
}

bool LayerPointerHold::freezeBlocksAt(const Vector2D& global) {
    if (!freeze())
        return false;
    Vector2D local;
    if (overlayAt(global, local))
        return false;
    return isBelowSurface(surfaceBelowAt(global, local));
}

void LayerPointerHold::debugLog(const std::string& msg) {
    const char* dir  = getenv("XDG_RUNTIME_DIR");
    const auto  path = std::string{dir && dir[0] ? dir : "/tmp"} + "/screen-shadow-hypr.log";
    FILE*       f    = fopen(path.c_str(), "a");
    if (!f)
        return;
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    const long long ms = sc<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    fprintf(f, "%lld hypr %s\n", ms, msg.c_str());
    fclose(f);
}

SP<CWLSurfaceResource> LayerPointerHold::overlayAt(const Vector2D& global, Vector2D& local) {
    if (!enabled())
        return nullptr;

    const auto PMON = State::monitorState()->query().vec(global).run();
    if (!PMON)
        return nullptr;

    static const uint32_t kLayers[] = {ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP};
    for (const auto layerId : kLayers) {
        PHLLS ls;
        auto  surf = Desktop::viewState()->hitTest().layerSurfaceAt(global, &PMON->m_layerSurfaceLayers[layerId], &local, &ls);
        if (surf && isHeldLayer(ls) && surf->getResource() && surf->getResource()->resource())
            return surf;
    }
    return nullptr;
}

bool LayerPointerHold::isLogoKey(SP<IKeyboard> keyboard, uint32_t evdevKeycode) {
    if (!keyboard || !keyboard->m_xkbState)
        return false;
    const auto sym = xkb_state_key_get_one_sym(keyboard->m_xkbState, evdevKeycode + 8);
    return sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
}

uint32_t LayerPointerHold::stripLogoMods(SP<IKeyboard> keyboard, uint32_t xkbMods) {
    if (!keyboard || !keyboard->m_xkbKeymap)
        return xkbMods;

    auto idx = xkb_keymap_mod_get_index(keyboard->m_xkbKeymap, XKB_MOD_NAME_LOGO);
    if (idx == XKB_MOD_INVALID)
        idx = xkb_keymap_mod_get_index(keyboard->m_xkbKeymap, "Mod4");
    if (idx == XKB_MOD_INVALID)
        return xkbMods;
    return xkbMods & ~(1u << idx);
}

void LayerPointerHold::rememberBelow(SP<CWLSurfaceResource> surf) {
    if (!surf || isHeldSurface(surf))
        return;
    g_below = surf;
}

SP<CWLSurfaceResource> LayerPointerHold::below() {
    return g_below.lock();
}

void LayerPointerHold::clearBelow() {
    g_below.reset();
}

SP<CWLSurfaceResource> LayerPointerHold::surfaceBelowAt(const Vector2D& global, Vector2D& local) {
    const uint16_t props = Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING | Desktop::View::FOLLOW_MOUSE_CHECK;
    auto           win   = Desktop::viewState()->hitTest().windowAt(global, props);
    if (!win || !win->wlSurface())
        return nullptr;
    if (win->backend().isX11()) {
        local = global - win->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        return win->wlSurface()->resource();
    }
    return Desktop::viewState()->hitTest().windowSurfaceAt(global, win, local);
}

void LayerPointerHold::beginSimulated() {
    g_simulating = true;
}

void LayerPointerHold::endSimulated() {
    g_simulating = false;
}

bool LayerPointerHold::simulated() {
    return g_simulating;
}

void LayerPointerHold::notePos(const Vector2D& global) {
    g_noted    = global;
    g_hasNoted = true;
}

void LayerPointerHold::clearNoted() {
    g_hasNoted = false;
}

bool LayerPointerHold::noted(const Vector2D& global) {
    if (!g_hasNoted)
        return false;
    return std::abs(global.x - g_noted.x) < 2.0 && std::abs(global.y - g_noted.y) < 2.0;
}

std::optional<Vector2D> LayerPointerHold::belowLocal(const Vector2D& global) {
    const auto surf = below();
    if (!surf)
        return std::nullopt;

    const auto hl = CWLSurface::fromResource(surf);
    if (!hl)
        return std::nullopt;

    const auto box = hl->getSurfaceBoxGlobal();
    if (!box.has_value() || !box->containsPoint(global))
        return std::nullopt;

    return global - box->pos();
}
