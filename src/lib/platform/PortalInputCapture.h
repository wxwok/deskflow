/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2022, 2026 Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/MonitorGeometry.h"
#include "mt/Thread.h"
#include "platform/EiComputer.h"
#include "platform/InputCaptureRecovery.h"

#include <QByteArray>

#include <glib.h>
#include <libportal/inputcapture.h>
#include <libportal/portal.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace deskflow {

class EiClipboard;

class PortalInputCapture
{
public:
  PortalInputCapture(EiComputer *computer, IEventQueue *events);
  ~PortalInputCapture();

  // Get the clipboard for the specified ID
  EiClipboard *getClipboard(ClipboardID id) const;
  void enable();
  void disable();
  /// Queue a plain Release. Returns false when a release is already in flight.
  bool release();
  /// Queue one Release: ReleaseAt when \c x, \c y map inside a zone, otherwise a plain Release.
  bool release(double x, double y);
  /// True while a Release has been requested and the portal has not deactivated.
  bool releaseInFlight() const
  {
    return m_recovery.phase() == InputCaptureRecovery::CapturePhase::Releasing;
  }
  /// Release on this thread and mark the grab idle. Used when the session is about to be destroyed.
  void releaseSynchronously();
  /// Drop a Release queued by returning home. The pointer left again and the
  /// new grab has to stay up so the client keeps receiving it.
  void cancelPendingRelease();
  /// EIS start_emulating. \c sequence is the portal activation id.
  void noteEmulating(std::uint32_t sequence);
  /// EIS stop_emulating. The event carries no sequence number.
  void noteStoppedEmulating();
  bool isEmulating() const
  {
    return m_recovery.emulating();
  }
  void setSwitchEdges(std::vector<deskflow::EdgeSpan> edges);
  bool isActive() const
  {
    return m_recovery.captured();
  }

private:
  void glibThread(const void *);
  gboolean timeoutHandler() const;
  gboolean initSession();
  void setupSession(XdpInputCaptureSession *session);
  void handleStart(GObject *object, GAsyncResult *res);
  void handleInitSession(GObject *object, GAsyncResult *res);
  void handleSetPointerBarriers(const GObject *object, GAsyncResult *res);
  void handleSessionClosed(XdpSession *session);
  void handleDisabled(const XdpInputCaptureSession *session, const GVariant *option);
  void handleActivated(const XdpInputCaptureSession *session, const std::uint32_t activationId, GVariant *options);
  void
  handleDeactivated(const XdpInputCaptureSession *session, const std::uint32_t activationId, const GVariant *options);
  void handleZonesChanged(XdpInputCaptureSession *session, const GVariant *options, bool fresh = true);
  void queueRelease(bool plain, bool atPosition, double x, double y, std::uint32_t activationId);
  void scheduleBarrierRetry();
  void scheduleDeferredBarrierApply();
  void onCaptureBecameIdle();
  /// Release a live grab and Close the portal session. Safe to call once.
  void releaseAndCloseSession();

  struct CaptureRelease
  {
    bool plain = false;
    bool atPosition = false;
    double x = 0;
    double y = 0;
    std::uint32_t activationId = 0;
  };

  /// Drop a queued Release and return the job so the caller can free it after
  /// the idle source is gone. The glib callback takes the same job under the lock.
  std::shared_ptr<CaptureRelease> takeReleaseJob(guint &idleId);

  void handleSelectionTransfer(XdpSession *session, const char *mimeType, uint32_t serial) const;
  void readClipboardSelection(XdpSession *session) const;
  void claimClipboardOwnership(XdpSession *session) const;

  /// g_signal_connect callback wrapper
  static void sessionClosed(XdpSession *session, const gpointer data)
  {
    static_cast<PortalInputCapture *>(data)->handleSessionClosed(session);
  }
  static void disabled(const XdpInputCaptureSession *session, const GVariant *options, const gpointer data)
  {
    static_cast<PortalInputCapture *>(data)->handleDisabled(session, options);
  }
  static void activated(
      const XdpInputCaptureSession *session, const std::uint32_t activationId, GVariant *options, const gpointer data
  )
  {
    static_cast<PortalInputCapture *>(data)->handleActivated(session, activationId, options);
  }
  static void deactivated(
      const XdpInputCaptureSession *session, const std::uint32_t activationId, const GVariant *options,
      const gpointer data
  )
  {
    static_cast<PortalInputCapture *>(data)->handleDeactivated(session, activationId, options);
  }
  static void zonesChanged(XdpInputCaptureSession *session, const GVariant *options, const gpointer data)
  {
    static_cast<PortalInputCapture *>(data)->handleZonesChanged(session, options);
  }
  static void selectionTransfer(XdpSession *session, const char *mimeType, uint32_t serial, const gpointer data)
  {
    static_cast<PortalInputCapture *>(data)->handleSelectionTransfer(session, mimeType, serial);
  }

  enum class Signal : uint8_t
  {
    SessionClosed,
    Disabled,
    Activated,
    Deactivated,
    ZonesChanged,

    // Clipboard signals
    SelectionTransfer,
  };

  enum class BarrierSide : uint8_t
  {
    Left,
    Right,
    Top,
    Bottom
  };

  struct BarrierInfo
  {
    guint id = 0;
    BarrierSide side = BarrierSide::Left;
    gint x = 0;
    gint y = 0;
    guint width = 0;
    guint height = 0;
    gint x1 = 0;
    gint y1 = 0;
    gint x2 = 0;
    gint y2 = 0;
  };

  struct Bounds
  {
    gint left = 0;
    gint top = 0;
    gint right = 0;
    gint bottom = 0;
  };

  static const char *barrierSideName(BarrierSide side);
  static int
  scaleCoordinateBetweenRanges(double value, int sourceMin, int sourceMax, int destinationMin, int destinationMax);
  bool getPortalBounds(Bounds &bounds) const;
  bool getClosestReleaseBarrier(
      double x, double y, int computerLeft, int computerTop, int computerRight, int computerBottom,
      const Bounds &portalBounds, BarrierInfo &barrier
  ) const;
  std::pair<int, int> mapPortalActivationToComputerPosition(guint barrierId, double rawX, double rawY) const;
  void nudgeMappedReleaseAwayFromBarriers(double &x, double &y) const;
  std::pair<double, double> mapPortalReleasePosition(double x, double y) const;
  void addBarrier(guint id, BarrierSide side, gint zoneX, gint zoneY, guint zoneWidth, guint zoneHeight);
  void addBarrierSegment(
      guint id, BarrierSide side, gint zoneX, gint zoneY, guint zoneWidth, guint zoneHeight, gint x1, gint y1, gint x2,
      gint y2
  );
  bool monitorEdges() const
  {
    return m_monitorEdges;
  }
  std::vector<deskflow::EdgeSpan> switchEdges() const;

  EiComputer *m_computer = nullptr;
  IEventQueue *m_events = nullptr;
  int m_portalVersion = 0;

  Thread *m_glibThread;
  GMainLoop *m_glibMainLoop = nullptr;

  XdpPortal *m_portal = nullptr;
  XdpInputCaptureSession *m_session = nullptr;
  bool m_sessionClosed = false;

  std::map<Signal, gulong> m_signals = {
      {Signal::SessionClosed, 0},     {Signal::Disabled, 0},     {Signal::Activated, 0},
      {Signal::Deactivated, 0},       {Signal::ZonesChanged, 0},

      {Signal::SelectionTransfer, 0},
  };

  bool m_enabled = false;
  InputCaptureRecovery m_recovery;
  // Kept in sync with m_recovery for the portal calls that still want the id.
  std::uint32_t m_activationId = 0;
  guint m_releaseIdle = 0;
  std::shared_ptr<CaptureRelease> m_releaseJob;
  guint m_barrierRetry = 0;
  guint m_deferredBarrierIdle = 0;
  /// Set while a barrier reapply is waiting for the glib thread. The idle
  /// callback takes it; a recreate or destruction drops it so the callback no-ops.
  std::shared_ptr<bool> m_deferredBarrierToken;
  guint m_reenableTimeout = 0;

  std::vector<XdpInputCapturePointerBarrier *> m_barriers;
  std::vector<BarrierInfo> m_barrierInfo;
  mutable std::mutex m_edgeMutex;
  std::vector<deskflow::EdgeSpan> m_switchEdges;
  bool m_monitorEdges = false;
  bool m_rebuildQueued = false;
  guint m_rebuildIdle = 0;
  std::atomic<bool> m_portalAlive{true};
  Bounds m_zoneUnion;
  bool m_hasZoneUnion = false;

  EiClipboard *m_clipboard = nullptr;
};

} // namespace deskflow
