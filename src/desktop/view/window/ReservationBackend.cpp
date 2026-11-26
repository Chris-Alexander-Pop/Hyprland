#include "ReservationBackend.hpp"

#include <csignal>

#include "Window.hpp"

using namespace Desktop::View;

CReservationBackend::CReservationBackend(pid_t pid, std::string token) : m_pid(pid), m_token(std::move(token)) {
    static uint64_t nextID = 1;
    m_clientID             = {.type = eBackendType::WINDOW_BACKEND_WAYLAND, .id = nextID++};
}

void CReservationBackend::notifyMapped() {
    m_events.map.emit();
    m_mapped = m_window && m_window->mapped();
}

void CReservationBackend::finish(bool killProcess) {
    if (m_closed)
        return;

    m_closed = true;

    if (killProcess && m_pid > 1)
        ::kill(-m_pid, SIGTERM);

    const bool MAPPED = m_window && m_window->mapped();
    m_mapped          = false;

    if (MAPPED)
        m_events.unmap.emit();

    m_events.destroy.emit();
}

bool CReservationBackend::valid() const {
    return !m_closed;
}

bool CReservationBackend::isMapped() const {
    return m_mapped;
}

eBackendType CReservationBackend::type() const {
    return eBackendType::WINDOW_BACKEND_WAYLAND;
}

bool CReservationBackend::isReservation() const {
    return true;
}

pid_t CReservationBackend::pid() const {
    return m_pid;
}

SP<CWLSurfaceResource> CReservationBackend::surface() const {
    return nullptr;
}

PHLWINDOW CReservationBackend::parent() const {
    return nullptr;
}

SBackendClientID CReservationBackend::clientID() const {
    return m_clientID;
}

bool CReservationBackend::initialCommit() const {
    return true;
}

SClientGeometry CReservationBackend::geometry() const {
    return {.box = {{}, m_reportedSize}, .positionAuthoritative = false};
}

SGeometryHints CReservationBackend::geometryHints(eBackendState) const {
    return {};
}

SBackendMetadata CReservationBackend::metadata() const {
    return {.title = "Opening", .appID = "hyprland-launch"};
}

SBackendTraits CReservationBackend::traits() const {
    return {};
}

double CReservationBackend::surfaceScale() const {
    return 1.0;
}

Vector2D CReservationBackend::reportedSize() const {
    return m_reportedSize;
}

CBox CReservationBackend::clientToLogical(const CBox& box, PHLMONITOR) const {
    return box;
}

CBox CReservationBackend::logicalToClient(const CBox& box, PHLMONITOR) const {
    return box;
}

Vector2D CReservationBackend::surfaceLocalToBuffer(const Vector2D& local) const {
    return local;
}

Vector2D CReservationBackend::bufferToSurfaceLocal(const Vector2D& buffer) const {
    return buffer;
}

Vector2D CReservationBackend::globalToBuffer(const Vector2D& pos, const Vector2D& origin) const {
    return pos - origin;
}

void CReservationBackend::configure(const CBox& logicalBox, PHLMONITOR, bool) {
    if (logicalBox.w > 2 && logicalBox.h > 2)
        m_reportedSize = logicalBox.size();
}

void CReservationBackend::acknowledgeConfigure(const CBox&) {
    ;
}

void CReservationBackend::setActive(bool) {
    ;
}

void CReservationBackend::setFullscreen(bool) {
    ;
}

void CReservationBackend::setMaximized(bool) {
    ;
}

void CReservationBackend::setResizing(bool) {
    ;
}

bool CReservationBackend::setSuspended(bool) {
    return true;
}

void CReservationBackend::setMinimized(bool) {
    ;
}

void CReservationBackend::restackToTop() {
    ;
}

void CReservationBackend::close() {
    finish(true);
}

void CReservationBackend::ping() {
    m_events.pong.emit();
}

void CReservationBackend::attach(PHLWINDOWREF window) {
    m_window = window;
}
