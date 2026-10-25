/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/MonitorLayoutWidget.h"

#include "common/Settings.h"
#include "gui/config/LayoutClients.h"

#include <QCheckBox>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
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

bool areasOverlap(const QRect &a, const QRect &b)
{
  return a.x() < b.x() + b.width() && b.x() < a.x() + a.width() && a.y() < b.y() + b.height() &&
         b.y() < a.y() + a.height();
}

bool areaContains(const QRect &outer, const QRect &inner)
{
  return inner.x() >= outer.x() && inner.y() >= outer.y() && inner.x() + inner.width() <= outer.x() + outer.width() &&
         inner.y() + inner.height() <= outer.y() + outer.height();
}

bool computersOverlap(const LayoutComputer &a, const LayoutComputer &b)
{
  for (const auto &left : a.monitors) {
    for (const auto &right : b.monitors) {
      if (areasOverlap(monitorGlobal(a, left), monitorGlobal(b, right))) {
        return true;
      }
    }
  }
  return false;
}

const LayoutMonitor *largestMonitor(const LayoutComputer &computer)
{
  const LayoutMonitor *best = nullptr;
  int bestArea = -1;
  for (const auto &monitor : computer.monitors) {
    const int area = monitor.width * monitor.height;
    if (best == nullptr || area > bestArea) {
      best = &monitor;
      bestArea = area;
    }
  }
  return best;
}

} // namespace

MonitorLayoutWidget::MonitorLayoutWidget(ServerConfig &config, QWidget *parent) : QWidget(parent), m_config(config)
{
  auto *layout = new QVBoxLayout(this);
  m_controls = new QWidget(this);
  auto *controls = new QVBoxLayout(m_controls);
  controls->setContentsMargins(0, 0, 0, 0);
  m_enabled = new QCheckBox(tr("Use monitor layout for pointer switching"), m_controls);
  m_hint = new QLabel(m_controls);
  m_hint->setWordWrap(true);
  m_discoveryLabel = new QLabel(tr("Clients not in the layout"), m_controls);
  m_discoveryHost = new QWidget(m_controls);
  m_discoveryLayout = new QVBoxLayout(m_discoveryHost);
  m_discoveryLayout->setContentsMargins(0, 0, 0, 0);
  m_status = new QLabel(m_controls);
  m_status->setWordWrap(true);
  auto *reload = new QPushButton(tr("Reload reported geometry"), m_controls);
  m_participation = new QPushButton(tr("Disable"), m_controls);
  m_stackFront = new QPushButton(tr("Bring to front"), m_controls);
  m_stackUp = new QPushButton(tr("Move up one"), m_controls);
  m_stackDown = new QPushButton(tr("Move down one"), m_controls);
  m_stackBack = new QPushButton(tr("Send to back"), m_controls);
  m_stackFront->setObjectName(QStringLiteral("stack-front"));
  m_stackUp->setObjectName(QStringLiteral("stack-up"));
  m_stackDown->setObjectName(QStringLiteral("stack-down"));
  m_stackBack->setObjectName(QStringLiteral("stack-back"));
  const auto stackTip =
      tr("Reorder computers that overlap. The front screen receives the pointer; screens underneath are paused there, "
         "not disabled. These stay off when the selection does not overlap another computer.");
  m_stackFront->setToolTip(stackTip);
  m_stackUp->setToolTip(stackTip);
  m_stackDown->setToolTip(stackTip);
  m_stackBack->setToolTip(stackTip);
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
  originRow->addWidget(m_participation);
  originRow->addWidget(m_stackFront);
  originRow->addWidget(m_stackUp);
  originRow->addWidget(m_stackDown);
  originRow->addWidget(m_stackBack);
  originRow->addStretch(1);
  controls->addWidget(m_enabled);
  controls->addWidget(m_hint);
  controls->addWidget(m_discoveryLabel);
  controls->addWidget(m_discoveryHost);
  controls->addWidget(m_status);
  controls->addLayout(originRow);
  layout->addWidget(m_controls);
  layout->addStretch(1);
  setMinimumSize(640, 480);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  connect(m_enabled, &QCheckBox::toggled, this, [this](bool checked) {
    if (m_reloading) {
      return;
    }
    m_config.setUseMonitorLayout(checked);
    store();
  });
  connect(m_participation, &QPushButton::clicked, this, &MonitorLayoutWidget::toggleParticipation);
  connect(m_stackFront, &QPushButton::clicked, this, [this]() { restack(StackMove::Front); });
  connect(m_stackUp, &QPushButton::clicked, this, [this]() { restack(StackMove::Up); });
  connect(m_stackDown, &QPushButton::clicked, this, [this]() { restack(StackMove::Down); });
  connect(m_stackBack, &QPushButton::clicked, this, [this]() { restack(StackMove::Back); });
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

bool MonitorLayoutWidget::isExternal() const
{
  return m_config.useExternalConfig();
}

void MonitorLayoutWidget::updateHint()
{
  const auto drag =
      tr("Drag a computer so its monitors touch or overlap another computer. Edges within about 12 px snap "
         "together and line up; hold Alt or Shift to turn snapping off. A computer you drop on top of another "
         "comes to the front. Arrow keys nudge by 1 px, or by 10 px with Shift; hold Alt to nudge without snapping. "
         "The server stays put. Orange lines are the edges that will switch. Where screens overlap, only the front "
         "one receives the pointer; the ones underneath are paused there and stay in the layout. Leave this off to "
         "keep the grid links.");
  if (!isExternal()) {
    m_hint->setText(drag);
    m_enabled->setEnabled(true);
    m_enabled->setToolTip(QString());
    return;
  }
  m_hint->setText(
      tr("An external server config is in use. Pointer switching follows section: layout in that file. "
         "Add writes that section after saving a .bak backup, and registers the computer in the general "
         "computer table. This view does not change the internal layout.") +
      QLatin1Char('\n') + drag
  );
  m_enabled->setEnabled(false);
  m_enabled->setToolTip(
      tr("Pointer switching follows section: layout in the external server config, not this checkbox.")
  );
}

QString MonitorLayoutWidget::participationError(deskflow::LayoutWriteError error) const
{
  switch (error) {
  case deskflow::LayoutWriteError::InvalidName:
    return tr("The computer name is not valid.");
  case deskflow::LayoutWriteError::AlreadyPlaced:
    return tr("That computer is already in the layout.");
  case deskflow::LayoutWriteError::ReadFailed:
    return tr("Could not read the server config file.");
  case deskflow::LayoutWriteError::BackupFailed:
    return tr("Could not back up the server config file.");
  case deskflow::LayoutWriteError::WriteFailed:
    return tr("Could not write the server config file.");
  case deskflow::LayoutWriteError::None:
    break;
  }
  return {};
}

void MonitorLayoutWidget::reloadFromConfig()
{
  m_reloading = true;
  m_serverName = m_config.getServerName();
  m_enabled->setChecked(m_config.useMonitorLayout());
  m_status->clear();
  if (isExternal()) {
    QFile file(m_config.configFile());
    if (!file.open(QIODevice::ReadOnly)) {
      m_layout.clear();
      m_status->setText(participationError(deskflow::LayoutWriteError::ReadFailed));
    } else {
      m_layout = deskflow::parseLayoutSection(QString::fromUtf8(file.readAll()));
    }
  } else {
    m_layout = m_config.monitorLayout();
    ensureGridComputers();
  }
  reloadReported();
  updateHint();
  if (m_selected >= m_layout.size()) {
    m_selected = -1;
  }
  syncOriginFields();
  refreshDiscovery();
  refreshParticipation();
  m_committedLayout = m_layout;
  m_pendingLayoutWrite = false;
  m_reloading = false;
  update();
}

void MonitorLayoutWidget::reloadDiscovery()
{
  refreshDiscovery();
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
  const auto key = QStringLiteral("internalConfig/reportedGeometry/%1").arg(computerName);
  Settings::setValue(key, body);
  Settings::proxy().setValue(key, body);
  for (auto &computer : m_layout) {
    if (computer.name.compare(computerName, Qt::CaseInsensitive) == 0) {
      computer.monitors = monitors;
      m_reported.insert(computer.name);
      if (isExternal() || m_config.useMonitorLayout()) {
        store();
      }
      update();
      return;
    }
  }

  // A new client stays in the discovery list until Add places it.
  refreshDiscovery();
  update();
}

void MonitorLayoutWidget::store()
{
  if (m_reloading) {
    return;
  }
  if (isExternal()) {
    // The file stays untouched until Save. Cancel therefore drops the move.
    m_pendingLayoutWrite = m_layout != m_committedLayout;
    Q_EMIT layoutChanged();
    return;
  }
  m_config.setMonitorLayout(m_layout);
  Q_EMIT layoutChanged();
}

bool MonitorLayoutWidget::commitPendingLayout()
{
  if (!isExternal() || !m_pendingLayoutWrite) {
    return true;
  }
  const auto written = deskflow::writeLayoutFile(m_config.configFile(), m_layout);
  if (!written.ok) {
    m_status->setText(participationError(written.error));
    return false;
  }
  m_committedLayout = m_layout;
  m_pendingLayoutWrite = false;
  m_status->clear();
  return true;
}

QPoint MonitorLayoutWidget::placedComputerCenter(const QString &name) const
{
  const auto transform = viewTransform();
  for (const auto &computer : m_layout) {
    if (computer.name != name || computer.monitors.isEmpty()) {
      continue;
    }
    const auto global = monitorGlobal(computer, computer.monitors.first());
    return QPoint(
        static_cast<int>(std::lround(transform.originX + (global.x() + global.width() / 2.0) * transform.scale)),
        static_cast<int>(std::lround(transform.originY + (global.y() + global.height() / 2.0) * transform.scale))
    );
  }
  return {};
}

void MonitorLayoutWidget::refreshDiscovery()
{
  while (QLayoutItem *item = m_discoveryLayout->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  QStringList placed;
  for (const auto &computer : m_layout) {
    placed.append(computer.name);
  }
  const auto waiting =
      deskflow::unplacedClients(deskflow::connectedComputerNames(), deskflow::reportedComputerNames(), placed);
  if (waiting.isEmpty()) {
    auto *empty = new QLabel(tr("No clients are waiting to be added."), m_discoveryHost);
    empty->setWordWrap(true);
    m_discoveryLayout->addWidget(empty);
    return;
  }

  for (const auto &client : waiting) {
    auto *row = new QWidget(m_discoveryHost);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    QString detail;
    if (client.connected && client.reported) {
      detail = tr("Connected, reported geometry");
    } else if (client.connected) {
      detail = tr("Connected");
    } else {
      detail = tr("Reported geometry");
    }
    rowLayout->addWidget(new QLabel(QStringLiteral("%1 - %2").arg(client.name, detail), row));
    auto *add = new QPushButton(tr("Add"), row);
    add->setObjectName(QStringLiteral("add-%1").arg(client.name));
    rowLayout->addWidget(add);
    rowLayout->addStretch(1);
    const auto name = client.name;
    connect(add, &QPushButton::clicked, this, [this, name]() { addDiscovered(name); }, Qt::QueuedConnection);
    m_discoveryLayout->addWidget(row);
  }
}

void MonitorLayoutWidget::refreshParticipation()
{
  const bool selected = m_selected >= 0 && m_selected < m_layout.size();
  const bool server = selected && m_layout.at(m_selected).name.compare(m_serverName, Qt::CaseInsensitive) == 0;
  m_participation->setEnabled(selected && !server);
  if (!selected || server) {
    m_participation->setText(tr("Disable"));
    m_participation->setToolTip(
        tr("Disable the selected computer. Its position is kept, and the pointer will not switch to it.")
    );
  } else {
    const bool disabled = deskflow::isComputerDisabled(m_layout.at(m_selected).name);
    m_participation->setText(disabled ? tr("Enable") : tr("Disable"));
    m_participation->setToolTip(
        disabled ? tr("Enable the selected computer and restore pointer switching.")
                 : tr("Disable the selected computer. Its position is kept, and the pointer will not switch to it.")
    );
  }
  updateStackButtons();
}

void MonitorLayoutWidget::addDiscovered(const QString &name)
{
  const auto monitors = deskflow::reportedMonitors(name);
  const auto result = deskflow::addComputerToActiveLayout(
      isExternal(), m_config.configFile(), isExternal() ? nullptr : &m_config, m_layout, name, monitors
  );
  if (!result.ok) {
    m_status->setText(participationError(result.error));
    return;
  }
  m_layout = result.layout;
  if (!monitors.isEmpty()) {
    m_reported.insert(result.layout.isEmpty() ? name : result.layout.last().name);
  }
  m_status->clear();
  if (isExternal()) {
    // Add already wrote this layout, including any move that was still pending.
    m_committedLayout = m_layout;
    m_pendingLayoutWrite = false;
    Q_EMIT layoutChanged();
  } else {
    Q_EMIT authoritativeLayoutChanged();
    Q_EMIT layoutChanged();
  }
  refreshDiscovery();
  refreshParticipation();
  update();
}

void MonitorLayoutWidget::toggleParticipation()
{
  if (m_selected < 0 || m_selected >= m_layout.size()) {
    return;
  }
  const auto &computer = m_layout.at(m_selected);
  if (computer.name.compare(m_serverName, Qt::CaseInsensitive) == 0) {
    return;
  }
  deskflow::setComputerDisabled(computer.name, !deskflow::isComputerDisabled(computer.name));
  refreshParticipation();
  update();
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
  const auto skip = [&](int index) { return !includeServer && m_layout.at(index).name == m_serverName; };
  if (m_fanGroup >= 0) {
    const auto order = stackOrder(overlapGroup(m_fanGroup));
    for (const int index : order) {
      if (skip(index)) {
        continue;
      }
      if (fanCardRect(index).contains(widgetPoint.toPoint())) {
        return index;
      }
    }
  } else {
    for (auto chip = m_nameChips.crbegin(); chip != m_nameChips.crend(); ++chip) {
      if (chip->rect.contains(widgetPoint.toPoint()) && !skip(chip->index)) {
        return chip->index;
      }
    }
  }

  const auto transform = viewTransform();
  const auto order = paintOrder();
  const auto fanned = m_fanGroup >= 0 ? overlapGroup(m_fanGroup) : QList<int>{};
  for (int place = order.size() - 1; place >= 0; --place) {
    const int index = order.at(place);
    if (skip(index) || fanned.contains(index)) {
      continue;
    }
    const auto &computer = m_layout.at(index);
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
  raiseIfOverlapping(index);
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
    refreshParticipation();
    return;
  }
  const auto &computer = m_layout.at(m_selected);
  m_originLabel->setText(computer.name);
  m_updatingOrigin = true;
  m_originX->setValue(computer.originX);
  m_originY->setValue(computer.originY);
  m_updatingOrigin = false;
  refreshParticipation();
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
  const auto order = paintOrder();
  for (const int index : order) {
    const auto &computer = m_layout.at(index);
    const bool server = computer.name == m_serverName;
    const bool disabled = deskflow::isComputerDisabled(computer.name);
    const auto group = overlapGroup(index);
    const bool stacked = group.size() > 1;
    const bool top = stacked && stackOrder(group).constFirst() == index;
    QColor fill = disabled ? QColor(90, 90, 90) : (server ? QColor(80, 80, 80) : clientColors[clientOrdinal++ % 4]);
    if (stacked && !top && !disabled) {
      fill.setAlpha(150);
    }
    const bool reported = m_reported.contains(computer.name);
    const bool selected = index == m_selected;
    const bool fanned = m_fanGroup >= 0 && overlapGroup(m_fanGroup).contains(index);
    if (fanned) {
      continue;
    }
    const bool showOwnName = !stacked || !fullyCovered(index);
    const auto *primary = largestMonitor(computer);
    for (const auto &monitor : computer.monitors) {
      const auto global = monitorGlobal(computer, monitor);
      const QRect painted(
          static_cast<int>(std::lround(transform.originX + global.x() * transform.scale)),
          static_cast<int>(std::lround(transform.originY + global.y() * transform.scale)),
          std::max(1, static_cast<int>(std::lround(global.width() * transform.scale))),
          std::max(1, static_cast<int>(std::lround(global.height() * transform.scale)))
      );
      const bool hostsRoster = stacked && top && primary == &monitor;
      if (top) {
        painter.fillRect(painted.translated(4, 4), QColor(0, 0, 0, 50));
      }
      painter.fillRect(painted, fill);
      QPen pen(selected ? QColor(255, 220, 120) : (top ? QColor(210, 225, 255) : QColor(Qt::white)));
      pen.setWidth(selected || top ? 2 : 1);
      pen.setStyle(reported ? Qt::SolidLine : Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(painted.adjusted(0, 0, -1, -1));
      painter.setPen(Qt::white);
      if (hostsRoster && !disabled) {
        painter.save();
        QFont mark = painter.font();
        mark.setBold(true);
        mark.setPixelSize(std::min(64, std::max(22, painted.height() / 6)));
        painter.setFont(mark);
        painter.setPen(QColor(255, 255, 255, 235));
        painter.drawText(painted.adjusted(16, 12, -16, -(painted.height() / 3)), Qt::AlignCenter, computer.name);
        painter.restore();
      } else if (showOwnName) {
        painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, computer.name);
      }
      if (!hostsRoster) {
        painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignBottom, monitor.name);
      }
      if (disabled) {
        painter.drawText(painted.adjusted(4, 4, -4, -4), Qt::AlignCenter, tr("Disabled"));
      }
    }
  }
  paintStackNames(&painter, transform);

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

  if (m_fanGroup >= 0) {
    const auto fanned = stackOrder(overlapGroup(m_fanGroup));
    for (int rank = fanned.size() - 1; rank >= 0; --rank) {
      paintFanCard(&painter, fanned.at(rank));
    }
  }
}

void MonitorLayoutWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::RightButton) {
    setFocus(Qt::MouseFocusReason);
    m_selected = computerAt(event->position(), true);
    m_dragIndex = -1;
    syncOriginFields();
    update();
    showStackMenu(event->globalPosition().toPoint());
    return;
  }
  if (event->button() != Qt::LeftButton) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  m_selected = computerAt(event->position(), true);
  m_dragIndex = computerAt(event->position(), false);
  m_dragLast = event->position();
  m_guides.clear();
  if (m_dragIndex >= 0) {
    m_fanGroup = -1;
    m_freeX = m_layout.at(m_dragIndex).originX;
    m_freeY = m_layout.at(m_dragIndex).originY;
  }
  syncOriginFields();
  update();
}

void MonitorLayoutWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (m_dragIndex < 0 || m_dragIndex >= m_layout.size()) {
    updateFan(event->position());
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

void MonitorLayoutWidget::leaveEvent(QEvent *event)
{
  QWidget::leaveEvent(event);
  if (m_fanGroup >= 0) {
    m_fanGroup = -1;
    update();
  }
}

QList<int> MonitorLayoutWidget::overlapGroup(int index) const
{
  QList<int> group;
  if (index < 0 || index >= m_layout.size()) {
    return group;
  }
  QList<int> pending{index};
  while (!pending.isEmpty()) {
    const int current = pending.takeLast();
    if (group.contains(current)) {
      continue;
    }
    group.append(current);
    for (int other = 0; other < m_layout.size(); ++other) {
      if (group.contains(other) || pending.contains(other)) {
        continue;
      }
      if (computersOverlap(m_layout.at(current), m_layout.at(other))) {
        pending.append(other);
      }
    }
  }
  return group;
}

QList<int> MonitorLayoutWidget::stackOrder(const QList<int> &group) const
{
  QList<int> order = group;
  std::sort(order.begin(), order.end(), [&](int left, int right) {
    if (m_layout.at(left).z != m_layout.at(right).z) {
      return m_layout.at(left).z > m_layout.at(right).z;
    }
    return left > right;
  });
  return order;
}

QList<int> MonitorLayoutWidget::paintOrder() const
{
  QList<int> order;
  order.reserve(m_layout.size());
  for (int index = 0; index < m_layout.size(); ++index) {
    order.append(index);
  }
  std::sort(order.begin(), order.end(), [&](int left, int right) {
    if (m_layout.at(left).z != m_layout.at(right).z) {
      return m_layout.at(left).z < m_layout.at(right).z;
    }
    return left < right;
  });
  return order;
}

bool MonitorLayoutWidget::fullyCovered(int index) const
{
  if (index < 0 || index >= m_layout.size()) {
    return false;
  }
  const auto &computer = m_layout.at(index);
  if (computer.monitors.isEmpty()) {
    return false;
  }
  for (const auto &monitor : computer.monitors) {
    const auto inner = monitorGlobal(computer, monitor);
    bool contained = false;
    for (int other = 0; other < m_layout.size(); ++other) {
      if (other == index) {
        continue;
      }
      const bool above = m_layout.at(other).z > computer.z || (m_layout.at(other).z == computer.z && other > index);
      if (!above) {
        continue;
      }
      for (const auto &cover : m_layout.at(other).monitors) {
        if (areaContains(monitorGlobal(m_layout.at(other), cover), inner)) {
          contained = true;
          break;
        }
      }
      if (contained) {
        break;
      }
    }
    if (!contained) {
      return false;
    }
  }
  return true;
}

void MonitorLayoutWidget::raiseIfOverlapping(int index)
{
  if (index < 0 || index >= m_layout.size()) {
    return;
  }
  int maxBelow = std::numeric_limits<int>::min();
  bool overlaps = false;
  for (int other = 0; other < m_layout.size(); ++other) {
    if (other == index) {
      continue;
    }
    if (!computersOverlap(m_layout.at(index), m_layout.at(other))) {
      continue;
    }
    overlaps = true;
    maxBelow = std::max(maxBelow, m_layout.at(other).z);
  }
  if (overlaps && m_layout.at(index).z <= maxBelow && maxBelow < std::numeric_limits<int>::max()) {
    m_layout[index].z = maxBelow + 1;
  }
}

void MonitorLayoutWidget::restack(StackMove move)
{
  if (m_selected < 0 || m_selected >= m_layout.size()) {
    return;
  }
  const auto group = overlapGroup(m_selected);
  if (group.size() < 2) {
    return;
  }
  QList<int> bottomFirst = group;
  std::sort(bottomFirst.begin(), bottomFirst.end(), [&](int left, int right) {
    if (m_layout.at(left).z != m_layout.at(right).z) {
      return m_layout.at(left).z < m_layout.at(right).z;
    }
    return left < right;
  });
  const int pos = bottomFirst.indexOf(m_selected);
  if (pos < 0) {
    return;
  }
  switch (move) {
  case StackMove::Front:
    bottomFirst.removeAt(pos);
    bottomFirst.append(m_selected);
    break;
  case StackMove::Back:
    bottomFirst.removeAt(pos);
    bottomFirst.prepend(m_selected);
    break;
  case StackMove::Up:
    if (pos + 1 >= bottomFirst.size()) {
      return;
    }
    bottomFirst.swapItemsAt(pos, pos + 1);
    break;
  case StackMove::Down:
    if (pos == 0) {
      return;
    }
    bottomFirst.swapItemsAt(pos, pos - 1);
    break;
  }
  for (int place = 0; place < bottomFirst.size(); ++place) {
    m_layout[bottomFirst.at(place)].z = place;
  }
  store();
  updateStackButtons();
  update();
}

void MonitorLayoutWidget::updateStackButtons()
{
  if (m_stackFront == nullptr) {
    return;
  }
  bool front = false;
  bool up = false;
  bool down = false;
  bool back = false;
  if (m_selected >= 0 && m_selected < m_layout.size()) {
    const auto group = overlapGroup(m_selected);
    if (group.size() > 1) {
      const auto order = stackOrder(group);
      const int pos = order.indexOf(m_selected);
      front = pos > 0;
      up = pos > 0;
      down = pos >= 0 && pos + 1 < order.size();
      back = down;
    }
  }
  m_stackFront->setEnabled(front);
  m_stackUp->setEnabled(up);
  m_stackDown->setEnabled(down);
  m_stackBack->setEnabled(back);
}

void MonitorLayoutWidget::showStackMenu(const QPoint &globalPos)
{
  QMenu menu(this);
  auto *front = menu.addAction(tr("Bring to front"));
  auto *up = menu.addAction(tr("Move up one"));
  auto *down = menu.addAction(tr("Move down one"));
  auto *back = menu.addAction(tr("Send to back"));
  front->setEnabled(m_stackFront->isEnabled());
  up->setEnabled(m_stackUp->isEnabled());
  down->setEnabled(m_stackDown->isEnabled());
  back->setEnabled(m_stackBack->isEnabled());
  const auto *chosen = menu.exec(globalPos);
  if (chosen == front) {
    restack(StackMove::Front);
  } else if (chosen == up) {
    restack(StackMove::Up);
  } else if (chosen == down) {
    restack(StackMove::Down);
  } else if (chosen == back) {
    restack(StackMove::Back);
  }
}

void MonitorLayoutWidget::updateFan(const QPointF &widgetPoint)
{
  int next = -1;
  const int hovered = computerAt(widgetPoint, true);
  if (hovered >= 0) {
    const auto group = overlapGroup(hovered);
    for (const int member : group) {
      if (fullyCovered(member)) {
        next = stackOrder(group).constFirst();
        break;
      }
    }
  }
  if (next == m_fanGroup) {
    return;
  }
  m_fanCursor = widgetPoint.toPoint();
  m_fanGroup = next;
  update();
}

QRect MonitorLayoutWidget::fanCardRect(int index) const
{
  if (m_fanGroup < 0 || index < 0 || index >= m_layout.size()) {
    return {};
  }
  const auto group = overlapGroup(m_fanGroup);
  if (!group.contains(index)) {
    return {};
  }
  const auto order = stackOrder(group);
  const int rank = order.indexOf(index);
  const int count = order.size();
  if (rank < 0 || count < 2) {
    return {};
  }

  const int cardW = 232;
  const int cardH = 148;
  int minX = 8;
  int minY = 8;
  if (m_controls != nullptr) {
    minY = std::max(minY, m_controls->geometry().bottom() + 6);
  }
  const int availW = std::max(cardW, width() - minX - 8);
  const int availH = std::max(cardH, height() - minY - 8);
  int stepX = 112;
  int stepY = 68;
  if (count > 1) {
    stepX = std::min(stepX, std::max(48, (availW - cardW) / (count - 1)));
    stepY = std::min(stepY, std::max(32, (availH - cardH) / (count - 1)));
  }
  const int spanX = (count - 1) * stepX;
  const int spanY = (count - 1) * stepY;
  QPoint front(m_fanCursor.x() - cardW / 2, m_fanCursor.y() - cardH / 3);
  const int left = front.x() - spanX;
  const int top = front.y();
  const int right = front.x() + cardW;
  const int bottom = front.y() + cardH + spanY;
  int shiftX = 0;
  int shiftY = 0;
  if (left < minX) {
    shiftX = minX - left;
  }
  if (right + shiftX > minX + availW) {
    shiftX += (minX + availW) - (right + shiftX);
  }
  if (top < minY) {
    shiftY = minY - top;
  }
  if (bottom + shiftY > minY + availH) {
    shiftY += (minY + availH) - (bottom + shiftY);
  }
  front += QPoint(shiftX, shiftY);
  if (!QRect(front, QSize(cardW, cardH)).contains(m_fanCursor)) {
    front = QPoint(m_fanCursor.x() - cardW / 2, m_fanCursor.y() - cardH / 3);
  }
  return {front + QPoint(-rank * stepX, rank * stepY), QSize(cardW, cardH)};
}

void MonitorLayoutWidget::paintFanCard(QPainter *painter, int index) const
{
  const QRect card = fanCardRect(index);
  if (painter == nullptr || card.isEmpty() || index < 0 || index >= m_layout.size()) {
    return;
  }
  const auto order = stackOrder(overlapGroup(index));
  const int rank = order.indexOf(index);
  if (rank < 0) {
    return;
  }
  const auto &computer = m_layout.at(index);
  const bool front = rank == 0;
  const bool selected = index == m_selected;
  const bool disabled = deskflow::isComputerDisabled(computer.name);
  const auto *primary = largestMonitor(computer);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor(20, 30, 50, 28));
  painter->drawRoundedRect(card.translated(0, 4), 12, 12);
  painter->setBrush(front ? QColor(255, 255, 255) : QColor(248, 246, 242));
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(card, 12, 12);
  QPen border(selected ? QColor(230, 168, 40) : (front ? QColor(47, 128, 237) : QColor(214, 214, 214)));
  border.setWidth(selected || front ? 2 : 1);
  painter->setPen(border);
  painter->setBrush(Qt::NoBrush);
  painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 11, 11);

  const QString badge = QStringLiteral("%1/%2").arg(rank + 1).arg(order.size());
  QFont badgeFont = painter->font();
  badgeFont.setPixelSize(11);
  badgeFont.setBold(true);
  const QFontMetrics badgeMetrics(badgeFont);
  const int badgeW = badgeMetrics.horizontalAdvance(badge) + 14;
  const int badgeH = 18;
  const QRect badgeRect(card.right() - badgeW - 10, card.top() + 10, badgeW, badgeH);
  painter->setFont(badgeFont);
  painter->setPen(Qt::NoPen);
  painter->setBrush(front ? QColor(47, 128, 237) : QColor(232, 226, 214));
  painter->drawRoundedRect(badgeRect, badgeH / 2, badgeH / 2);
  painter->setPen(front ? Qt::white : QColor(90, 78, 60));
  painter->drawText(badgeRect, Qt::AlignCenter, badge);

  QFont title = painter->font();
  title.setPixelSize(15);
  title.setBold(true);
  painter->setFont(title);
  painter->setPen(disabled ? QColor(130, 130, 130) : QColor(32, 40, 54));
  const QRect titleRect(card.left() + 12, card.top() + 8, badgeRect.left() - card.left() - 16, 22);
  painter->drawText(
      titleRect, Qt::AlignVCenter | Qt::AlignLeft,
      QFontMetrics(title).elidedText(computer.name, Qt::ElideRight, titleRect.width())
  );

  const QRect screen(card.center().x() - 34, card.top() + 44, 68, 44);
  painter->setPen(QPen(QColor(176, 198, 220), 2));
  painter->setBrush(QColor(226, 238, 248));
  painter->drawRoundedRect(screen, 4, 4);
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor(186, 208, 226));
  painter->drawRoundedRect(QRect(card.center().x() - 12, screen.bottom() + 1, 24, 5), 1, 1);
  painter->drawRoundedRect(QRect(card.center().x() - 20, screen.bottom() + 6, 40, 4), 1, 1);

  if (primary != nullptr) {
    QFont resolution = painter->font();
    resolution.setPixelSize(12);
    resolution.setBold(false);
    painter->setFont(resolution);
    painter->setPen(front ? QColor(47, 128, 237) : QColor(110, 110, 110));
    const QRect resolutionRect(card.left() + 8, screen.bottom() + 14, card.width() - 16, 18);
    painter->drawText(
        resolutionRect, Qt::AlignHCenter | Qt::AlignVCenter,
        QStringLiteral("%1×%2").arg(primary->width).arg(primary->height)
    );
  }
  if (disabled) {
    painter->setPen(QColor(120, 120, 120));
    painter->drawText(card.adjusted(8, 8, -8, -8), Qt::AlignHCenter | Qt::AlignBottom, tr("Disabled"));
  }
  painter->restore();
}

void MonitorLayoutWidget::paintStackNames(QPainter *painter, const Transform &transform)
{
  m_nameChips.clear();
  if (m_fanGroup >= 0 || painter == nullptr) {
    return;
  }
  const QFont saved = painter->font();
  QSet<int> seen;
  for (int index = 0; index < m_layout.size(); ++index) {
    if (seen.contains(index)) {
      continue;
    }
    const auto group = overlapGroup(index);
    for (const int member : group) {
      seen.insert(member);
    }
    if (group.size() < 2) {
      continue;
    }
    const auto order = stackOrder(group);
    const int top = order.constFirst();
    const auto *best = largestMonitor(m_layout.at(top));
    if (best == nullptr) {
      continue;
    }
    const auto global = monitorGlobal(m_layout.at(top), *best);
    const QRect painted(
        static_cast<int>(std::lround(transform.originX + global.x() * transform.scale)),
        static_cast<int>(std::lround(transform.originY + global.y() * transform.scale)),
        std::max(1, static_cast<int>(std::lround(global.width() * transform.scale))),
        std::max(1, static_cast<int>(std::lround(global.height() * transform.scale)))
    );
    if (painted.width() < 48 || painted.height() < 28) {
      continue;
    }
    QFont font = saved;
    font.setPixelSize(12);
    font.setBold(false);
    const QFontMetrics metrics(font);
    int textWidth = 0;
    for (const int member : order) {
      textWidth = std::max(textWidth, metrics.horizontalAdvance(m_layout.at(member).name));
    }
    const int rowHeight = 22;
    const int rows = std::min(static_cast<int>(order.size()), std::max(1, (painted.height() - 16) / rowHeight));
    const int cardWidth = std::min(painted.width() - 16, std::max(72, 34 + textWidth));
    const int cardHeight = rows * rowHeight + 6;
    const QRect card(painted.left() + 8, painted.bottom() - 8 - cardHeight, cardWidth, cardHeight);
    if (!painted.contains(card.center())) {
      continue;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(20, 30, 50, 24));
    painter->drawRoundedRect(card.translated(0, 2), 8, 8);
    painter->setBrush(QColor(255, 255, 255, 236));
    painter->setPen(QPen(QColor(226, 226, 230), 1));
    painter->drawRoundedRect(card, 8, 8);
    for (int place = 0; place < rows; ++place) {
      const int member = order.at(place);
      const bool lead = place == 0;
      const QRect row(card.left() + 3, card.top() + 3 + place * rowHeight, card.width() - 6, rowHeight);
      if (place > 0) {
        painter->setPen(QColor(232, 232, 236));
        painter->drawLine(row.left() + 4, row.top(), row.right() - 4, row.top());
      }
      const QRect circle(row.left() + 4, row.top() + (rowHeight - 16) / 2, 16, 16);
      painter->setPen(Qt::NoPen);
      painter->setBrush(lead ? QColor(232, 240, 252) : QColor(244, 244, 246));
      painter->drawEllipse(circle);
      QFont number = font;
      number.setPixelSize(10);
      number.setBold(lead);
      painter->setFont(number);
      painter->setPen(lead ? QColor(47, 128, 237) : QColor(150, 150, 150));
      painter->drawText(circle, Qt::AlignCenter, QString::number(place + 1));
      QFont rowFont = font;
      rowFont.setBold(lead);
      painter->setFont(rowFont);
      painter->setPen(lead ? QColor(36, 44, 58) : QColor(130, 130, 130));
      const QRect nameRect(circle.right() + 6, row.top(), row.right() - circle.right() - 10, rowHeight);
      painter->drawText(
          nameRect, Qt::AlignVCenter | Qt::AlignLeft,
          QFontMetrics(rowFont).elidedText(m_layout.at(member).name, Qt::ElideRight, nameRect.width())
      );
      m_nameChips.append(NameChip{member, row});
    }
    painter->restore();
  }
  painter->setFont(saved);
}

QRect MonitorLayoutWidget::stackRosterChip(const QString &name) const
{
  for (const auto &chip : m_nameChips) {
    if (chip.index >= 0 && chip.index < m_layout.size() && m_layout.at(chip.index).name == name) {
      return chip.rect;
    }
  }
  return {};
}
