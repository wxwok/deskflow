/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/MonitorLayoutWidget.h"

#include "common/Settings.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kSnapScreenPixels = 12;
constexpr int kPlaceholderWidth = 1920;
constexpr int kPlaceholderHeight = 1080;
constexpr int kOriginLimit = 20000000;

QRect monitorGlobal(const LayoutComputer &computer, const LayoutMonitor &monitor)
{
  return {computer.originX + monitor.x, computer.originY + monitor.y, monitor.width, monitor.height};
}

} // namespace

MonitorLayoutWidget::MonitorLayoutWidget(ServerConfig &config, QWidget *parent) : QWidget(parent), m_config(config)
{
  auto *layout = new QVBoxLayout(this);
  m_controls = new QWidget(this);
  auto *controls = new QVBoxLayout(m_controls);
  controls->setContentsMargins(0, 0, 0, 0);
  m_enabled = new QCheckBox(tr("Use monitor layout for pointer switching"), m_controls);
  m_hint = new QLabel(
      tr("Drag a computer so its monitors touch another computer. Edges within about 12 px snap together "
         "and line up; hold Alt or Shift to turn snapping off. Arrow keys nudge by 1 px, or by 10 px with Shift; "
         "hold Alt to nudge without snapping. The server stays put. Orange lines are the edges that will switch. "
         "Leave this off to keep the grid links."),
      m_controls
  );
  m_hint->setWordWrap(true);
  auto *reload = new QPushButton(tr("Reload reported geometry"), m_controls);
  auto *originRow = new QHBoxLayout();
  m_originLabel = new QLabel(tr("No computer selected"), m_controls);
  m_originX = new QSpinBox(m_controls);
  m_originY = new QSpinBox(m_controls);
  m_originX->setRange(-kOriginLimit, kOriginLimit);
  m_originY->setRange(-kOriginLimit, kOriginLimit);
  m_originX->setKeyboardTracking(false);
  m_originY->setKeyboardTracking(false);
  m_originX->setPrefix(QStringLiteral("X "));
  m_originY->setPrefix(QStringLiteral("Y "));
  m_originX->setToolTip(tr("Exact layout origin of the selected computer, in pixels."));
  m_originY->setToolTip(m_originX->toolTip());
  m_originX->installEventFilter(this);
  m_originY->installEventFilter(this);
  originRow->addWidget(reload);
  originRow->addWidget(m_originLabel);
  originRow->addWidget(m_originX);
  originRow->addWidget(m_originY);
  originRow->addStretch(1);
  controls->addWidget(m_enabled);
  controls->addWidget(m_hint);
  controls->addLayout(originRow);
  layout->addWidget(m_controls);
  layout->addStretch(1);
  setMinimumSize(640, 480);
  setFocusPolicy(Qt::StrongFocus);

  connect(m_enabled, &QCheckBox::toggled, this, [this](bool checked) {
    m_config.setUseMonitorLayout(checked);
    store();
  });
  connect(reload, &QPushButton::clicked, this, [this]() {
    reloadReported();
    store();
    update();
  });
  const auto originEdited = [this](int) {
    if (m_updatingOrigin || m_selected < 0) {
      return;
    }
    moveComputer(m_selected, m_originX->value(), m_originY->value(), false);
    store();
    update();
  };
  connect(m_originX, &QSpinBox::valueChanged, this, originEdited);
  connect(m_originY, &QSpinBox::valueChanged, this, originEdited);

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
  if (m_selected >= m_layout.size()) {
    m_selected = -1;
  }
  syncOriginFields();
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

  int top = m_controls == nullptr ? 96 : m_controls->geometry().bottom();
  if (m_controls != nullptr) {
    top = std::max(top, m_controls->sizeHint().height());
  }
  const QRect area = rect().adjusted(12, top + 8, -12, -12);
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

int MonitorLayoutWidget::computerAt(const QPointF &widgetPoint, bool includeServer) const
{
  const auto transform = viewTransform();
  for (int index = m_layout.size() - 1; index >= 0; --index) {
    const auto &computer = m_layout.at(index);
    if (!includeServer && computer.name == m_serverName) {
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

int MonitorLayoutWidget::snapThreshold(const Transform &transform) const
{
  return std::max(1, static_cast<int>(std::lround(kSnapScreenPixels / std::max(transform.scale, 0.0001))));
}

std::vector<deskflow::SnapRect> MonitorLayoutWidget::rectsFor(int index, int originX, int originY) const
{
  std::vector<deskflow::SnapRect> rects;
  if (index < 0 || index >= m_layout.size()) {
    return rects;
  }
  const auto &computer = m_layout.at(index);
  rects.reserve(static_cast<size_t>(computer.monitors.size()));
  for (const auto &monitor : computer.monitors) {
    rects.push_back({originX + monitor.x, originY + monitor.y, monitor.width, monitor.height, index});
  }
  return rects;
}

void MonitorLayoutWidget::moveComputer(int index, int originX, int originY, bool enableSnap)
{
  if (index < 0 || index >= m_layout.size() || m_layout.at(index).name == m_serverName) {
    return;
  }
  std::vector<deskflow::SnapRect> stationary;
  for (int other = 0; other < m_layout.size(); ++other) {
    if (other == index) {
      continue;
    }
    const auto part = rectsFor(other, m_layout.at(other).originX, m_layout.at(other).originY);
    stationary.insert(stationary.end(), part.begin(), part.end());
  }
  const auto result = deskflow::snapMonitorPlacement(
      rectsFor(index, originX, originY), stationary, snapThreshold(viewTransform()), enableSnap
  );
  auto &computer = m_layout[index];
  computer.originX = originX + result.dx;
  computer.originY = originY + result.dy;
  m_guides = m_dragIndex == index ? result.guides : std::vector<deskflow::SnapGuide>{};
  syncOriginFields();
}

void MonitorLayoutWidget::syncOriginFields()
{
  const bool selected = m_selected >= 0 && m_selected < m_layout.size();
  const bool editable = selected && m_layout.at(m_selected).name != m_serverName;
  m_originX->setEnabled(editable);
  m_originY->setEnabled(editable);
  if (!selected) {
    m_originLabel->setText(tr("No computer selected"));
    return;
  }
  const auto &computer = m_layout.at(m_selected);
  m_originLabel->setText(computer.name);
  m_updatingOrigin = true;
  m_originX->setValue(computer.originX);
  m_originY->setValue(computer.originY);
  m_updatingOrigin = false;
}

void MonitorLayoutWidget::nudge(int dx, int dy, bool enableSnap)
{
  if (m_selected < 0 || m_selected >= m_layout.size()) {
    return;
  }
  const auto &computer = m_layout.at(m_selected);
  moveComputer(m_selected, computer.originX + dx, computer.originY + dy, enableSnap);
  store();
  update();
}

QPointF MonitorLayoutWidget::mapLayout(const Transform &transform, int32_t x, int32_t y) const
{
  return {transform.originX + x * transform.scale, transform.originY + y * transform.scale};
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
  for (int index = 0; index < m_layout.size(); ++index) {
    const auto &computer = m_layout.at(index);
    const bool server = computer.name == m_serverName;
    const QColor fill = server ? QColor(80, 80, 80) : clientColors[clientOrdinal++ % 4];
    const bool reported = m_reported.contains(computer.name);
    const bool selected = index == m_selected;
    for (const auto &monitor : computer.monitors) {
      const auto global = monitorGlobal(computer, monitor);
      const QRect painted(
          static_cast<int>(std::lround(transform.originX + global.x() * transform.scale)),
          static_cast<int>(std::lround(transform.originY + global.y() * transform.scale)),
          std::max(1, static_cast<int>(std::lround(global.width() * transform.scale))),
          std::max(1, static_cast<int>(std::lround(global.height() * transform.scale)))
      );
      painter.fillRect(painted, fill);
      QPen pen(selected ? QColor(255, 220, 120) : QColor(Qt::white));
      pen.setWidth(selected ? 2 : 1);
      pen.setStyle(reported ? Qt::SolidLine : Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(painted.adjusted(0, 0, -1, -1));
      painter.setPen(Qt::white);
      painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, computer.name);
      painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignBottom, monitor.name);
    }
  }

  if (m_dragIndex >= 0) {
    QPen guidePen(QColor(80, 200, 255));
    guidePen.setStyle(Qt::DashLine);
    guidePen.setWidth(1);
    painter.setPen(guidePen);
    const QRect canvas = rect();
    for (const auto &guide : m_guides) {
      const QPointF start = mapLayout(
          transform, guide.vertical ? guide.position : guide.begin, guide.vertical ? guide.begin : guide.position
      );
      const QPointF end = mapLayout(
          transform, guide.vertical ? guide.position : guide.end, guide.vertical ? guide.end : guide.position
      );
      if (guide.vertical) {
        painter.drawLine(QPointF(start.x(), canvas.top()), QPointF(start.x(), canvas.bottom()));
      } else {
        painter.drawLine(QPointF(canvas.left(), start.y()), QPointF(canvas.right(), start.y()));
      }
      painter.drawLine(start, end);
    }
  }

  std::vector<deskflow::SnapRect> placed;
  for (int index = 0; index < m_layout.size(); ++index) {
    const auto part = rectsFor(index, m_layout.at(index).originX, m_layout.at(index).originY);
    placed.insert(placed.end(), part.begin(), part.end());
  }
  QPen switchPen(QColor(255, 160, 0));
  switchPen.setWidth(3);
  painter.setPen(switchPen);
  for (const auto &segment : deskflow::switchEdgeSegments(placed)) {
    const QPointF start = mapLayout(
        transform, segment.vertical ? segment.position : segment.begin,
        segment.vertical ? segment.begin : segment.position
    );
    const QPointF end = mapLayout(
        transform, segment.vertical ? segment.position : segment.end, segment.vertical ? segment.end : segment.position
    );
    painter.drawLine(start, end);
  }
}

void MonitorLayoutWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  m_selected = computerAt(event->position(), true);
  m_dragIndex = computerAt(event->position(), false);
  m_dragLast = event->position();
  m_guides.clear();
  if (m_dragIndex >= 0) {
    m_freeX = m_layout.at(m_dragIndex).originX;
    m_freeY = m_layout.at(m_dragIndex).originY;
  }
  syncOriginFields();
  update();
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
  m_freeX += delta.x() / transform.scale;
  m_freeY += delta.y() / transform.scale;
  const bool enableSnap = (event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier)) == 0;
  moveComputer(m_dragIndex, static_cast<int>(std::lround(m_freeX)), static_cast<int>(std::lround(m_freeY)), enableSnap);
  update();
}

void MonitorLayoutWidget::mouseReleaseEvent(QMouseEvent *)
{
  if (m_dragIndex >= 0) {
    store();
  }
  m_dragIndex = -1;
  m_guides.clear();
  update();
}

void MonitorLayoutWidget::keyPressEvent(QKeyEvent *event)
{
  int dx = 0;
  int dy = 0;
  switch (event->key()) {
  case Qt::Key_Left:
    dx = -1;
    break;
  case Qt::Key_Right:
    dx = 1;
    break;
  case Qt::Key_Up:
    dy = -1;
    break;
  case Qt::Key_Down:
    dy = 1;
    break;
  default:
    QWidget::keyPressEvent(event);
    return;
  }
  const int step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10 : 1;
  const bool enableSnap = (event->modifiers() & Qt::AltModifier) == 0;
  nudge(dx * step, dy * step, enableSnap);
}

bool MonitorLayoutWidget::eventFilter(QObject *watched, QEvent *event)
{
  if ((watched == m_originX || watched == m_originY) && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    switch (key->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
      keyPressEvent(key);
      return true;
    default:
      break;
    }
  }
  return QWidget::eventFilter(watched, event);
}
