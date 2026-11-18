#pragma once

#include "WindowBackend.hpp"

#include <string>

namespace Desktop::View {
    class CReservationBackend final : public IWindowBackend {
      public:
        CReservationBackend(pid_t pid, std::string token);
        ~CReservationBackend() override = default;

        void notifyMapped();
        void finish(bool killProcess);

        bool                   valid() const override;
        bool                   isMapped() const override;
        eBackendType           type() const override;
        bool                   isReservation() const override;
        pid_t                  pid() const override;
        SP<CWLSurfaceResource> surface() const override;
        PHLWINDOW              parent() const override;
        SBackendClientID       clientID() const override;

        bool                   initialCommit() const override;
        SClientGeometry        geometry() const override;
        SGeometryHints         geometryHints(eBackendState state) const override;
        SBackendMetadata       metadata() const override;
        SBackendTraits         traits() const override;
        double                 surfaceScale() const override;
        Vector2D               reportedSize() const override;

        CBox                   clientToLogical(const CBox& box, PHLMONITOR preferredMonitor) const override;
        CBox                   logicalToClient(const CBox& box, PHLMONITOR preferredMonitor) const override;
        Vector2D               surfaceLocalToBuffer(const Vector2D& local) const override;
        Vector2D               bufferToSurfaceLocal(const Vector2D& buffer) const override;
        Vector2D               globalToBuffer(const Vector2D& pos, const Vector2D& origin) const override;

        void                   configure(const CBox& logicalBox, PHLMONITOR preferredMonitor, bool force = false) override;
        void                   acknowledgeConfigure(const CBox& clientBox) override;
        void                   setActive(bool active) override;
        void                   setFullscreen(bool fullscreen) override;
        void                   setMaximized(bool maximized) override;
        void                   setResizing(bool resizing) override;
        bool                   setSuspended(bool suspended) override;
        void                   setMinimized(bool minimized) override;
        void                   restackToTop() override;
        void                   close() override;
        void                   ping() override;

      private:
        void attach(PHLWINDOWREF window) override;

        pid_t            m_pid = 0;
        std::string      m_token;
        SBackendClientID m_clientID;
        PHLWINDOWREF     m_window;
        Vector2D         m_reportedSize = {800, 600};
        bool             m_mapped       = false;
        bool             m_closed       = false;
    };
}
