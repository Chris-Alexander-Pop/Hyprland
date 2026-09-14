#include "LayerPointerHold.hpp"

#include "../../config/ConfigValue.hpp"
#include "../../desktop/state/LayerState.hpp"
#include "../../desktop/view/LayerSurface.hpp"
#include "../../desktop/view/WLSurface.hpp"
#include "../../devices/IKeyboard.hpp"
#include "../../protocols/core/Compositor.hpp"

#include <xkbcommon/xkbcommon.h>

using namespace Desktop::View;

static WP<CWLSurfaceResource> g_below;

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
