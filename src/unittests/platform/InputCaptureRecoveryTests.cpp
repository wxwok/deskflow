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

void InputCaptureRecoveryTests::rapidBounce_releasesTheNewGrabAndDropsTheStaleRelease()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(1);
  const auto first = recovery.requestRelease(true);
  QCOMPARE(first.activationId, 1u);
  QVERIFY(first.plain);

  // The queued Release has not run yet. The cursor was placed on a barrier,
  // so the compositor activates id 2 (pointer and keyboard) first.
  recovery.noteEmulating(2);
  recovery.noteEmulating(2);
  const auto stale = recovery.claimQueuedRelease(1);
  QVERIFY(!stale.send);

  const auto second = recovery.requestRelease(false);
  QVERIFY(second.plain);
  QVERIFY(!second.atPosition);
  QCOMPARE(second.activationId, 2u);
  const auto live = recovery.claimQueuedRelease(2);
  QVERIFY(live.send);
  QCOMPARE(live.activationId, 2u);

  // Deactivated for the grab we already left must not clear the new one,
  // including while its devices are still emulating.
  recovery.setDeactivated(1);
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 2u);
}

void InputCaptureRecoveryTests::rapidBounce_lateStopsDoNotRetireTheNewGrab()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(1);
  recovery.noteEmulating(1);
  recovery.noteEmulating(1);
  const auto first = recovery.requestRelease(true);
  QCOMPARE(first.activationId, 1u);

  // ReleaseAt lands on the barrier. The compositor starts grab 4 before the
  // two stop_emulating events from grab 1 have been dispatched. Those stops
  // have no sequence number.
  recovery.noteEmulating(4);
  recovery.noteEmulating(4);
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 4u);

  const auto second = recovery.requestRelease(false);
  QVERIFY(second.plain);
  QVERIFY(!second.atPosition);
  QCOMPARE(second.activationId, 4u);

  // The new grab's own stops do not drop it until the portal says it ended.
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(recovery.captured());
  recovery.setDeactivated(4);
  QVERIFY(!recovery.captured());

  // Deactivated for the new grab can arrive while its devices are still
  // emulating and the previous grab's stops are still queued. A late
  // Deactivated for the old id must not satisfy that wait.
  InputCaptureRecovery waiting;
  waiting.setActivated(1);
  waiting.noteEmulating(1);
  waiting.noteEmulating(1);
  waiting.noteEmulating(8);
  waiting.noteEmulating(8);
  waiting.setDeactivated(1);
  QVERIFY(waiting.captured());
  QCOMPARE(waiting.activationId(), 8u);
  waiting.setDeactivated(8);
  QVERIFY(waiting.captured());
  QVERIFY(!waiting.noteStoppedEmulating());
  QVERIFY(!waiting.noteStoppedEmulating());
  QVERIFY(waiting.captured());
  QVERIFY(!waiting.noteStoppedEmulating());
  QVERIFY(waiting.noteStoppedEmulating());
  QVERIFY(!waiting.captured());
}

void InputCaptureRecoveryTests::deviceStopsWithoutDeactivate_keepTheGrab()
{
  InputCaptureRecovery recovery;
  recovery.noteEmulating(3);
  recovery.noteEmulating(3);
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 3u);

  recovery.setDeactivated(3);
  QVERIFY(!recovery.captured());
}

void InputCaptureRecoveryTests::leaveDuringReturn_keepsTheNewGrab()
{
  InputCaptureRecovery recovery;
  recovery.setActivated(1);
  QVERIFY(recovery.requestRelease(true).plain);
  recovery.noteEmulating(2);

  // Switched to the client before the glib idle ran.
  recovery.cancelQueuedRelease();
  const auto dropped = recovery.claimQueuedRelease(1);
  QVERIFY(!dropped.send);
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 2u);

  // Coming home again releases the grab that is current, not the old one.
  const auto home = recovery.requestRelease(true);
  QVERIFY(home.plain);
  QCOMPARE(home.activationId, 2u);
}

void InputCaptureRecoveryTests::enabledSessionRejectsBarriers_recreatesInsteadOfRetrying()
{
  InputCaptureRecovery held;
  held.setActivated(4);
  QCOMPARE(held.noteEnabledSessionRejectedBarriers(), InputCaptureRecovery::SessionRebuild::AfterRelease);
  held.noteStoppedEmulating();
  // No EIS start was counted, so the stop does not drop the DBus grab.
  QVERIFY(held.captured());
  held.setDeactivated(4);
  QVERIFY(held.takeRecreateSession());
  QVERIFY(!held.takeDeferredBarriers());

  InputCaptureRecovery idle;
  QCOMPARE(idle.noteEnabledSessionRejectedBarriers(), InputCaptureRecovery::SessionRebuild::Now);
  QVERIFY(!idle.takeRecreateSession());
  QCOMPARE(idle.noteAllBarriersRejected(), true);
}

void InputCaptureRecoveryTests::emulatingDevices_holdTheGrabUntilTheLastOneStops()
{
  InputCaptureRecovery recovery;
  recovery.noteEmulating(9);
  recovery.noteEmulating(9);
  recovery.setDeactivated(9);
  QVERIFY(recovery.captured());

  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(recovery.captured());
  QVERIFY(recovery.noteStoppedEmulating());
  QVERIFY(!recovery.captured());

  // A late stop from the previous grab must not retire a grab that has not
  // started emulating yet.
  recovery.setActivated(10);
  QVERIFY(!recovery.noteStoppedEmulating());
  QVERIFY(recovery.captured());
  QCOMPARE(recovery.activationId(), 10u);
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
