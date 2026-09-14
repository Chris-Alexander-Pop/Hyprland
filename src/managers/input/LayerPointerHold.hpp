#pragma once

#include "../../desktop/DesktopTypes.hpp"
#include "../../helpers/math/Math.hpp"

#include <optional>
#include <string>

class CWLSurfaceResource;
class IKeyboard;

// Keep pointer enter on the last client while the cursor is over a named layer.
namespace LayerPointerHold {
    bool                    enabled();
    std::string             targetNamespace();
    bool                    layerMapped();
    bool                    isHeldLayer(PHLLS layer);
    bool                    isHeldSurface(SP<CWLSurfaceResource> surf);

    bool                    isLogoKey(SP<IKeyboard> keyboard, uint32_t evdevKeycode);
    uint32_t                stripLogoMods(SP<IKeyboard> keyboard, uint32_t xkbMods);

    void                    rememberBelow(SP<CWLSurfaceResource> surf);
    SP<CWLSurfaceResource>  below();
    void                    clearBelow();

    std::optional<Vector2D> belowLocal(const Vector2D& global);
}
