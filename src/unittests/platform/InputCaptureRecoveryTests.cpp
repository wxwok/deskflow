/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "InputCaptureRecoveryTests.h"

#include "platform/InputCaptureRecovery.h"

using deskflow::InputCaptureRecovery;
using deskflow::releasePositionInsideZone;

void InputCaptureRecoveryTests::disconnectWhileActive_releasesGrabAndDefersBarriers()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(29);

  // The layout rebuild that follows "client has disconnected" asks for new
  // barriers while the grab from the switch to that client is still held.
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Defer);
  QVERIFY(recovery.barriersDirty());

  const auto release = recovery.requestRelease(true);
  QVERIFY(release.plain);
  QVERIFY(release.atPosition);
  QCOMPARE(recovery.phase(), InputCaptureRecovery::CapturePhase::Releasing);

  // Still held until the portal says so, so a second barrier request stays deferred.
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Defer);
  QVERIFY(recovery.captured());
}

void InputCaptureRecoveryTests::heartbeatTimeoutWhileActive_usesTheSameReleasePath()
{
  // "client is dead" takes the same return as a TCP disconnect: release the
  // grab, keep barriers deferred, then apply them after deactivation.
  InputCaptureRecovery recovery;
  recovery.setActivated(7);
  const auto release = recovery.requestRelease(true);
  QVERIFY(release.plain);
  QVERIFY(release.atPosition);
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Defer);

  recovery.setDeactivated(7);
  QVERIFY(!recovery.captured());
  QVERIFY(recovery.takeDeferredBarriers());
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Now);
}

void InputCaptureRecoveryTests::reconnectDuringReturn_doesNotReactivateOrApplyBarriersEarly()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(29);
  QVERIFY(recovery.requestRelease(true).plain);

  // The reconnected client rebuilds the layout. That must not apply barriers
  // yet, and it must not look like a new grab.
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Defer);
  QCOMPARE(recovery.activationId(), 29u);
  QVERIFY(recovery.captured());

  recovery.setDeactivated(29);
  QVERIFY(recovery.takeDeferredBarriers());
  QVERIFY(!recovery.captured());

  // After the pointer is home the reconnected computer is not active, so a
  // further layout update just applies. It does not release or re-grab.
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Now);
  const auto late = recovery.requestRelease(true);
  QVERIFY(!late.plain);
  QVERIFY(!late.atPosition);
  QVERIFY(!recovery.captured());
}

void InputCaptureRecoveryTests::barriersReapplyOnceAfterReturn()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(3);
  recovery.requestBarrierUpdate(true);
  recovery.requestRelease(false);
  recovery.setDeactivated(3);

  QVERIFY(recovery.takeDeferredBarriers());
  QVERIFY(!recovery.takeDeferredBarriers());
  QVERIFY(!recovery.barriersDirty());
  QCOMPARE(recovery.requestBarrierUpdate(true), InputCaptureRecovery::BarrierApply::Now);
}

void InputCaptureRecoveryTests::releaseOutsideZone_usesPlainReleaseOnly()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(4);
  const auto release = recovery.requestRelease(false);
  QVERIFY(release.plain);
  QVERIFY(!release.atPosition);
}

void InputCaptureRecoveryTests::secondReleaseWhileInFlight_doesNotFlap()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(4);
  QVERIFY(recovery.requestRelease(true).plain);

  const auto repeat = recovery.requestRelease(true);
  QVERIFY(!repeat.plain);
  QVERIFY(!repeat.atPosition);
  QCOMPARE(recovery.phase(), InputCaptureRecovery::CapturePhase::Releasing);
}

void InputCaptureRecoveryTests::staleDeactivate_doesNotDropANewerGrab()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(29);
  recovery.requestRelease(true);
  recovery.setDeactivated(28);
  QVERIFY(recovery.captured());

  recovery.setActivated(30);
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 30u);

  recovery.setDeactivated(30);
  QVERIFY(!recovery.captured());
}

void InputCaptureRecoveryTests::rejectedBarriersWhileHeld_waitForReleaseThenRetryOnce()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(1);
  recovery.requestRelease(false);

  // Rejected during the grab: remember them, do not retry yet.
  QVERIFY(!recovery.noteAllBarriersRejected());
  QVERIFY(recovery.barriersDirty());

  recovery.setDeactivated(1);
  QVERIFY(recovery.takeDeferredBarriers());

  // Idle and rejected again: one immediate retry, then stop.
  InputCaptureRecovery idle;
  QVERIFY(idle.noteAllBarriersRejected());
  QVERIFY(!idle.noteAllBarriersRejected());
}

void InputCaptureRecoveryTests::releasePosition_rejectsTheZoneEdge()
{
  QVERIFY(releasePositionInsideZone(10, 10, 0, 0, 100, 100));
  QVERIFY(!releasePositionInsideZone(0, 10, 0, 0, 100, 100));
  QVERIFY(!releasePositionInsideZone(100, 50, 0, 0, 100, 100));
  QVERIFY(!releasePositionInsideZone(50, 0, 0, 0, 100, 100));
  QVERIFY(!releasePositionInsideZone(50, 100, 0, 0, 100, 100));
}

QTEST_MAIN(InputCaptureRecoveryTests)
