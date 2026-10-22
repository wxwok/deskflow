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
  };

  void setActivated(std::uint32_t activationId)
  {
    std::lock_guard lock(m_mutex);
    m_phase = CapturePhase::Captured;
    m_activationId = activationId;
    m_plainReleaseSent = false;
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
    m_phase = CapturePhase::Idle;
    m_plainReleaseSent = false;
  }

  void setDisabled()
  {
    std::lock_guard lock(m_mutex);
    m_phase = CapturePhase::Idle;
    m_plainReleaseSent = false;
  }

  /// Ask the portal to give the pointer back. A second call while a release
  /// is already in flight does nothing, so reconnects and queued motion do
  /// not flap the grab.
  ReleaseRequest requestRelease(bool positionIsInsideZone)
  {
    std::lock_guard lock(m_mutex);
    ReleaseRequest request;
    if (m_phase != CapturePhase::Captured && m_phase != CapturePhase::Releasing) {
      return request;
    }
    if (m_plainReleaseSent) {
      return request;
    }
    m_phase = CapturePhase::Releasing;
    m_plainReleaseSent = true;
    request.plain = true;
    request.atPosition = positionIsInsideZone;
    return request;
  }

  /// Barrier changes during an active grab are remembered and applied once
  /// the portal has deactivated. \c fresh resets the one-shot retry budget
  /// used when the portal rejects a whole barrier set.
  BarrierApply requestBarrierUpdate(bool fresh)
  {
    std::lock_guard lock(m_mutex);
    if (m_phase != CapturePhase::Idle) {
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
    if (m_phase != CapturePhase::Idle || !m_barriersDirty) {
      return false;
    }
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
  mutable std::mutex m_mutex;
  CapturePhase m_phase = CapturePhase::Idle;
  std::uint32_t m_activationId = 0;
  bool m_barriersDirty = false;
  bool m_plainReleaseSent = false;
  int m_barrierAttempts = 0;
};

/// ReleaseAt fails, and leaves the grab active, when the point is on or
/// outside the zone edge. The center of a zone is inside; a barrier line is not.
inline bool releasePositionInsideZone(double x, double y, int left, int top, int right, int bottom)
{
  return x > left && y > top && x < right && y < bottom;
}

} // namespace deskflow
