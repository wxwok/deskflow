/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/HotEdgeBar.h"

#include <QEnterEvent>
#include <QPainter>

#include <algorithm>

namespace deskflow::phone {

HotEdgeBar::HotEdgeBar(const QString &phoneId, QWidget *parent) : QWidget(parent), m_phoneId(phoneId)
{
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setMouseTracking(true);
  setToolTip(tr("Phone screen"));
}

void HotEdgeBar::place(const QRect &screenRect)
{
  setGeometry(screenRect);
}

void HotEdgeBar::enterEvent(QEnterEvent *event)
{
  QWidget::enterEvent(event);
  const bool vertical = height() >= width();
  const double span = vertical ? std::max(1, height()) : std::max(1, width());
  const double pos = vertical ? event->position().y() : event->position().x();
  const double fraction = std::clamp(pos / span, 0.0, 1.0);
  Q_EMIT entered(m_phoneId, fraction);
}

void HotEdgeBar::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  // Steel cyan, not the disabled gray used for a switched-off computer.
  painter.fillRect(rect(), QColor(90, 190, 210, 180));
}

} // namespace deskflow::phone
