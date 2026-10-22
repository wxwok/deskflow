/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2022, 2026 Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/PortalInputCapture.h"
#include "base/DirectionTypes.h"
#include "base/Event.h"
#include "base/Log.h"
#include "base/TMethodJob.h"
#include "common/Settings.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/MonitorGeometry.h"
#include "platform/EiClipboard.h"

#ifdef HAVE_LIBPORTAL_CLIPBOARD
#include "platform/PortalClipboard.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <poll.h>
#include <sys/socket.h> // for EIS fd hack, remove
#include <sys/un.h>     // for EIS fd hack, remove

#include <QBuffer>
#include <QByteArray>
#include <QByteArrayList>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QtEndian>

namespace deskflow {

const char *PortalInputCapture::barrierSideName(BarrierSide side)
{
  using enum BarrierSide;

  switch (side) {
  case Left:
    return "left";
  case Right:
    return "right";
  case Top:
    return "top";
  case Bottom:
    return "bottom";
  }

  return "unknown";
}

int PortalInputCapture::scaleCoordinateBetweenRanges(
    double value, int sourceMin, int sourceMax, int destinationMin, int destinationMax
)
{
  if (sourceMax <= sourceMin || destinationMax <= destinationMin) {
    return destinationMin;
  }

  const auto clamped = std::clamp(value, static_cast<double>(sourceMin), static_cast<double>(sourceMax));
  const auto fraction = (clamped - sourceMin) / (sourceMax - sourceMin);
  const auto mapped = static_cast<int>(std::lround(destinationMin + fraction * (destinationMax - destinationMin)));
  return std::clamp(mapped, destinationMin, destinationMax);
}

bool PortalInputCapture::getPortalBounds(Bounds &bounds) const
{
  if (m_hasZoneUnion) {
    bounds = m_zoneUnion;
    return bounds.right > bounds.left && bounds.bottom > bounds.top;
  }

  if (m_barrierInfo.empty()) {
    return false;
  }

  bounds.left = std::numeric_limits<gint>::max();
  bounds.top = std::numeric_limits<gint>::max();
  bounds.right = std::numeric_limits<gint>::min();
  bounds.bottom = std::numeric_limits<gint>::min();

  for (const auto &info : m_barrierInfo) {
    bounds.left = std::min(bounds.left, info.x);
    bounds.top = std::min(bounds.top, info.y);
    bounds.right = std::max(bounds.right, info.x + static_cast<gint>(info.width) - 1);
    bounds.bottom = std::max(bounds.bottom, info.y + static_cast<gint>(info.height) - 1);
  }

  return true;
}

bool PortalInputCapture::getClosestReleaseBarrier(
    double x, double y, int computerLeft, int computerTop, int computerRight, int computerBottom,
    const Bounds &portalBounds, BarrierInfo &barrier
) const
{
  const auto activeSides = m_computer->activeSides();
  using enum DirectionMask;

  auto side = BarrierSide::Left;
  auto sideDistance = std::numeric_limits<double>::max();
  const auto considerSide = [&side, &sideDistance](BarrierSide candidateSide, double candidateDistance) {
    if (candidateDistance < sideDistance) {
      side = candidateSide;
      sideDistance = candidateDistance;
    }
  };

  if (activeSides & static_cast<int>(LeftMask)) {
    considerSide(BarrierSide::Left, std::abs(x - computerLeft));
  }
  if (activeSides & static_cast<int>(RightMask)) {
    considerSide(BarrierSide::Right, std::abs(x - computerRight));
  }
  if (activeSides & static_cast<int>(TopMask)) {
    considerSide(BarrierSide::Top, std::abs(y - computerTop));
  }
  if (activeSides & static_cast<int>(BottomMask)) {
    considerSide(BarrierSide::Bottom, std::abs(y - computerBottom));
  }
  if (sideDistance == std::numeric_limits<double>::max()) {
    return false;
  }

  const auto portalX =
      scaleCoordinateBetweenRanges(x, computerLeft, computerRight, portalBounds.left, portalBounds.right);
  const auto portalY =
      scaleCoordinateBetweenRanges(y, computerTop, computerBottom, portalBounds.top, portalBounds.bottom);

  auto bestDistance = std::numeric_limits<int>::max();
  for (const auto &info : m_barrierInfo) {
    if (info.side != side) {
      continue;
    }

    const auto left = info.x;
    const auto top = info.y;
    const auto right = info.x + static_cast<gint>(info.width) - 1;
    const auto bottom = info.y + static_cast<gint>(info.height) - 1;
    auto distance = 0;

    switch (side) {
    case BarrierSide::Left:
    case BarrierSide::Right:
      if (portalY < top) {
        distance = top - portalY;
      } else if (portalY > bottom) {
        distance = portalY - bottom;
      }
      break;

    case BarrierSide::Top:
    case BarrierSide::Bottom:
      if (portalX < left) {
        distance = left - portalX;
      } else if (portalX > right) {
        distance = portalX - right;
      }
      break;
    }

    if (distance < bestDistance) {
      bestDistance = distance;
      barrier = info;
    }
  }

  return bestDistance != std::numeric_limits<int>::max();
}

PortalInputCapture::PortalInputCapture(EiComputer *computer, IEventQueue *events)
    : m_computer{computer},
      m_events{events},
      m_portalVersion(0),
      m_portal{xdp_portal_new()}
{
  // Create clipboard for primary clipboard ID
  m_clipboard = new EiClipboard(kClipboardClipboard);

  m_glibMainLoop = g_main_loop_new(nullptr, true);

  auto tMethodJob = new TMethodJob<PortalInputCapture>(this, &PortalInputCapture::glibThread);
  m_glibThread = new Thread(tMethodJob);

  auto captureCallback = [](gpointer data) { return static_cast<PortalInputCapture *>(data)->initSession(); };

  g_idle_add(captureCallback, this);
}

void PortalInputCapture::setSwitchEdges(std::vector<deskflow::EdgeSpan> edges)
{
  bool schedule = false;
  {
    std::lock_guard lock(m_edgeMutex);
    m_switchEdges = std::move(edges);
    m_monitorEdges = !m_switchEdges.empty();
    if (m_glibMainLoop != nullptr && m_portalAlive.load() && !m_rebuildQueued) {
      m_rebuildQueued = true;
      schedule = true;
    }
  }
  if (!schedule) {
    return;
  }

  const auto idle = g_idle_add(
      [](gpointer data) -> gboolean {
        auto *self = static_cast<PortalInputCapture *>(data);
        {
          std::lock_guard lock(self->m_edgeMutex);
          self->m_rebuildQueued = false;
          self->m_rebuildIdle = 0;
        }
        if (self->m_portalAlive.load() && self->m_session != nullptr) {
          self->handleZonesChanged(self->m_session, nullptr);
        }
        return G_SOURCE_REMOVE;
      },
      this
  );

  std::lock_guard lock(m_edgeMutex);
  m_rebuildIdle = idle;
}

std::vector<deskflow::EdgeSpan> PortalInputCapture::switchEdges() const
{
  std::lock_guard lock(m_edgeMutex);
  return m_switchEdges;
}

PortalInputCapture::~PortalInputCapture()
{
  m_portalAlive.store(false);
  guint idle = 0;
  guint releaseIdle = 0;
  guint barrierRetry = 0;
  guint reenableTimeout = 0;
  std::shared_ptr<CaptureRelease> releaseJob = takeReleaseJob(releaseIdle);
  {
    std::lock_guard lock(m_edgeMutex);
    idle = m_rebuildIdle;
    m_rebuildIdle = 0;
    barrierRetry = m_barrierRetry;
    m_barrierRetry = 0;
    reenableTimeout = m_reenableTimeout;
    m_reenableTimeout = 0;
  }
  if (idle != 0) {
    g_source_remove(idle);
  }
  if (releaseIdle != 0) {
    g_source_remove(releaseIdle);
  }
  releaseJob.reset();
  if (barrierRetry != 0) {
    g_source_remove(barrierRetry);
  }
  if (reenableTimeout != 0) {
    g_source_remove(reenableTimeout);
  }

  if (g_main_loop_is_running(m_glibMainLoop))
    g_main_loop_quit(m_glibMainLoop);

  if (m_glibThread) {
    m_glibThread->cancel();
    m_glibThread->wait();
    m_glibThread = nullptr;

    g_main_loop_unref(m_glibMainLoop);
    m_glibMainLoop = nullptr;
  }

  if (m_session) {
    using enum Signal;
    XdpSession *parentSession = xdp_input_capture_session_get_session(m_session);
    if (m_signals.at(SessionClosed) != 0)
      g_signal_handler_disconnect(parentSession, m_signals.at(SessionClosed));
    g_signal_handler_disconnect(m_session, m_signals.at(Disabled));
    g_signal_handler_disconnect(m_session, m_signals.at(Activated));
    g_signal_handler_disconnect(m_session, m_signals.at(Deactivated));
    g_signal_handler_disconnect(m_session, m_signals.at(ZonesChanged));
#ifdef HAVE_LIBPORTAL_CLIPBOARD
    g_signal_handler_disconnect(parentSession, m_signals.at(SelectionTransfer));
#endif
    g_object_unref(m_session);
  }

  for (auto b : m_barriers) {
    g_object_unref(b);
  }
  m_barriers.clear();
  g_object_unref(m_portal);

  delete m_clipboard;
}

EiClipboard *PortalInputCapture::getClipboard(ClipboardID id) const
{
  // Currently only supporting primary clipboard
  if (id == kClipboardClipboard) {
    return m_clipboard;
  }
  return nullptr;
}

gboolean PortalInputCapture::timeoutHandler() const
{
  return true; // keep re-triggering
}

void PortalInputCapture::handleSessionClosed(XdpSession *session)
{
  LOG_ERR("portal input capture session was closed, exiting");
  g_main_loop_quit(m_glibMainLoop);
  m_events->addEvent(Event(EventTypes::Quit));

  g_signal_handler_disconnect(session, m_signals.at(Signal::SessionClosed));
  m_signals.at(Signal::SessionClosed) = 0;
}

void PortalInputCapture::claimClipboardOwnership([[maybe_unused]] XdpSession *session) const
{
#ifdef HAVE_LIBPORTAL_CLIPBOARD
  PortalClipboard::claimOwnership(m_clipboard, session);
#endif
}

void PortalInputCapture::readClipboardSelection(XdpSession *session) const
{
#ifdef HAVE_LIBPORTAL_CLIPBOARD
  const qint64 maxBytes = static_cast<qint64>(m_computer->maximumClipboardSize()) * 1024;
  LOG_DEBUG("clipboard read cap: %lld bytes", static_cast<long long>(maxBytes));

  const char **mimeTypes = xdp_session_get_selection_mime_types(session);
  if (!mimeTypes) {
    LOG_DEBUG("clipboard has no mime types available to read");
    return;
  }

  if (PortalClipboard::readSelectionIntoCache(m_clipboard, session, mimeTypes, maxBytes))
    m_computer->sendClipboardEvent(EventTypes::ClipboardGrabbed, kClipboardClipboard);
#else
  (void)session;
#endif
}

void PortalInputCapture::handleSelectionTransfer(XdpSession *session, const char *mimeType, uint32_t serial) const
{
#ifdef HAVE_LIBPORTAL_CLIPBOARD
  if (m_recovery.captured()) {
    LOG_DEBUG("skipping clipboard selection transfer, clipboard is active");
    return;
  }
  PortalClipboard::serveSelectionTransfer(m_clipboard, session, mimeType, serial);
#else
  (void)session;
  (void)mimeType;
  (void)serial;
#endif
}

void PortalInputCapture::setupSession(XdpInputCaptureSession *session)
{
  g_autoptr(GError) error = nullptr;
  XdpSession *parentSession = xdp_input_capture_session_get_session(session);

#ifdef HAVE_LIBPORTAL_CLIPBOARD
  if (!xdp_session_is_clipboard_enabled(parentSession) && m_portalVersion > 1) {
    if (Settings::value(Settings::Server::XdpClipboardRetried).toBool()) {
      // some backends never report clipboard enabled even when granted; don't loop forever
      LOG_DEBUG("clipboard still not enabled on session after one retry, continuing without it");
    } else {
      LOG_WARN("clipboard not enabled on session, discarding restore token to force a fresh session");
#ifdef HAVE_LIBPORTAL_INPUTCAPTURE_RESTORE
      Settings::setValue(Settings::Server::XdpRestoreToken, QString());
#endif
      Settings::setValue(Settings::Server::XdpClipboardRetried, true);
      g_object_unref(m_session);
      m_session = nullptr;
      g_idle_add([](gpointer data) { return static_cast<PortalInputCapture *>(data)->initSession(); }, this);
      return;
    }
  }
#endif

  auto fd = xdp_input_capture_session_connect_to_eis(session, &error);
  if (fd < 0) {
    LOG_ERR("failed to connect to eis: %s", error->message);
    g_main_loop_quit(m_glibMainLoop);
    m_events->addEvent(Event(EventTypes::Quit));
    return;
  }

  // Socket ownership is transferred to the EiComputer
  m_events->addEvent(Event(EventTypes::EIConnected, m_computer->getEventTarget(), EiComputer::EiConnectInfo::alloc(fd))
  );

  using enum Signal;
  m_signals.at(Disabled) = g_signal_connect(G_OBJECT(session), "disabled", G_CALLBACK(disabled), this);
  m_signals.at(Activated) = g_signal_connect(G_OBJECT(session), "activated", G_CALLBACK(activated), this);
  m_signals.at(Deactivated) = g_signal_connect(G_OBJECT(session), "deactivated", G_CALLBACK(deactivated), this);
  m_signals.at(ZonesChanged) = g_signal_connect(G_OBJECT(session), "zones-changed", G_CALLBACK(zonesChanged), this);
  m_signals.at(SessionClosed) = g_signal_connect(G_OBJECT(parentSession), "closed", G_CALLBACK(sessionClosed), this);
  handleZonesChanged(session, nullptr);

#ifdef HAVE_LIBPORTAL_CLIPBOARD
  m_signals.at(SelectionTransfer) =
      g_signal_connect(G_OBJECT(parentSession), "selection-transfer", G_CALLBACK(selectionTransfer), this);
#endif
}

void PortalInputCapture::handleInitSession(GObject *object, GAsyncResult *res)
{
  g_autoptr(GError) error = nullptr;

  LOG_DEBUG("portal input capture session initialized");

  auto session = xdp_portal_create_input_capture_session_finish(XDP_PORTAL(object), res, &error);
  if (!session) {
    LOG_ERR("failed to initialize input capture session, quitting: %s", error->message);
    g_main_loop_quit(m_glibMainLoop);
    m_events->addEvent(Event(EventTypes::Quit));
    return;
  }

  m_session = session;

  setupSession(session);
}

void PortalInputCapture::handleStart(GObject *object, GAsyncResult *res)
{
  g_autoptr(GError) error = nullptr;

#ifdef HAVE_LIBPORTAL_INPUTCAPTURE_RESTORE
  LOG_DEBUG("portal input capture session initialized");
  if (!xdp_input_capture_session_start_finish(m_session, res, &error)) {
    LOG_ERR("failed to start input capture session, quitting: %s", error->message);
    g_main_loop_quit(m_glibMainLoop);
    m_events->addEvent(Event(EventTypes::Quit));
    return;
  }

  auto restoreToken = QString(xdp_input_capture_session_get_restore_token(m_session));
  if (!restoreToken.isEmpty()) {
    Settings::setValue(Settings::Server::XdpRestoreToken, restoreToken);
  }
#endif
  setupSession(m_session);
}

void PortalInputCapture::handleSetPointerBarriers(const GObject *, GAsyncResult *res)
{
  g_autoptr(GError) error = nullptr;

  const auto requested = static_cast<int>(m_barriers.size());
  auto failed_list = xdp_input_capture_session_set_pointer_barriers_finish(m_session, res, &error);
  if (failed_list) {
    auto it = failed_list;
    while (it) {
      guint id;
      g_object_get(it->data, "id", &id, nullptr);
      for (auto elem = m_barriers.begin(); elem != m_barriers.end(); elem++) {
        if (*elem == it->data) {
          int x1;
          int x2;
          int y1;
          int y2;

          g_object_get(G_OBJECT(*elem), "x1", &x1, "x2", &x2, "y1", &y1, "y2", &y2, nullptr);

          LOG_WARN("failed to apply barrier %d (%d/%d-%d/%d)", id, x1, y1, x2, y2);
          g_object_unref(*elem);
          m_barriers.erase(elem);
          std::erase_if(m_barrierInfo, [id](const auto &info) { return info.id == id; });
          break;
        }
      }
      it = it->next;
    }
  }
  g_list_free_full(failed_list, g_object_unref);

  enable();

  // A grab that is still active makes mutter reject the whole set ("failed to
  // apply barrier"). Retry once we are idle; a second total failure is geometry
  // and retrying it would loop.
  if (requested > 0 && m_barriers.empty() && m_recovery.noteAllBarriersRejected()) {
    LOG_INFO("all pointer barriers were rejected; retrying once input capture is idle");
    scheduleBarrierRetry();
  }
}

std::pair<int, int>
PortalInputCapture::mapPortalActivationToComputerPosition(guint barrierId, double rawX, double rawY) const
{
  auto x = static_cast<int>(rawX);
  auto y = static_cast<int>(rawY);

  const auto it = std::ranges::find_if(m_barrierInfo, [barrierId](const auto &info) { return info.id == barrierId; });
  if (it == m_barrierInfo.end()) {
    LOG_DEBUG("activated barrier %u is not in the current pointer barrier set", barrierId);
    return {x, y};
  }

  const auto zoneLeft = it->x;
  const auto zoneTop = it->y;
  const auto zoneRight = it->x + static_cast<gint>(it->width) - 1;
  const auto zoneBottom = it->y + static_cast<gint>(it->height) - 1;

  std::int32_t computerX;
  std::int32_t computerY;
  std::int32_t computerW;
  std::int32_t computerH;
  m_computer->getShape(computerX, computerY, computerW, computerH);

  if (Bounds portalBounds; getPortalBounds(portalBounds)) {
    x = scaleCoordinateBetweenRanges(rawX, portalBounds.left, portalBounds.right, computerX, computerX + computerW - 1);
    y = scaleCoordinateBetweenRanges(rawY, portalBounds.top, portalBounds.bottom, computerY, computerY + computerH - 1);
  } else {
    x = std::clamp(x, zoneLeft, zoneRight);
    y = std::clamp(y, zoneTop, zoneBottom);
  }

  // Monitor layout keeps the pointer on the monitor that was actually touched.
  // Legacy mode collapses every barrier onto the bounding-box edge the neighbour links use.
  using enum BarrierSide;
  if (monitorEdges()) {
    int scaledLeft = zoneLeft;
    int scaledRight = zoneRight;
    int scaledTop = zoneTop;
    int scaledBottom = zoneBottom;
    if (Bounds portalBounds; getPortalBounds(portalBounds)) {
      scaledLeft = scaleCoordinateBetweenRanges(
          zoneLeft, portalBounds.left, portalBounds.right, computerX, computerX + computerW - 1
      );
      scaledRight = scaleCoordinateBetweenRanges(
          zoneRight, portalBounds.left, portalBounds.right, computerX, computerX + computerW - 1
      );
      scaledTop = scaleCoordinateBetweenRanges(
          zoneTop, portalBounds.top, portalBounds.bottom, computerY, computerY + computerH - 1
      );
      scaledBottom = scaleCoordinateBetweenRanges(
          zoneBottom, portalBounds.top, portalBounds.bottom, computerY, computerY + computerH - 1
      );
    }
    switch (it->side) {
    case Left:
      x = scaledLeft;
      break;
    case Right:
      x = scaledRight;
      break;
    case Top:
      y = scaledTop;
      break;
    case Bottom:
      y = scaledBottom;
      break;
    }
    x = std::clamp(x, std::min(scaledLeft, scaledRight), std::max(scaledLeft, scaledRight));
    y = std::clamp(y, std::min(scaledTop, scaledBottom), std::max(scaledTop, scaledBottom));
    return {x, y};
  }

  switch (it->side) {
  case Left:
    x = computerX;
    break;
  case Right:
    x = computerX + computerW - 1;
    break;
  case Top:
    y = computerY;
    break;
  case Bottom:
    y = computerY + computerH - 1;
    break;
  }

  return {x, y};
}

std::pair<double, double> PortalInputCapture::mapPortalReleasePosition(double x, double y) const
{
  std::int32_t computerX;
  std::int32_t computerY;
  std::int32_t computerW;
  std::int32_t computerH;
  m_computer->getShape(computerX, computerY, computerW, computerH);

  const auto computerLeft = computerX;
  const auto computerTop = computerY;
  const auto computerRight = computerX + computerW - 1;
  const auto computerBottom = computerY + computerH - 1;
  const auto jumpZoneSize = m_computer->getJumpZoneSize();
  Bounds portalBounds;
  if (!getPortalBounds(portalBounds)) {
    return {x, y};
  }

  auto mappedX = static_cast<std::int32_t>(std::lround(x));
  auto mappedY = static_cast<std::int32_t>(std::lround(y));

  // A monitor layout releases inside whichever monitor was entered. Scaling the
  // computer point into the portal keeps it off the bounding-box edge.
  if (monitorEdges()) {
    return {
        static_cast<double>(
            scaleCoordinateBetweenRanges(x, computerLeft, computerRight, portalBounds.left, portalBounds.right)
        ),
        static_cast<double>(
            scaleCoordinateBetweenRanges(y, computerTop, computerBottom, portalBounds.top, portalBounds.bottom)
        )
    };
  }

  if (BarrierInfo releaseBarrier; getClosestReleaseBarrier(
          x, y, computerLeft, computerTop, computerRight, computerBottom, portalBounds, releaseBarrier
      )) {
    const Bounds releaseBounds = {
        releaseBarrier.x, releaseBarrier.y, releaseBarrier.x + static_cast<gint>(releaseBarrier.width) - 1,
        releaseBarrier.y + static_cast<gint>(releaseBarrier.height) - 1
    };

    using enum BarrierSide;
    switch (releaseBarrier.side) {
    case Left:
      mappedX = std::min(releaseBounds.left + jumpZoneSize, releaseBounds.right);
      mappedY = std::clamp(mappedY, releaseBounds.top, releaseBounds.bottom);
      break;
    case Right:
      mappedX = std::max(releaseBounds.right - jumpZoneSize, releaseBounds.left);
      mappedY = std::clamp(mappedY, releaseBounds.top, releaseBounds.bottom);
      break;
    case Top:
      mappedX = std::clamp(mappedX, releaseBounds.left, releaseBounds.right);
      mappedY = std::min(releaseBounds.top + jumpZoneSize, releaseBounds.bottom);
      break;
    case Bottom:
      mappedX = std::clamp(mappedX, releaseBounds.left, releaseBounds.right);
      mappedY = std::max(releaseBounds.bottom - jumpZoneSize, releaseBounds.top);
      break;
    }
  }

  return {mappedX, mappedY};
}

void PortalInputCapture::addBarrier(
    guint id, BarrierSide side, gint zoneX, gint zoneY, guint zoneWidth, guint zoneHeight
)
{
  gint x1 = 0;
  gint x2 = 0;
  gint y1 = 0;
  gint y2 = 0;

  using enum BarrierSide;
  switch (side) {
  case Left:
    x1 = zoneX;
    y1 = zoneY;
    x2 = zoneX;
    y2 = zoneY + static_cast<gint>(zoneHeight) - 1;
    break;
  case Right:
    x1 = zoneX + static_cast<gint>(zoneWidth);
    y1 = zoneY;
    x2 = x1;
    y2 = zoneY + static_cast<gint>(zoneHeight) - 1;
    break;
  case Top:
    x1 = zoneX;
    y1 = zoneY;
    x2 = zoneX + static_cast<gint>(zoneWidth) - 1;
    y2 = zoneY;
    break;
  case Bottom:
    x1 = zoneX;
    y1 = zoneY + static_cast<gint>(zoneHeight);
    x2 = zoneX + static_cast<gint>(zoneWidth) - 1;
    y2 = y1;
    break;
  }

  LOG_DEBUG("barrier (%s) %u at %d,%d-%d,%d", barrierSideName(side), id, x1, y1, x2, y2);
  m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
      g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER, "id", id, "x1", x1, "y1", y1, "x2", x2, "y2", y2, nullptr)
  ));
  m_barrierInfo.push_back({id, side, zoneX, zoneY, zoneWidth, zoneHeight, x1, y1, x2, y2});
}

void PortalInputCapture::addBarrierSegment(
    guint id, BarrierSide side, gint zoneX, gint zoneY, guint zoneWidth, guint zoneHeight, gint x1, gint y1, gint x2,
    gint y2
)
{
  if (x1 != x2 && y1 != y2) {
    return;
  }
  LOG_DEBUG("barrier (%s) %u at %d,%d-%d,%d", barrierSideName(side), id, x1, y1, x2, y2);
  m_barriers.push_back(XDP_INPUT_CAPTURE_POINTER_BARRIER(
      g_object_new(XDP_TYPE_INPUT_CAPTURE_POINTER_BARRIER, "id", id, "x1", x1, "y1", y1, "x2", x2, "y2", y2, nullptr)
  ));
  m_barrierInfo.push_back({id, side, zoneX, zoneY, zoneWidth, zoneHeight, x1, y1, x2, y2});
}

gboolean PortalInputCapture::initSession()
{
  LOG_DEBUG("setting up input capture session");
  XdpInputCaptureSession *session;
#ifdef HAVE_LIBPORTAL_INPUTCAPTURE_RESTORE
  g_autoptr(GError) error = nullptr;

  m_portalVersion = xdp_portal_get_input_capture_version_sync(
      m_portal,
      nullptr, // Cancellable
      nullptr
  );
  LOG_DEBUG("input capture version %d", m_portalVersion);

  switch (m_portalVersion) {
  case 0:
    LOG_WARN("portal bug: input capture version is %d", m_portalVersion);
    // fallthrough
  case 1:
    xdp_portal_create_input_capture_session(
        m_portal,
        nullptr, // parent
        static_cast<XdpInputCapability>(XDP_INPUT_CAPABILITY_KEYBOARD | XDP_INPUT_CAPABILITY_POINTER),
        nullptr, // cancellable
        [](GObject *obj, GAsyncResult *res, gpointer data) {
          static_cast<PortalInputCapture *>(data)->handleInitSession(obj, res);
        },
        this
    );
    break;
  default:
    session = xdp_portal_create_input_capture_session2_sync(
        m_portal,
        nullptr, // Cancellable
        &error
    );
    if (!session) {
      LOG_ERR("failed to initialize input capture session, quitting: %s", error->message);
      g_main_loop_quit(m_glibMainLoop);
      m_events->addEvent(Event(EventTypes::Quit));
      return FALSE;
    }
    m_session = session;
    xdp_session_request_clipboard(xdp_input_capture_session_get_session(session));
    xdp_input_capture_session_set_session_persistence(session, XDP_INPUT_CAPTURE_SESSION_PERSISTENCE_PERSISTENT);
    if (auto sessionToken = Settings::value(Settings::Server::XdpRestoreToken).toByteArray(); !sessionToken.isEmpty()) {
      xdp_input_capture_session_set_restore_token(session, strdup(sessionToken.data()));
    }
    xdp_input_capture_session_start(
        m_session,
        nullptr, // parent
        static_cast<XdpInputCapability>(XDP_INPUT_CAPABILITY_KEYBOARD | XDP_INPUT_CAPABILITY_POINTER),
        nullptr, // cancellable
        [](GObject *obj, GAsyncResult *res, gpointer data) {
          static_cast<PortalInputCapture *>(data)->handleStart(obj, res);
        },
        this
    );
    break;
  }
#else
  xdp_portal_create_input_capture_session(
      m_portal,
      nullptr, // parent
      static_cast<XdpInputCapability>(XDP_INPUT_CAPABILITY_KEYBOARD | XDP_INPUT_CAPABILITY_POINTER),
      nullptr, // cancellable
      [](GObject *obj, GAsyncResult *res, gpointer data) {
        static_cast<PortalInputCapture *>(data)->handleInitSession(obj, res);
      },
      this
  );
#endif

  return false;
}

void PortalInputCapture::enable()
{
  if (!m_enabled) {
    LOG_DEBUG("enabling the portal input capture session");
    xdp_input_capture_session_enable(m_session);
    m_enabled = true;
  }
}

void PortalInputCapture::disable()
{
  if (m_enabled) {
    LOG_DEBUG("disabling the portal input capture session");
    xdp_input_capture_session_disable(m_session);
    m_enabled = false;
  }
}

bool PortalInputCapture::release()
{
  const auto request = m_recovery.requestRelease(false);
  if (!request.plain && !request.atPosition) {
    return false;
  }
  queueRelease(request.plain, request.atPosition, 0, 0);
  return true;
}

bool PortalInputCapture::release(double x, double y)
{
  // Reading the zone bounds is only safe while a grab is held: that is when
  // barrier rebuilds are deferred and will not mutate them.
  if (!m_recovery.captured()) {
    return false;
  }
  bool inside = false;
  double mappedX = x;
  double mappedY = y;
  if (Bounds portalBounds; getPortalBounds(portalBounds)) {
    const auto mapped = mapPortalReleasePosition(x, y);
    mappedX = mapped.first;
    mappedY = mapped.second;
    inside = releasePositionInsideZone(
        mappedX, mappedY, portalBounds.left, portalBounds.top, portalBounds.right, portalBounds.bottom
    );
  }
  const auto request = m_recovery.requestRelease(inside);
  if (!request.plain && !request.atPosition) {
    return false;
  }
  queueRelease(request.plain, request.atPosition, mappedX, mappedY);
  return true;
}

std::shared_ptr<PortalInputCapture::CaptureRelease> PortalInputCapture::takeReleaseJob(guint &idleId)
{
  std::lock_guard lock(m_edgeMutex);
  idleId = m_releaseIdle;
  m_releaseIdle = 0;
  auto job = std::move(m_releaseJob);
  return job;
}

void PortalInputCapture::releaseSynchronously()
{
  guint releaseIdle = 0;
  auto releaseJob = takeReleaseJob(releaseIdle);
  if (releaseIdle != 0) {
    g_source_remove(releaseIdle);
  }
  releaseJob.reset();
  if (m_session == nullptr || !m_recovery.captured()) {
    return;
  }
  const auto id = m_recovery.activationId();
  LOG_DEBUG("releasing input capture session, id=%u", id);
  xdp_input_capture_session_release(m_session, id);
  m_recovery.setDeactivated(id);
}

void PortalInputCapture::queueRelease(bool plain, bool atPosition, double x, double y)
{
  if ((!plain && !atPosition) || !m_portalAlive.load()) {
    return;
  }

  auto job = std::make_shared<CaptureRelease>();
  job->plain = plain;
  job->atPosition = atPosition;
  job->x = x;
  job->y = y;
  job->activationId = m_recovery.activationId();

  guint previous = 0;
  auto discarded = takeReleaseJob(previous);
  {
    std::lock_guard lock(m_edgeMutex);
    m_releaseJob = job;
  }
  if (previous != 0) {
    g_source_remove(previous);
  }
  discarded.reset();

  const auto idle = g_idle_add(
      [](gpointer data) -> gboolean {
        auto *self = static_cast<PortalInputCapture *>(data);
        guint ignored = 0;
        auto job = self->takeReleaseJob(ignored);
        if (!job || !self->m_portalAlive.load() || self->m_session == nullptr) {
          return G_SOURCE_REMOVE;
        }
        // Plain Release is the call that actually drops the grab. ReleaseAt is
        // only a cursor warp, and mutter keeps the grab if the point is outside
        // a zone, so it is never the only call.
        if (job->atPosition) {
          LOG_DEBUG("releasing input capture session, id=%u x=%.1f y=%.1f", job->activationId, job->x, job->y);
          xdp_input_capture_session_release_at(self->m_session, job->activationId, job->x, job->y);
        }
        if (job->plain) {
          LOG_DEBUG("releasing input capture session, id=%u", job->activationId);
          xdp_input_capture_session_release(self->m_session, job->activationId);
        }
        return G_SOURCE_REMOVE;
      },
      this
  );

  if (idle == 0) {
    std::lock_guard lock(m_edgeMutex);
    if (m_releaseJob == job) {
      m_releaseJob.reset();
    }
    return;
  }

  std::lock_guard lock(m_edgeMutex);
  // The callback may already have cleared the id. Only publish it if this job
  // is still the one waiting to run.
  if (m_releaseJob == job) {
    m_releaseIdle = idle;
  }
}

void PortalInputCapture::scheduleBarrierRetry()
{
  guint previous = 0;
  {
    std::lock_guard lock(m_edgeMutex);
    previous = m_barrierRetry;
    m_barrierRetry = 0;
  }
  if (previous != 0) {
    g_source_remove(previous);
  }

  const auto id = g_timeout_add(
      200,
      [](gpointer data) -> gboolean {
        auto *self = static_cast<PortalInputCapture *>(data);
        {
          std::lock_guard lock(self->m_edgeMutex);
          self->m_barrierRetry = 0;
        }
        if (self->m_portalAlive.load() && self->m_session != nullptr &&
            self->m_recovery.phase() == InputCaptureRecovery::CapturePhase::Idle) {
          self->handleZonesChanged(self->m_session, nullptr, false);
        }
        return G_SOURCE_REMOVE;
      },
      this
  );

  std::lock_guard lock(m_edgeMutex);
  m_barrierRetry = id;
}

void PortalInputCapture::handleDisabled(const XdpInputCaptureSession *, const GVariant *)
{
  LOG_DEBUG("portal cb disabled");

  if (!m_enabled)
    return; // Nothing to do

  m_enabled = false;
  m_recovery.setDisabled();

  // FIXME: need some better heuristics here of when we want to enable again
  // But we don't know *why* we got disabled (and it's doubtfull we ever
  // will), so we just assume that the zones will change or something and we
  // can re-enable again
  // ... very soon
  guint previous = 0;
  {
    std::lock_guard lock(m_edgeMutex);
    previous = m_reenableTimeout;
    m_reenableTimeout = 0;
  }
  if (previous != 0) {
    g_source_remove(previous);
  }
  const auto reenable = g_timeout_add(
      1000,
      [](gpointer data) -> gboolean {
        auto *self = static_cast<PortalInputCapture *>(data);
        {
          std::lock_guard lock(self->m_edgeMutex);
          self->m_reenableTimeout = 0;
        }
        if (!self->m_portalAlive.load()) {
          return G_SOURCE_REMOVE;
        }
        self->enable();
        if (self->m_session != nullptr && self->m_recovery.takeDeferredBarriers()) {
          LOG_INFO("reapplying pointer barriers after input capture was re-enabled");
          self->handleZonesChanged(self->m_session, nullptr, true);
        }
        return G_SOURCE_REMOVE;
      },
      this
  );
  {
    std::lock_guard lock(m_edgeMutex);
    m_reenableTimeout = reenable;
  }
}

void PortalInputCapture::handleActivated(
    const XdpInputCaptureSession *, const std::uint32_t activationId, GVariant *options
)
{
  LOG_DEBUG("portal cb activated, id=%d", activationId);

  if (options) {
    gdouble x;
    gdouble y;
    if (g_variant_lookup(options, "cursor_position", "(dd)", &x, &y)) {
      auto warpX = static_cast<int>(x);
      auto warpY = static_cast<int>(y);

      guint barrierId = 0;

      if (const bool hasBarrierId = g_variant_lookup(options, "barrier_id", "u", &barrierId);
          hasBarrierId && barrierId > 0) {
        auto [mappedX, mappedY] = mapPortalActivationToComputerPosition(barrierId, x, y);
        warpX = mappedX;
        warpY = mappedY;
      } else if (!hasBarrierId) {
        LOG_DEBUG("portal activation has no barrier id, using raw cursor position");
      } else {
        LOG_DEBUG("portal activation barrier id is zero, using raw cursor position");
      }

      m_computer->warpCursor(warpX, warpY);
      m_events->addEvent(Event(
          EventTypes::PrimaryComputerMotionOnPrimary, m_computer->getEventTarget(),
          IPrimaryComputer::MotionInfo::alloc(warpX, warpY)
      ));
    } else {
      LOG_WARN("failed to get cursor position");
    }
  } else {
    LOG_WARN("activation has no options");
  }
  m_activationId = activationId;
  m_recovery.setActivated(activationId);

#ifdef HAVE_LIBPORTAL_CLIPBOARD
  if (m_session) {
    LOG_DEBUG("reading clipboard selection on activation");
    m_computer->sendClipboardEvent(EventTypes::ClipboardGrabbed, kClipboardClipboard);

    XdpSession *session = xdp_input_capture_session_get_session(m_session);
    const char **mimeTypes = xdp_session_get_selection_mime_types(session);

    if (mimeTypes && mimeTypes[0]) {
      LOG_DEBUG("clipboard current selection mime types: %s", PortalClipboard::formatMimeTypes(mimeTypes).constData());
      if (!xdp_session_is_selection_owned_by_session(session))
        readClipboardSelection(session);
    } else {
      LOG_DEBUG("no current clipboard selection");
    }

    claimClipboardOwnership(session);
    LOG_DEBUG("activation clipboard handling complete");
  } else {
    LOG_WARN("input capture activated without a session, skipping clipboard read");
  }
#endif
}

void PortalInputCapture::handleDeactivated(
    const XdpInputCaptureSession *, const std::uint32_t activationId, const GVariant *
)
{
  LOG_DEBUG("cb deactivated, id=%i", activationId);
  m_recovery.setDeactivated(activationId);
  if (m_session != nullptr && m_recovery.takeDeferredBarriers()) {
    LOG_INFO("reapplying pointer barriers after input capture released");
    handleZonesChanged(m_session, nullptr, true);
  }
}

void PortalInputCapture::handleZonesChanged(XdpInputCaptureSession *session, const GVariant *, bool fresh)
{
  if (m_recovery.requestBarrierUpdate(fresh) == InputCaptureRecovery::BarrierApply::Defer) {
    LOG_INFO("deferring pointer barrier update until input capture is released");
    return;
  }

  for (auto b : m_barriers)
    g_object_unref(b);
  m_barriers.clear();
  m_barrierInfo.clear();

  const auto activeSides = m_computer->activeSides();
  using enum DirectionMask;

  auto zones = xdp_input_capture_session_get_zones(session);

  struct Zone
  {
    gint x = 0;
    gint y = 0;
    guint width = 0;
    guint height = 0;

    gint right() const
    {
      return x + static_cast<gint>(width);
    }
    gint bottom() const
    {
      return y + static_cast<gint>(height);
    }
  };
  struct Interval
  {
    gint begin = 0;
    gint end = 0;
  };

  std::vector<Zone> zoneList;
  gint unionLeft = 0;
  gint unionTop = 0;
  gint unionRight = 0;
  gint unionBottom = 0;
  bool boundsInit = false;
  for (auto z = zones; z != nullptr; z = z->next) {
    Zone zone;
    g_object_get(z->data, "width", &zone.width, "height", &zone.height, "x", &zone.x, "y", &zone.y, nullptr);
    LOG_DEBUG("input capture zone, %dx%d@%d,%d", zone.width, zone.height, zone.x, zone.y);
    if (!boundsInit) {
      unionLeft = zone.x;
      unionTop = zone.y;
      unionRight = zone.right();
      unionBottom = zone.bottom();
      boundsInit = true;
    } else {
      unionLeft = std::min(unionLeft, zone.x);
      unionTop = std::min(unionTop, zone.y);
      unionRight = std::max(unionRight, zone.right());
      unionBottom = std::max(unionBottom, zone.bottom());
    }
    zoneList.push_back(zone);
  }

  if (boundsInit && unionRight > unionLeft && unionBottom > unionTop) {
    m_zoneUnion = {unionLeft, unionTop, unionRight - 1, unionBottom - 1};
    m_hasZoneUnion = true;
  } else {
    m_hasZoneUnion = false;
  }

  // A barrier on a seam shared by two zones is rejected ("adjacent to multiple
  // monitor edges") and fails the whole set. Free intervals are the parts of a
  // zone edge that no other zone touches, including concave notches inside the
  // bounding box. Those notches are still the outside boundary of the union.
  const auto freeAlong = [](gint begin, gint end, std::vector<Interval> blocked) {
    std::sort(blocked.begin(), blocked.end(), [](const Interval &a, const Interval &b) { return a.begin < b.begin; });
    std::vector<Interval> free;
    gint cursor = begin;
    for (const auto &block : blocked) {
      const gint blockBegin = std::max(block.begin, begin);
      const gint blockEnd = std::min(block.end, end);
      if (blockEnd <= blockBegin) {
        continue;
      }
      if (blockBegin > cursor) {
        free.push_back({cursor, blockBegin});
      }
      cursor = std::max(cursor, blockEnd);
    }
    if (cursor < end) {
      free.push_back({cursor, end});
    }
    return free;
  };

  const auto blockedIntervals = [&zoneList](const Zone &zone, BarrierSide side) {
    std::vector<Interval> blocked;
    for (const auto &other : zoneList) {
      if (&other == &zone) {
        continue;
      }
      bool touches = false;
      Interval interval;
      using enum BarrierSide;
      switch (side) {
      case Left:
        touches = other.right() == zone.x;
        interval = {other.y, other.bottom()};
        break;
      case Right:
        touches = other.x == zone.right();
        interval = {other.y, other.bottom()};
        break;
      case Top:
        touches = other.bottom() == zone.y;
        interval = {other.x, other.right()};
        break;
      case Bottom:
        touches = other.y == zone.bottom();
        interval = {other.x, other.right()};
        break;
      }
      if (touches) {
        blocked.push_back(interval);
      }
    }
    return blocked;
  };

  guint id = 0;
  const bool useMonitorEdges = monitorEdges();
  if (!useMonitorEdges) {
    for (const auto &zone : zoneList) {
      if ((activeSides & static_cast<int>(LeftMask)) && zone.x == unionLeft) {
        addBarrier(++id, BarrierSide::Left, zone.x, zone.y, zone.width, zone.height);
      }
      if ((activeSides & static_cast<int>(RightMask)) && zone.right() == unionRight) {
        addBarrier(++id, BarrierSide::Right, zone.x, zone.y, zone.width, zone.height);
      }
      if ((activeSides & static_cast<int>(TopMask)) && zone.y == unionTop) {
        addBarrier(++id, BarrierSide::Top, zone.x, zone.y, zone.width, zone.height);
      }
      if ((activeSides & static_cast<int>(BottomMask)) && zone.bottom() == unionBottom) {
        addBarrier(++id, BarrierSide::Bottom, zone.x, zone.y, zone.width, zone.height);
      }
    }
  } else {
    std::int32_t computerX = 0;
    std::int32_t computerY = 0;
    std::int32_t computerW = 0;
    std::int32_t computerH = 0;
    m_computer->getShape(computerX, computerY, computerW, computerH);
    const int computerRight = computerX + computerW - 1;
    const int computerBottom = computerY + computerH - 1;
    const int portalLeft = unionLeft;
    const int portalTop = unionTop;
    const int portalRight = std::max(unionLeft, unionRight - 1);
    const int portalBottom = std::max(unionTop, unionBottom - 1);
    const auto edges = switchEdges();
    constexpr int kPositionTolerance = 8;

    const auto toComputerX = [&](double value) {
      return scaleCoordinateBetweenRanges(value, portalLeft, portalRight, computerX, computerRight);
    };
    const auto toComputerY = [&](double value) {
      return scaleCoordinateBetweenRanges(value, portalTop, portalBottom, computerY, computerBottom);
    };
    const auto toPortalX = [&](double value) {
      return scaleCoordinateBetweenRanges(value, computerX, computerRight, portalLeft, portalRight);
    };
    const auto toPortalY = [&](double value) {
      return scaleCoordinateBetweenRanges(value, computerY, computerBottom, portalTop, portalBottom);
    };

    const auto placeFree = [&](bool matchEdges) {
      int placed = 0;
      for (const auto &zone : zoneList) {
        const struct Side
        {
          BarrierSide barrier;
          Direction direction;
          bool vertical;
          gint line;
          gint inclusive;
          gint alongBegin;
          gint alongEnd;
        } sides[] = {
            {BarrierSide::Left, Direction::Left, true, zone.x, zone.x, zone.y, zone.bottom()},
            {BarrierSide::Right, Direction::Right, true, zone.right(), zone.right() - 1, zone.y, zone.bottom()},
            {BarrierSide::Top, Direction::Top, false, zone.y, zone.y, zone.x, zone.right()},
            {BarrierSide::Bottom, Direction::Bottom, false, zone.bottom(), zone.bottom() - 1, zone.x, zone.right()},
        };
        for (const auto &side : sides) {
          const auto free = freeAlong(side.alongBegin, side.alongEnd, blockedIntervals(zone, side.barrier));
          for (const auto &interval : free) {
            if (interval.end <= interval.begin) {
              continue;
            }
            const auto addSegment = [&](gint along0, gint along1) {
              if (along1 < along0) {
                std::swap(along0, along1);
              }
              gint x1 = 0;
              gint y1 = 0;
              gint x2 = 0;
              gint y2 = 0;
              if (side.vertical) {
                x1 = x2 = side.line;
                y1 = along0;
                y2 = along1;
              } else {
                y1 = y2 = side.line;
                x1 = along0;
                x2 = along1;
              }
              addBarrierSegment(++id, side.barrier, zone.x, zone.y, zone.width, zone.height, x1, y1, x2, y2);
              ++placed;
            };
            if (!matchEdges) {
              addSegment(interval.begin, interval.end - 1);
              continue;
            }

            const int computerPosition = side.vertical ? toComputerX(side.inclusive) : toComputerY(side.inclusive);
            const int scaledBegin = side.vertical ? toComputerY(interval.begin) : toComputerX(interval.begin);
            const int scaledEnd = side.vertical ? toComputerY(interval.end - 1) : toComputerX(interval.end - 1);
            const int alongMin = std::min(scaledBegin, scaledEnd);
            const int alongMax = std::max(scaledBegin, scaledEnd);
            for (const auto &edge : edges) {
              if (edge.side != side.direction || std::abs(edge.position - computerPosition) > kPositionTolerance) {
                continue;
              }
              const int overlapBegin = std::max(edge.begin, alongMin);
              const int overlapEnd = std::min(edge.end, alongMax);
              if (overlapEnd < overlapBegin) {
                continue;
              }
              gint portalBegin = 0;
              gint portalEnd = 0;
              if (side.vertical) {
                portalBegin = std::clamp(toPortalY(overlapBegin), interval.begin, interval.end - 1);
                portalEnd = std::clamp(toPortalY(overlapEnd), interval.begin, interval.end - 1);
              } else {
                portalBegin = std::clamp(toPortalX(overlapBegin), interval.begin, interval.end - 1);
                portalEnd = std::clamp(toPortalX(overlapEnd), interval.begin, interval.end - 1);
              }
              addSegment(portalBegin, portalEnd);
            }
          }
        }
      }
      return placed;
    };

    if (placeFree(true) == 0 && !edges.empty()) {
      LOG_WARN("monitor switch edges did not match portal zones; capturing every free monitor edge");
      placeFree(false);
    }
  }

  GList *list = nullptr;
  for (auto const &b : m_barriers) {
    list = g_list_append(list, b);
  }

  if (list != nullptr) {
    xdp_input_capture_session_set_pointer_barriers(
        m_session, list,
        nullptr, // cancellable
        [](GObject *obj, GAsyncResult *res, gpointer data) {
          static_cast<PortalInputCapture *>(data)->handleSetPointerBarriers(obj, res);
        },
        this
    );
  } else {
    LOG_WARN("no input capture pointer barriers found");
  }
}

void PortalInputCapture::glibThread(const void *)
{
  auto context = g_main_loop_get_context(m_glibMainLoop);

  LOG_DEBUG("glib thread running");

  while (g_main_loop_is_running(m_glibMainLoop)) {
    Thread::testCancel();
    g_main_context_iteration(context, true);
  }

  LOG_DEBUG("shutting down glib thread");
}

} // namespace deskflow
