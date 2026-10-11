/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/MonitorLayoutWidget.h"

#include "common/Settings.h"

#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kSnapScreenPixels = 40;
constexpr int kPlaceholderWidth = 1920;
constexpr int kPlaceholderHeight = 1080;

QRect monitorGlobal(const LayoutComputer &computer, const LayoutMonitor &monitor)
{
  return {computer.originX + monitor.x, computer.originY + monitor.y, monitor.width, monitor.height};
}

} // namespace

MonitorLayoutWidget::MonitorLayoutWidget(ServerConfig &config, QWidget *parent) : QWidget(parent), m_config(config)
{
  auto *layout = new QVBoxLayout(this);
  m_enabled = new QCheckBox(tr("Use monitor layout for pointer switching"), this);
  m_hint = new QLabel(
      tr("Drag a computer so its monitors touch the server. The server stays put. "
         "Edges that touch switch; edges that only meet another monitor of the same computer do not. "
         "Leave this off to keep the grid links."),
      this
  );
  m_hint->setWordWrap(true);
  auto *reload = new QPushButton(tr("Reload reported geometry"), this);
  layout->addWidget(m_enabled);
  layout->addWidget(m_hint);
  layout->addWidget(reload, 0, Qt::AlignLeft);
  layout->addStretch(1);
  setMinimumSize(640, 420);

  connect(m_enabled, &QCheckBox::toggled, this, [this](bool checked) {
    m_config.setUseMonitorLayout(checked);
    store();
  });
  connect(reload, &QPushButton::clicked, this, [this]() {
    reloadReported();
    store();
    update();
  });

  reloadFromConfig();
}

void MonitorLayoutWidget::reloadFromConfig()
{
  m_layout = m_config.monitorLayout();
  m_serverName = m_config.getServerName();
  m_enabled->setChecked(m_config.useMonitorLayout());
  ensureGridComputers();
  reloadReported();
  if (m_config.useExternalConfig()) {
    m_hint->setText(
        tr("An external server config is in use. Pointer switching follows section: layout in that file. "
           "This view edits the internal config, which is used when external config is turned off.")
    );
  }
  update();
}

LayoutMonitor MonitorLayoutWidget::defaultMonitor(const QString &name) const
{
  return {name, 0, 0, kPlaceholderWidth, kPlaceholderHeight};
}

void MonitorLayoutWidget::ensureGridComputers()
{
  QSet<QString> names;
  int right = 0;
  for (const auto &computer : m_layout) {
    names.insert(computer.name);
    for (const auto &monitor : computer.monitors) {
      right = std::max(right, computer.originX + monitor.x + monitor.width);
    }
  }

  auto append = [&](const QString &name) {
    if (name.isEmpty() || names.contains(name)) {
      return;
    }
    LayoutComputer placed;
    placed.name = name;
    placed.originX = names.isEmpty() ? 0 : right;
    placed.originY = 0;
    placed.monitors.append(defaultMonitor(name));
    right = placed.originX + kPlaceholderWidth;
    names.insert(name);
    if (name == m_serverName) {
      m_layout.prepend(placed);
    } else {
      m_layout.append(placed);
    }
  };

  append(m_serverName);
  for (const auto &computer : std::as_const(m_config).computers()) {
    if (!computer.isNull()) {
      append(computer.name());
    }
  }
}

QList<LayoutMonitor> MonitorLayoutWidget::parseMonitors(const QString &body) const
{
  QList<LayoutMonitor> monitors;
  const auto records = body.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  for (const auto &record : records) {
    const auto fields = record.split(QLatin1Char(','));
    if (fields.size() != 5) {
      continue;
    }
    bool xOk = false;
    bool yOk = false;
    bool wOk = false;
    bool hOk = false;
    LayoutMonitor monitor;
    monitor.name = fields.at(0);
    monitor.x = fields.at(1).toInt(&xOk);
    monitor.y = fields.at(2).toInt(&yOk);
    monitor.width = fields.at(3).toInt(&wOk);
    monitor.height = fields.at(4).toInt(&hOk);
    if (xOk && yOk && wOk && hOk && monitor.width > 0 && monitor.height > 0) {
      monitors.append(monitor);
    }
  }
  return monitors;
}

void MonitorLayoutWidget::reloadReported()
{
  m_reported.clear();
  for (auto &computer : m_layout) {
    const auto key = QStringLiteral("internalConfig/reportedGeometry/%1").arg(computer.name);
    const auto body = Settings::value(key).toString();
    const auto monitors = parseMonitors(body);
    if (!monitors.isEmpty()) {
      computer.monitors = monitors;
      m_reported.insert(computer.name);
    }
  }
}

void MonitorLayoutWidget::applyReportedGeometry(const QString &computerName, const QString &body)
{
  const auto monitors = parseMonitors(body);
  if (monitors.isEmpty()) {
    return;
  }
  Settings::setValue(QStringLiteral("internalConfig/reportedGeometry/%1").arg(computerName), body);
  for (auto &computer : m_layout) {
    if (computer.name.compare(computerName, Qt::CaseInsensitive) == 0) {
      computer.monitors = monitors;
      m_reported.insert(computer.name);
      if (m_config.useMonitorLayout()) {
        store();
      }
      update();
      return;
    }
  }

  int right = 0;
  for (const auto &computer : m_layout) {
    for (const auto &monitor : computer.monitors) {
      right = std::max(right, computer.originX + monitor.x + monitor.width);
    }
  }
  LayoutComputer placed;
  placed.name = computerName;
  placed.originX = m_layout.isEmpty() ? 0 : right;
  placed.originY = 0;
  placed.monitors = monitors;
  m_layout.append(placed);
  m_reported.insert(computerName);
  if (m_config.useMonitorLayout()) {
    store();
  }
  update();
}

void MonitorLayoutWidget::store()
{
  m_config.setMonitorLayout(m_layout);
  Q_EMIT layoutChanged();
}

MonitorLayoutWidget::Transform MonitorLayoutWidget::viewTransform() const
{
  QRect bounds;
  bool any = false;
  for (const auto &computer : m_layout) {
    for (const auto &monitor : computer.monitors) {
      const auto rect = monitorGlobal(computer, monitor);
      bounds = any ? bounds.united(rect) : rect;
      any = true;
    }
  }
  if (!any) {
    bounds = {0, 0, kPlaceholderWidth, kPlaceholderHeight};
  }
  bounds.adjust(-80, -80, 80, 80);

  const QRect area = rect().adjusted(12, 96, -12, -12);
  Transform transform;
  if (area.width() < 1 || area.height() < 1 || bounds.width() < 1 || bounds.height() < 1) {
    return transform;
  }
  transform.scale = std::min(
      area.width() / static_cast<double>(bounds.width()), area.height() / static_cast<double>(bounds.height())
  );
  transform.originX =
      area.left() + (area.width() - bounds.width() * transform.scale) / 2.0 - bounds.left() * transform.scale;
  transform.originY =
      area.top() + (area.height() - bounds.height() * transform.scale) / 2.0 - bounds.top() * transform.scale;
  return transform;
}

int MonitorLayoutWidget::computerAt(const QPointF &widgetPoint) const
{
  const auto transform = viewTransform();
  for (int index = m_layout.size() - 1; index >= 0; --index) {
    const auto &computer = m_layout.at(index);
    if (computer.name == m_serverName) {
      continue;
    }
    for (const auto &monitor : computer.monitors) {
      const auto global = monitorGlobal(computer, monitor);
      const QRectF painted(
          transform.originX + global.x() * transform.scale, transform.originY + global.y() * transform.scale,
          global.width() * transform.scale, global.height() * transform.scale
      );
      if (painted.contains(widgetPoint)) {
        return index;
      }
    }
  }
  return -1;
}

void MonitorLayoutWidget::snap(LayoutComputer &computer) const
{
  const auto transform = viewTransform();
  const int threshold =
      std::max(1, static_cast<int>(std::lround(kSnapScreenPixels / std::max(transform.scale, 0.0001))));
  int bestDx = 0;
  int bestDy = 0;
  int bestDistance = threshold + 1;
  bool found = false;

  for (const auto &monitor : computer.monitors) {
    const auto global = monitorGlobal(computer, monitor);
    for (const auto &other : m_layout) {
      if (other.name == computer.name) {
        continue;
      }
      for (const auto &otherMonitor : other.monitors) {
        const auto target = monitorGlobal(other, otherMonitor);
        const auto consider = [&](int dx, int dy, int overlap) {
          if (overlap <= 0) {
            return;
          }
          const int distance = std::abs(dx) + std::abs(dy);
          if (distance <= threshold && distance < bestDistance) {
            bestDistance = distance;
            bestDx = dx;
            bestDy = dy;
            found = true;
          }
        };
        const int globalRight = global.x() + global.width();
        const int globalBottom = global.y() + global.height();
        const int targetRight = target.x() + target.width();
        const int targetBottom = target.y() + target.height();
        const int verticalOverlap = std::min(globalBottom, targetBottom) - std::max(global.top(), target.top());
        const int horizontalOverlap = std::min(globalRight, targetRight) - std::max(global.left(), target.left());
        consider(target.left() - globalRight, 0, verticalOverlap);
        consider(targetRight - global.left(), 0, verticalOverlap);
        consider(0, target.top() - globalBottom, horizontalOverlap);
        consider(0, targetBottom - global.top(), horizontalOverlap);
      }
    }
  }

  if (found) {
    computer.originX += bestDx;
    computer.originY += bestDy;
  }
}

void MonitorLayoutWidget::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto transform = viewTransform();
  const QColor clientColors[] = {
      QColor(70, 130, 180),
      QColor(60, 140, 90),
      QColor(160, 110, 50),
      QColor(120, 90, 160),
  };

  int clientOrdinal = 0;
  for (const auto &computer : m_layout) {
    const bool server = computer.name == m_serverName;
    const QColor fill = server ? QColor(80, 80, 80) : clientColors[clientOrdinal++ % 4];
    const bool reported = m_reported.contains(computer.name);
    for (const auto &monitor : computer.monitors) {
      const auto global = monitorGlobal(computer, monitor);
      const QRect painted(
          static_cast<int>(std::lround(transform.originX + global.x() * transform.scale)),
          static_cast<int>(std::lround(transform.originY + global.y() * transform.scale)),
          std::max(1, static_cast<int>(std::lround(global.width() * transform.scale))),
          std::max(1, static_cast<int>(std::lround(global.height() * transform.scale)))
      );
      painter.fillRect(painted, fill);
      QPen pen(Qt::white);
      pen.setStyle(reported ? Qt::SolidLine : Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(painted.adjusted(0, 0, -1, -1));
      painter.setPen(Qt::white);
      painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, computer.name);
      painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignBottom, monitor.name);
    }
  }
}

void MonitorLayoutWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton) {
    return;
  }
  m_dragIndex = computerAt(event->position());
  m_dragLast = event->position();
}

void MonitorLayoutWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (m_dragIndex < 0 || m_dragIndex >= m_layout.size()) {
    return;
  }
  const auto transform = viewTransform();
  if (transform.scale <= 0.0) {
    return;
  }
  const auto delta = event->position() - m_dragLast;
  m_dragLast = event->position();
  auto &computer = m_layout[m_dragIndex];
  computer.originX += static_cast<int>(std::lround(delta.x() / transform.scale));
  computer.originY += static_cast<int>(std::lround(delta.y() / transform.scale));
  snap(computer);
  update();
}

void MonitorLayoutWidget::mouseReleaseEvent(QMouseEvent *)
{
  if (m_dragIndex >= 0) {
    store();
  }
  m_dragIndex = -1;
}
