#pragma once

#include "../../desktop/DesktopTypes.hpp"
#include "../../helpers/math/Math.hpp"

#include <optional>
#include <string>

class CWLSurfaceResource;
class IKeyboard;
struct wl_client;

namespace LayerPointerHold {
    bool                    enabled();
    bool                    freeze();
    void                    syncFreezePin();
    std::optional<Vector2D> freezePin();
    std::string             targetNamespace();
    bool                    layerMapped();
    bool                    isHeldLayer(PHLLS layer);
    bool                    isHeldSurface(SP<CWLSurfaceResource> surf);
    bool                    isHeldClient(wl_client* client);
    bool                    isBelowSurface(SP<CWLSurfaceResource> surf);
    bool                    freezeBlocksAt(const Vector2D& global);
    void                    debugLog(const std::string& msg);
    SP<CWLSurfaceResource>  overlayAt(const Vector2D& global, Vector2D& local);

    bool                    isLogoKey(SP<IKeyboard> keyboard, uint32_t evdevKeycode);
    uint32_t                stripLogoMods(SP<IKeyboard> keyboard, uint32_t xkbMods);

    void                    rememberBelow(SP<CWLSurfaceResource> surf);
    SP<CWLSurfaceResource>  below();
    void                    clearBelow();
    SP<CWLSurfaceResource>  surfaceBelowAt(const Vector2D& global, Vector2D& local);

    std::optional<Vector2D> belowLocal(const Vector2D& global);

    void                    clearNoted();

    void                    beginSimulated();
    void                    endSimulated();
    bool                    simulated();

    void                    notePos(const Vector2D& global);
    bool                    noted(const Vector2D& global);
}
