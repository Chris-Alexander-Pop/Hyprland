#include "ScreenshareManager.hpp"
#include "../../pointer/PointerManager.hpp"
#include "../input/LayerPointerHold.hpp"
#include "../../protocols/core/Seat.hpp"
#include "../permissions/DynamicPermissionManager.hpp"
#include "../../render/Renderer.hpp"
#include "../../render/pass/ClearPassElement.hpp"
#include "../../render/pass/TexPassElement.hpp"
#include <hyprgraphics/egl/Egl.hpp>

using namespace Hyprgraphics::Egl;
using namespace Screenshare;

CCursorshareSession::CCursorshareSession(wl_client* client, WP<CWLPointerResource> pointer) : m_client(client), m_pointer(pointer) {
    m_listeners.pointerDestroyed = m_pointer->m_events.destroyed.listen([this] { stop(); });
    m_listeners.cursorChanged    = Pointer::mgr()->m_events.cursorChanged.listen([this] {
        if (LayerPointerHold::freeze())
            return;
        calculateConstraints();
        m_events.constraintsChanged.emit();

        if (m_pendingFrame.pending) {
            if (copy())
                return;

            LOG(Log::ERR, "Failed to copy cursor image for cursor share");
            if (m_pendingFrame.callback)
                m_pendingFrame.callback(RESULT_NOT_COPIED);
            m_pendingFrame.pending = false;
            return;
        }
    });

    calculateConstraints();
}

CCursorshareSession::~CCursorshareSession() {
    stop();
}

void CCursorshareSession::stop() {
    if (m_stopped)
        return;
    m_stopped = true;
    m_events.stopped.emit();
}

void CCursorshareSession::calculateConstraints() {
    m_constraintsChanged = true;

    SP<Aquamarine::IBuffer> buf;
    Vector2D                hotspot;
    Vector2D                size;

    if (LayerPointerHold::freeze() && Pointer::mgr()->hasFreezeCursor()) {
        buf     = Pointer::mgr()->freezeCursorBuffer();
        hotspot = Pointer::mgr()->freezeCursorHotspot();
        size    = Pointer::mgr()->freezeCursorSize();
    } else {
        const auto& cursorImage = Pointer::mgr()->currentCursorImage();
        buf                     = cursorImage.pBuffer;
        hotspot                 = cursorImage.hotspot;
        size                    = cursorImage.size;
    }

    if (!buf)
        return;

    if (auto attrs = buf->shm(); attrs.success)
        m_format = attrs.format;
    else
        return;

    m_hotspot    = hotspot;
    m_bufferSize = size;
}

// TODO: allow render to buffer without monitor and remove monitor param
eScreenshareError CCursorshareSession::share(PHLMONITOR monitor, SP<IHLBuffer> buffer, FSourceBoxCallback sourceBoxCallback, FScreenshareCallback callback) {
    if (m_stopped || m_pointer.expired() || m_bufferSize == Vector2D(0, 0))
        return ERROR_STOPPED;

    if UNLIKELY (!buffer || !buffer->m_resource || !buffer->m_resource->good()) {
        LOG(Log::ERR, "Client requested sharing to an invalid buffer");
        return ERROR_NO_BUFFER;
    }

    if UNLIKELY (buffer->size != m_bufferSize) {
        LOG(Log::ERR, "Client requested sharing to an invalid buffer size");
        return ERROR_BUFFER_SIZE;
    }

    uint32_t bufFormat;
    if (buffer->dmabuf().success)
        bufFormat = buffer->dmabuf().format;
    else if (buffer->shm().success)
        bufFormat = buffer->shm().format;
    else {
        LOG(Log::ERR, "Client requested sharing to an invalid buffer");
        return ERROR_NO_BUFFER;
    }

    if (bufFormat != m_format) {
        LOG(Log::ERR, "Invalid format {} in {:x}", bufFormat, (uintptr_t)this);
        return ERROR_BUFFER_FORMAT;
    }

    m_pendingFrame.pending           = true;
    m_pendingFrame.monitor           = monitor;
    m_pendingFrame.buffer            = buffer;
    m_pendingFrame.sourceBoxCallback = sourceBoxCallback;
    m_pendingFrame.callback          = callback;

    // nothing changed, then delay copy until contraints changed
    if (!m_constraintsChanged)
        return ERROR_NONE;

    if (!copy()) {
        LOG(Log::ERR, "Failed to copy cursor image for cursor share");
        callback(RESULT_NOT_COPIED);
        m_pendingFrame.pending = false;
        return ERROR_UNKNOWN;
    }

    return ERROR_NONE;
}

void CCursorshareSession::render() {
    const auto  PERM = g_pDynamicPermissionManager->clientPermissionMode(m_client, PERMISSION_TYPE_CURSOR_POS);

    const auto& cursorImage = Pointer::mgr()->currentCursorImage();
    auto        freezeTex   = LayerPointerHold::freeze() ? Pointer::mgr()->freezeCursorTexture() : nullptr;

    // TODO: implement a monitor independent render mode to buffer that does this in CHyprRenderer::begin() or something like that
    g_pHyprRenderer->m_renderData.transformDamage = false;
    g_pHyprRenderer->setViewport(0, 0, m_bufferSize.x, m_bufferSize.y);

    CBox sourceBox = m_pendingFrame.sourceBoxCallback();
    CBox cursorBox = Pointer::mgr()->getCursorBoxGlobal();
    if (auto pin = LayerPointerHold::freezePin()) {
        const auto hot = Pointer::mgr()->hasFreezeCursor() ? Pointer::mgr()->freezeCursorHotspot() : Pointer::mgr()->hotspot();
        cursorBox.x    = pin->x - hot.x;
        cursorBox.y    = pin->y - hot.y;
        if (Pointer::mgr()->hasFreezeCursor())
            cursorBox.w = Pointer::mgr()->freezeCursorSize().x, cursorBox.h = Pointer::mgr()->freezeCursorSize().y;
    }
    bool overlaps = cursorBox.overlaps(sourceBox);
    g_pHyprRenderer->startRenderPass();
    auto tex = freezeTex ? freezeTex : cursorImage.bufferTex;
    if (PERM != PERMISSION_RULE_ALLOW_MODE_ALLOW || !overlaps) {
        g_pHyprRenderer->draw(CClearPassElement::SClearData{Colors::BLACK});
    } else if (!tex) {
        g_pHyprRenderer->draw(CClearPassElement::SClearData{{0, 0, 0, 0}});
    } else {
        g_pHyprRenderer->draw(CTexPassElement::SRenderData{
            .tex = tex,
            .box = {{}, tex->m_size},
        });
    }

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
}

bool CCursorshareSession::copy() {
    if (!m_pendingFrame.callback || !m_pendingFrame.monitor || !m_pendingFrame.callback || !m_pendingFrame.sourceBoxCallback)
        return false;

    // FIXME: this doesn't really make sense but just to be safe
    m_pendingFrame.callback(RESULT_TIMESTAMP);

    CRegion fakeDamage = {0, 0, INT16_MAX, INT16_MAX};
    if (auto attrs = m_pendingFrame.buffer->dmabuf(); attrs.success) {
        if (attrs.format != m_format) {
            LOG(Log::ERR, "Can't copy: invalid format");
            return false;
        }

        if (!g_pHyprRenderer->beginRenderToBuffer(m_pendingFrame.monitor, fakeDamage, m_pendingFrame.buffer, true)) {
            LOG(Log::ERR, "Can't copy: failed to begin rendering to dmabuf");
            return false;
        }

        render();

        g_pHyprRenderer->endRender([callback = m_pendingFrame.callback]() {
            if (callback)
                callback(RESULT_COPIED);
        });
    } else if (auto attrs = m_pendingFrame.buffer->shm(); attrs.success) {
        const auto PFORMAT = getPixelFormatFromDRM(m_format);

        if (attrs.format != m_format || !PFORMAT) {
            LOG(Log::ERR, "Can't copy: invalid format");
            return false;
        }

        if (!m_copyFB)
            m_copyFB = g_pHyprRenderer->createFB("cursorshare shm");
        auto outFB = m_copyFB;
        outFB->alloc(m_bufferSize.x, m_bufferSize.y, m_format);

        if (!g_pHyprRenderer->beginFullFakeRender(m_pendingFrame.monitor, fakeDamage, outFB)) {
            LOG(Log::ERR, "Can't copy: failed to begin rendering to shm");
            return false;
        }

        render();

        g_pHyprRenderer->endRender();

        int glFormat = PFORMAT->glFormat;

        if (glFormat == GL_RGBA)
            glFormat = GL_BGRA_EXT;

        if (glFormat != GL_BGRA_EXT && glFormat != GL_RGB) {
            if (PFORMAT->swizzle.has_value()) {
                if (PFORMAT->swizzle == SWIZZLE_RGBA)
                    glFormat = GL_RGBA;
                else if (PFORMAT->swizzle == SWIZZLE_BGRA)
                    glFormat = GL_BGRA_EXT;
                else {
                    LOG(Log::ERR, "Copied frame via shm might be broken or color flipped");
                    glFormat = GL_RGBA;
                }
            }
        }

        if (!outFB->readPixels(m_pendingFrame.buffer, 0, 0, m_bufferSize.x, m_bufferSize.y)) {
            g_pHyprRenderer->m_renderData.pMonitor.reset();
            LOG(Log::ERR, "Can't copy: failed to read cursor pixels to shm");
            return false;
        }

        g_pHyprRenderer->m_renderData.pMonitor.reset();

        m_pendingFrame.callback(RESULT_COPIED);
    } else {
        LOG(Log::ERR, "Can't copy: invalid buffer type");
        return false;
    }

    m_pendingFrame.pending = false;
    m_constraintsChanged   = false;
    return true;
}

DRMFormat CCursorshareSession::format() const {
    return m_format;
}

Vector2D CCursorshareSession::bufferSize() const {
    return m_bufferSize;
}

Vector2D CCursorshareSession::hotspot() const {
    return m_hotspot;
}
