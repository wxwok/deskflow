/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class MonitorLayoutTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void edgeAdjacency();
  void travelMapping();
  void configRoundTrip();
  void snapAdjacencyAndAlignment();
  void snapAllowsOverlap();
  void switchEdgeSegments();
  void abuttingExitPrefersEdgeTravelOverStack();
  void stackTopReceivesTheOverlap();
  void stackFailoverDropsAndRestores();
  void stackFailoverOutsideTheLiveLayout();
  void layoutZOrderRoundTrip();
  void disabledComputerIsNotASwitchTarget();
  void unknownOrDisabledSwitchIsSkipped();

private:
  inline static const QString m_settingsPath = QStringLiteral("tmp/monitor-layout");
  inline static const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPath);
  inline static const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPath);
};
