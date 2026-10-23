/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <mutex>

namespace deskflow {

/// Decides when a Wayland input-capture grab may be released and when pointer
/// barriers may be replaced.
///
/// Mutter rejects SetPointerBarriers while a capture is activated, and it
/// rejects ReleaseAt when the cursor point is outside a zone. Either failure
/// leaves the EIS client holding buttons, so local clicks never reach
/// applications. This state machine keeps the grab marked held until the
/// portal reports deactivation, refuses barrier updates until then, and sends
/// a plain Release so a bad cursor point cannot leave the grab up.
class InputCaptureRecovery
{
public:
  enum class CapturePhase
  {
    Idle,
    Captured,
    Releasing
  };

  enum class BarrierApply
  {
    Now,
    Defer
  };

  struct ReleaseRequest
  {
    bool plain = false;
    bool atPosition = false;
    /// Grab this release applies to. Zero when nothing should be sent.
    std::uint32_t activationId = 0;
  };

  /// What to do with a release that was queued before the portal thread ran it.
  struct QueuedRelease
  {
    bool send = false;
    std::uint32_t activationId = 0;
  };

  void setActivated(std::uint32_t activationId)
  {
    std::lock_guard lock(m_mutex);
    // EIS start_emulating and the DBus Activated signal both report the same
    // grab. A duplicate must not forget a Release already sent for it.
    if (activationId == m_activationId && m_phase != CapturePhase::Idle) {
      return;
    }
    if (activationId != m_activationId) {
      m_emulatingDevices = 0;
    }
    activateUnlocked(activationId);
  }

  /// EIS start_emulating. The sequence number is the portal activation id.
  /// Devices are counted so one device stopping does not look like the grab ended.
  void noteEmulating(std::uint32_t sequence)
  {
    std::lock_guard lock(m_mutex);
    if (sequence != m_activationId) {
      // stop_emulating carries no sequence. The previous grab's stops are
      // still queued; count them so they cannot retire this new grab.
      m_staleStops += m_emulatingDevices;
      m_emulatingDevices = 0;
      m_deactivatePending = false;
      activateUnlocked(sequence);
    } else if (m_phase == CapturePhase::Idle) {
      activateUnlocked(sequence);
    }
    ++m_emulatingDevices;
  }

  /// EIS stop_emulating. The stop event has no sequence, so this counts down
  /// the devices started for the current grab. Returns true only when the
  /// portal has also deactivated and the last current device has stopped.
  /// A stop on its own does not end the grab: the stops after a barrier
  /// bounce belong to the activation we already left.
  bool noteStoppedEmulating()
  {
    std::lock_guard lock(m_mutex);
    if (m_staleStops > 0) {
      --m_staleStops;
      if (m_staleStops == 0 && m_emulatingDevices == 0 && m_deactivatePending) {
        return deactivateUnlocked(m_activationId);
      }
      return false;
    }
    // A stop with no matching start belongs to a grab we already replaced.
    if (m_emulatingDevices == 0) {
      return false;
    }
    --m_emulatingDevices;
    if (m_emulatingDevices > 0 || !m_deactivatePending) {
      return false;
    }
    return deactivateUnlocked(m_activationId);
  }

  /// \c activationId 0 accepts whatever grab is current. A stale id is ignored
  /// so a late deactivate cannot drop a newer grab.
  void setDeactivated(std::uint32_t activationId)
  {
    std::lock_guard lock(m_mutex);
    if (m_phase == CapturePhase::Idle) {
      return;
    }
    if (activationId != 0 && m_activationId != 0 && activationId != m_activationId) {
      return;
    }
    // The compositor can emit Deactivated before the matching stop_emulating
    // events have been dispatched. Those events still prove the grab is live.
    // Stale stops are the previous grab's and must drain before this one ends.
    if (m_emulatingDevices > 0 || m_staleStops > 0) {
      m_deactivatePending = true;
      return;
    }
    deactivateUnlocked(activationId);
  }

  void setDisabled()
  {
    std::lock_guard lock(m_mutex);
    // A disable signal while EIS is still emulating means the grab was not
    // actually dropped (GNOME's Disable leaves the pointer locked). Stay
    // captured so the return path can still Release or recreate.
    if (m_emulatingDevices > 0) {
      return;
    }
    m_phase = CapturePhase::Idle;
    m_plainReleaseSent = false;
    m_releaseSentForId = 0;
    m_releaseDispatched = false;
    m_wantRelease = false;
    m_deactivatePending = false;
    m_staleStops = 0;
  }

  /// Ask the portal to give the pointer back.
  ///
  /// A second call for the same activation does nothing. A new activation
  /// (the pointer bounced off a barrier and the compositor grabbed again)
  /// is a different grab and must be released on its own. The portal spec
  /// ignores Release for an id that is no longer current, so suppressing
  /// that second call leaves the new grab held.
  ReleaseRequest requestRelease(bool positionIsInsideZone)
  {
    std::lock_guard lock(m_mutex);
    ReleaseRequest request;
    if (m_phase == CapturePhase::Idle && m_emulatingDevices == 0) {
      return request;
    }
    if (m_phase == CapturePhase::Idle) {
      m_phase = CapturePhase::Captured;
    }
    const auto id = m_activationId;
    if (m_plainReleaseSent && m_releaseSentForId == id) {
      return request;
    }
    m_phase = CapturePhase::Releasing;
    m_plainReleaseSent = true;
    m_releaseSentForId = id;
    m_releaseDispatched = false;
    m_wantRelease = true;
    request.plain = true;
    request.atPosition = positionIsInsideZone;
    request.activationId = id;
    return request;
  }

  /// The pointer left the primary again before the queued Release ran.
  /// Drop it so the grab the client needs is not released.
  void cancelQueuedRelease()
  {
    std::lock_guard lock(m_mutex);
    m_wantRelease = false;
    if (m_releaseDispatched) {
      return;
    }
    m_plainReleaseSent = false;
    m_releaseSentForId = 0;
    if (m_phase == CapturePhase::Releasing) {
      m_phase = CapturePhase::Captured;
    }
  }

  /// The glib idle is about to call Release. A queued id that is no longer
  /// the current grab must not be sent: mutter applies a late Release to
  /// whatever grab is current and the new one dies with no Deactivated signal
  /// we can match.
  QueuedRelease claimQueuedRelease(std::uint32_t queuedId)
  {
    std::lock_guard lock(m_mutex);
    QueuedRelease result;
    if (!m_wantRelease) {
      return result;
    }
    if (m_activationId != 0 && queuedId != m_activationId) {
      return result;
    }
    m_releaseDispatched = true;
    result.send = true;
    result.activationId = queuedId;
    return result;
  }

  /// Barrier changes during an active grab are remembered and applied once
  /// the portal has deactivated. \c fresh resets the one-shot retry budget
  /// used when the portal rejects a whole barrier set.
  BarrierApply requestBarrierUpdate(bool fresh)
  {
    std::lock_guard lock(m_mutex);
    if (m_phase != CapturePhase::Idle || m_emulatingDevices > 0) {
      m_barriersDirty = true;
      return BarrierApply::Defer;
    }
    m_barriersDirty = false;
    if (fresh) {
      m_barrierAttempts = 0;
    }
    return BarrierApply::Now;
  }

  /// True once, after a deferred update and a completed release.
  bool takeDeferredBarriers()
  {
    std::lock_guard lock(m_mutex);
    if (m_recreateSession || m_phase != CapturePhase::Idle || !m_barriersDirty) {
      return false;
    }
    m_barriersDirty = false;
    return true;
  }

  /// GNOME rejects SetPointerBarriers once the session has been enabled
  /// ("Session already enabled"). Calling it again fails the same way, and
  /// Disable() leaves the pointer grabbed, so the session has to be recreated.
  enum class SessionRebuild
  {
    NotNeeded,
    Now,
    AfterRelease
  };

  SessionRebuild noteEnabledSessionRejectedBarriers()
  {
    std::lock_guard lock(m_mutex);
    m_barriersDirty = true;
    if (m_phase != CapturePhase::Idle || m_emulatingDevices > 0) {
      m_recreateSession = true;
      return SessionRebuild::AfterRelease;
    }
    m_recreateSession = false;
    m_barriersDirty = false;
    return SessionRebuild::Now;
  }

  /// True once, when a recreate was deferred until the grab ended.
  bool takeRecreateSession()
  {
    std::lock_guard lock(m_mutex);
    if (!m_recreateSession || m_phase != CapturePhase::Idle || m_emulatingDevices > 0) {
      return false;
    }
    m_recreateSession = false;
    m_barriersDirty = false;
    return true;
  }

  /// The portal rejected every barrier. Returns true when the caller should
  /// schedule a single retry now. While a grab is still held the update stays
  /// deferred and the deactivate path applies it.
  bool noteAllBarriersRejected()
  {
    std::lock_guard lock(m_mutex);
    m_barriersDirty = true;
    if (m_phase != CapturePhase::Idle) {
      return false;
    }
    if (m_barrierAttempts >= 1) {
      return false;
    }
    ++m_barrierAttempts;
    return true;
  }

  bool captured() const
  {
    std::lock_guard lock(m_mutex);
    return m_phase != CapturePhase::Idle;
  }

  bool emulating() const
  {
    std::lock_guard lock(m_mutex);
    return m_emulatingDevices > 0;
  }

  CapturePhase phase() const
  {
    std::lock_guard lock(m_mutex);
    return m_phase;
  }

  std::uint32_t activationId() const
  {
    std::lock_guard lock(m_mutex);
    return m_activationId;
  }

  bool barriersDirty() const
  {
    std::lock_guard lock(m_mutex);
    return m_barriersDirty;
  }

private:
  void activateUnlocked(std::uint32_t activationId)
  {
    m_phase = CapturePhase::Captured;
    m_activationId = activationId;
    m_plainReleaseSent = false;
    m_releaseSentForId = 0;
    m_releaseDispatched = false;
    // A Release queued for the previous activation must not be sent. Mutter
    // applies it to whichever grab is current and that new grab then dies
    // with no Deactivated we can match.
    m_wantRelease = false;
  }

  /// \return true when this call moved a live grab to idle.
  bool deactivateUnlocked(std::uint32_t)
  {
    if (m_phase == CapturePhase::Idle) {
      return false;
    }
    m_phase = CapturePhase::Idle;
    m_plainReleaseSent = false;
    m_releaseSentForId = 0;
    m_releaseDispatched = false;
    m_wantRelease = false;
    m_deactivatePending = false;
    m_emulatingDevices = 0;
    m_staleStops = 0;
    return true;
  }

  mutable std::mutex m_mutex;
  CapturePhase m_phase = CapturePhase::Idle;
  std::uint32_t m_activationId = 0;
  std::uint32_t m_releaseSentForId = 0;
  int m_emulatingDevices = 0;
  int m_staleStops = 0;
  bool m_barriersDirty = false;
  bool m_plainReleaseSent = false;
  bool m_releaseDispatched = false;
  bool m_wantRelease = false;
  bool m_deactivatePending = false;
  bool m_recreateSession = false;
  int m_barrierAttempts = 0;
};

/// ReleaseAt fails, and leaves the grab active, when the point is on or
/// outside the zone edge. The center of a zone is inside; a barrier line is not.
inline bool releasePositionInsideZone(double x, double y, int left, int top, int right, int bottom)
{
  return x > left && y > top && x < right && y < bottom;
}

} // namespace deskflow
