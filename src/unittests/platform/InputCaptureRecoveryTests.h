/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class InputCaptureRecoveryTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void disconnectWhileActive_releasesGrabAndDefersBarriers();
  void heartbeatTimeoutWhileActive_usesTheSameReleasePath();
  void reconnectDuringReturn_doesNotReactivateOrApplyBarriersEarly();
  void barriersReapplyOnceAfterReturn();
  void releaseOutsideZone_usesPlainReleaseOnly();
  void secondReleaseWhileInFlight_doesNotFlap();
  void staleDeactivate_doesNotDropANewerGrab();
  void rejectedBarriersWhileHeld_waitForReleaseThenRetryOnce();
  void releasePosition_rejectsTheZoneEdge();
  void nudgeRelease_bottomEdgeMovesInward();
  void nudgeRelease_leftEdgeMovesInward();
  void nudgeRelease_ignoresOtherZones();
  void rapidBounce_releasesTheNewGrabAndDropsTheStaleRelease();
  void rapidBounce_lateStopsDoNotRetireTheNewGrab();
  void deviceStopsWithoutDeactivate_keepTheGrab();
  void leaveDuringReturn_keepsTheNewGrab();
  void enabledSessionRejectsBarriers_recreatesInsteadOfRetrying();
  void emulatingDevices_holdTheGrabUntilTheLastOneStops();
};
