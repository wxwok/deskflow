/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/MonitorSnap.h"
#include "gui/config/LayoutClients.h"
#include "gui/config/ServerConfig.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QWidget>

#include <cstdint>
#include <vector>

class QCheckBox;
class QLabel;
class QPainter;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

//! Drag computers so their monitors share one virtual desktop.
class MonitorLayoutWidget : public QWidget
{
  Q_OBJECT

public:
  explicit MonitorLayoutWidget(ServerConfig &config, QWidget *parent = nullptr);

  void reloadFromConfig();
  void reloadDiscovery();
  void applyReportedGeometry(const QString &computer, const QString &body);
  const QList<LayoutComputer> &layout() const
  {
    return m_layout;
  }

  //! True when an external section: layout has moved and Save has not written it yet.
  bool hasPendingLayoutWrite() const
  {
    return m_pendingLayoutWrite;
  }

  //! Write a pending external layout. No-op when there is nothing pending.
  bool commitPendingLayout();

  //! Widget-space center of a placed computer's first monitor. Null when it is not placed.
  QPoint placedComputerCenter(const QString &name) const;

Q_SIGNALS:
  void layoutChanged();
  //! The internal monitor layout was committed to the general config and the caller's ServerConfig.
  void authoritativeLayoutChanged();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void leaveEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Transform
  {
    double scale = 1.0;
    double originX = 0.0;
    double originY = 0.0;
  };

  LayoutMonitor defaultMonitor(const QString &name) const;
  Transform viewTransform() const;
  int computerAt(const QPointF &widgetPoint, bool includeServer) const;
  int snapThreshold(const Transform &transform) const;
  void moveComputer(int index, int originX, int originY, bool enableSnap);
  void syncOriginFields();
  void nudge(int dx, int dy, bool enableSnap);
  void store();
  void reloadReported();
  QList<LayoutMonitor> parseMonitors(const QString &body) const;
  void ensureGridComputers();
  void updateHint();
  void refreshDiscovery();
  void refreshParticipation();
  void addDiscovered(const QString &name);
  void toggleParticipation();
  QString participationError(deskflow::LayoutWriteError error) const;
  enum class StackMove
  {
    Front,
    Up,
    Down,
    Back
  };
  QList<int> overlapGroup(int index) const;
  QList<int> stackOrder(const QList<int> &group) const;
  QList<int> paintOrder() const;
  bool fullyCovered(int index) const;
  void raiseIfOverlapping(int index);
  void restack(StackMove move);
  void updateStackButtons();
  void showStackMenu(const QPoint &globalPos);
  void updateFan(const QPointF &widgetPoint);
  QPoint fanOffset(int index) const;
  void paintStackNames(QPainter *painter, const Transform &transform);
  bool isExternal() const;
  std::vector<deskflow::SnapRect> rectsFor(int index, int originX, int originY) const;
  QPointF mapLayout(const Transform &transform, int32_t x, int32_t y) const;

  ServerConfig &m_config;
  QList<LayoutComputer> m_layout;
  QList<LayoutComputer> m_committedLayout;
  bool m_pendingLayoutWrite = false;
  QSet<QString> m_reported;
  QString m_serverName;
  QWidget *m_controls = nullptr;
  QCheckBox *m_enabled = nullptr;
  QLabel *m_hint = nullptr;
  QLabel *m_discoveryLabel = nullptr;
  QLabel *m_status = nullptr;
  QWidget *m_discoveryHost = nullptr;
  QVBoxLayout *m_discoveryLayout = nullptr;
  QPushButton *m_participation = nullptr;
  QPushButton *m_stackFront = nullptr;
  QPushButton *m_stackUp = nullptr;
  QPushButton *m_stackDown = nullptr;
  QPushButton *m_stackBack = nullptr;
  QLabel *m_originLabel = nullptr;
  struct NameChip
  {
    int index = -1;
    QRect rect;
  };
  QList<NameChip> m_nameChips;
  //! A computer in the stack currently fanned open, or -1.
  int m_fanGroup = -1;
  bool m_reloading = false;
  QSpinBox *m_originX = nullptr;
  QSpinBox *m_originY = nullptr;
  int m_dragIndex = -1;
  int m_selected = -1;
  double m_freeX = 0.0;
  double m_freeY = 0.0;
  QPointF m_dragLast;
  bool m_updatingOrigin = false;
  std::vector<deskflow::SnapGuide> m_guides;
};
