/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ServerConfig.h"

#include <QSet>
#include <QWidget>

class QCheckBox;
class QLabel;

//! Drag computers so their monitors share one virtual desktop.
class MonitorLayoutWidget : public QWidget
{
  Q_OBJECT

public:
  explicit MonitorLayoutWidget(ServerConfig &config, QWidget *parent = nullptr);

  void reloadFromConfig();
  void applyReportedGeometry(const QString &computer, const QString &body);

Q_SIGNALS:
  void layoutChanged();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  struct Transform
  {
    double scale = 1.0;
    double originX = 0.0;
    double originY = 0.0;
  };

  LayoutMonitor defaultMonitor(const QString &name) const;
  Transform viewTransform() const;
  int computerAt(const QPointF &widgetPoint) const;
  void snap(LayoutComputer &computer) const;
  void store();
  void reloadReported();
  QList<LayoutMonitor> parseMonitors(const QString &body) const;
  void ensureGridComputers();

  ServerConfig &m_config;
  QList<LayoutComputer> m_layout;
  QSet<QString> m_reported;
  QString m_serverName;
  QCheckBox *m_enabled = nullptr;
  QLabel *m_hint = nullptr;
  int m_dragIndex = -1;
  QPointF m_dragLast;
};
