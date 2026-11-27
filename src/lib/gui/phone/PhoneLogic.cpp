/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PhoneTypes.h"

#include <QHostAddress>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace deskflow::phone {

namespace {

constexpr int kEdgeTouchTolerance = 8;
constexpr int kMinimumFreeSpan = 32;

QStringList tokensOf(const QString &line)
{
  return line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString field(const QStringList &tokens, const QString &key)
{
  const auto prefix = key + QLatin1Char(':');
  for (const auto &token : tokens) {
    if (token.startsWith(prefix)) {
      return token.mid(prefix.size());
    }
  }
  return {};
}

QString embeddedAdbSerial(const QString &serial)
{
  static const QRegularExpression pattern(QStringLiteral("^adb-([A-Za-z0-9]+)"));
  const auto match = pattern.match(serial);
  return match.hasMatch() ? match.captured(1) : QString();
}

bool isIpEndpoint(const QString &serial)
{
  const auto colon = serial.lastIndexOf(QLatin1Char(':'));
  if (colon <= 0 || colon == serial.size() - 1) {
    return false;
  }
  bool portOk = false;
  const int port = serial.mid(colon + 1).toInt(&portOk);
  if (!portOk || port < 1 || port > 65535) {
    return false;
  }
  return !QHostAddress(serial.left(colon)).isNull();
}

QString groupId(const AdbDeviceLine &line, const QHash<QString, QString> &serialByEndpoint)
{
  if (const auto embedded = embeddedAdbSerial(line.serial); !embedded.isEmpty()) {
    return embedded;
  }
  if (const auto known = serialByEndpoint.value(line.serial); !known.isEmpty()) {
    return known;
  }
  return line.serial;
}

QRect layoutRect(const LayoutScreen &screen)
{
  return {
      screen.originX + screen.local.x(), screen.originY + screen.local.y(), screen.local.width(), screen.local.height()
  };
}

struct Range
{
  int begin = 0;
  int end = 0;
};

QList<Range> subtractRange(Range source, const QList<Range> &blocked)
{
  QList<Range> pieces{source};
  for (const auto &block : blocked) {
    QList<Range> next;
    for (const auto &piece : pieces) {
      if (block.end <= piece.begin || block.begin >= piece.end) {
        next.append(piece);
        continue;
      }
      if (block.begin > piece.begin) {
        next.append(Range{piece.begin, block.begin});
      }
      if (block.end < piece.end) {
        next.append(Range{block.end, piece.end});
      }
    }
    pieces = next;
  }
  return pieces;
}

bool overlap1D(int a0, int a1, int b0, int b1, int &begin, int &end)
{
  begin = std::max(a0, b0);
  end = std::min(a1, b1);
  return end > begin;
}

int gapOnSide(const QRect &server, const QRect &other, ScreenEdge side)
{
  switch (side) {
  case ScreenEdge::Right:
    return other.left() - (server.left() + server.width());
  case ScreenEdge::Left:
    return server.left() - (other.left() + other.width());
  case ScreenEdge::Bottom:
    return other.top() - (server.top() + server.height());
  case ScreenEdge::Top:
    return server.top() - (other.top() + other.height());
  }
  return 1000000;
}

bool spansTouch(const QRect &server, const QRect &other, ScreenEdge side, int &begin, int &end)
{
  if (std::abs(gapOnSide(server, other, side)) > kEdgeTouchTolerance) {
    return false;
  }
  if (side == ScreenEdge::Left || side == ScreenEdge::Right) {
    return overlap1D(
        server.top(), server.top() + server.height(), other.top(), other.top() + other.height(), begin, end
    );
  }
  return overlap1D(
      server.left(), server.left() + server.width(), other.left(), other.left() + other.width(), begin, end
  );
}

QString shellSingleQuote(const QString &text)
{
  QString escaped = text;
  escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  return QStringLiteral("'") + escaped + QStringLiteral("'");
}

bool packageNameOk(const QString &packageName)
{
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z0-9_]+)+$"));
  return pattern.match(packageName).hasMatch();
}

} // namespace

QList<AdbDeviceLine> parseAdbDevices(const QString &text)
{
  QList<AdbDeviceLine> lines;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
  for (const auto &row : rows) {
    const auto tokens = tokensOf(row);
    if (tokens.size() < 2 || tokens.at(0) == QLatin1String("List")) {
      continue;
    }
    AdbDeviceLine line;
    line.serial = tokens.at(0);
    line.state = tokens.at(1);
    line.model = field(tokens, QStringLiteral("model")).replace(QLatin1Char('_'), QLatin1Char(' '));
    line.product = field(tokens, QStringLiteral("product"));
    line.usb = field(tokens, QStringLiteral("usb")).isEmpty() == false;
    if (!line.usb) {
      for (const auto &token : tokens) {
        if (token.startsWith(QLatin1String("usb:"))) {
          line.usb = true;
          break;
        }
      }
    }
    lines.append(line);
  }
  return lines;
}

QList<ResolvedDevice> resolveDevices(const QString &adbDevicesText, const QHash<QString, QString> &serialByEndpoint)
{
  struct Group
  {
    ResolvedDevice device;
    QList<AdbDeviceLine> lines;
  };
  QList<Group> groups;
  auto findGroup = [&](const QString &id) -> Group * {
    for (auto &group : groups) {
      if (group.device.id == id) {
        return &group;
      }
    }
    return nullptr;
  };

  for (const auto &line : parseAdbDevices(adbDevicesText)) {
    const auto id = groupId(line, serialByEndpoint);
    auto *group = findGroup(id);
    if (group == nullptr) {
      Group created;
      created.device.id = id;
      groups.append(created);
      group = &groups.last();
    }
    group->lines.append(line);
    if (group->device.model.isEmpty()) {
      group->device.model = line.model;
    }
    if (!line.model.isEmpty()) {
      group->device.label = line.model;
    }
  }

  QList<ResolvedDevice> resolved;
  for (auto &group : groups) {
    const AdbDeviceLine *usbReady = nullptr;
    const AdbDeviceLine *wirelessReady = nullptr;
    QString detail;
    for (const auto &line : group.lines) {
      const bool ready = line.state == QLatin1String("device");
      const bool wireless = !line.usb || isIpEndpoint(line.serial) || !embeddedAdbSerial(line.serial).isEmpty();
      if (ready && line.usb && usbReady == nullptr) {
        usbReady = &line;
      }
      if (ready && wireless && wirelessReady == nullptr) {
        wirelessReady = &line;
      }
      if (detail.isEmpty() || line.state == QLatin1String("device")) {
        detail = line.state;
      }
    }
    group.device.usb = usbReady != nullptr;
    group.device.wireless = wirelessReady != nullptr;
    group.device.online = group.device.usb || group.device.wireless;
    group.device.stateDetail = detail;
    if (usbReady != nullptr) {
      group.device.scrcpySerial = usbReady->serial;
    } else if (wirelessReady != nullptr) {
      group.device.scrcpySerial = wirelessReady->serial;
    }
    if (group.device.label.isEmpty()) {
      group.device.label = group.device.id;
    }
    resolved.append(group.device);
  }
  std::sort(resolved.begin(), resolved.end(), [](const ResolvedDevice &left, const ResolvedDevice &right) {
    return left.id < right.id;
  });
  return resolved;
}

LinkDecision decideLink(bool usbReady, bool wirelessReady)
{
  LinkDecision decision;
  decision.online = usbReady || wirelessReady;
  decision.writeDisabled = false;
  return decision;
}

bool isPrivateLanHost(const QString &host)
{
  const QHostAddress address(host.trimmed());
  if (address.isNull() || address.isLoopback() || address.isMulticast()) {
    return false;
  }
  if (address.protocol() == QAbstractSocket::IPv4Protocol) {
    const quint32 ip = address.toIPv4Address();
    const auto inPrefix = [ip](quint32 base, int bits) {
      const quint32 mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
      return (ip & mask) == (base & mask);
    };
    return inPrefix(0x0A000000u, 8) || inPrefix(0xAC100000u, 12) || inPrefix(0xC0A80000u, 16) ||
           inPrefix(0xA9FE0000u, 16);
  }
  if (address.protocol() == QAbstractSocket::IPv6Protocol) {
    const auto bytes = address.toIPv6Address();
    const unsigned first = bytes[0];
    const unsigned second = bytes[1];
    const bool uniqueLocal = (first & 0xFE) == 0xFC;
    const bool linkLocal = first == 0xFE && (second & 0xC0) == 0x80;
    return uniqueLocal || linkLocal;
  }
  return false;
}

PairPlan planWirelessPair(const QString &host, int pairPort, const QString &code, int connectPort)
{
  PairPlan plan;
  const auto trimmedHost = host.trimmed();
  const auto trimmedCode = code.trimmed();
  if (!isPrivateLanHost(trimmedHost)) {
    plan.error = QStringLiteral("wireless-adb-not-lan");
    return plan;
  }
  if (pairPort < 1 || pairPort > 65535 || connectPort < 1 || connectPort > 65535) {
    plan.error = QStringLiteral("wireless-adb-bad-port");
    return plan;
  }
  if (trimmedCode.isEmpty()) {
    plan.error = QStringLiteral("wireless-adb-missing-code");
    return plan;
  }
  plan.ok = true;
  plan.pairArgs = {QStringLiteral("pair"), QStringLiteral("%1:%2").arg(trimmedHost).arg(pairPort), trimmedCode};
  plan.connectArgs = {QStringLiteral("connect"), QStringLiteral("%1:%2").arg(trimmedHost).arg(connectPort)};
  return plan;
}

QList<PhoneDisplay> parseListDisplays(const QString &text)
{
  QList<PhoneDisplay> displays;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
  static const QRegularExpression sizePattern(QStringLiteral("(\\d{2,5})x(\\d{2,5})"));
  static const QRegularExpression idPattern(QStringLiteral("(?:display[-_ ]?id[=:]\\s*)?(\\d+)"));
  for (const auto &row : rows) {
    const auto size = sizePattern.match(row);
    if (!size.hasMatch()) {
      continue;
    }
    PhoneDisplay display;
    display.width = size.captured(1).toInt();
    display.height = size.captured(2).toInt();
    const auto id = idPattern.match(row);
    display.id = id.hasMatch() ? id.captured(1).toInt() : displays.size();
    const auto lowered = row.toLower();
    display.current = row.contains(QLatin1Char('*')) || lowered.contains(QLatin1String("current")) ||
                      lowered.contains(QLatin1String("(on)")) || lowered.contains(QLatin1String(" active"));
    if (display.width > 0 && display.height > 0) {
      displays.append(display);
    }
  }
  return displays;
}

PhoneDisplay selectLogicalDisplay(const QList<PhoneDisplay> &displays)
{
  const PhoneDisplay *chosen = nullptr;
  for (const auto &display : displays) {
    if (!display.current) {
      continue;
    }
    if (chosen == nullptr || display.width * display.height > chosen->width * chosen->height) {
      chosen = &display;
    }
  }
  if (chosen == nullptr) {
    for (const auto &display : displays) {
      if (chosen == nullptr || display.width * display.height > chosen->width * chosen->height) {
        chosen = &display;
      }
    }
  }
  return chosen == nullptr ? PhoneDisplay{} : *chosen;
}

QSize parseWmSize(const QString &text)
{
  static const QRegularExpression pattern(QStringLiteral("(\\d{2,5})x(\\d{2,5})"));
  QSize physical;
  QSize overrideSize;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
  for (const auto &row : rows) {
    const auto match = pattern.match(row);
    if (!match.hasMatch()) {
      continue;
    }
    const QSize size(match.captured(1).toInt(), match.captured(2).toInt());
    if (row.contains(QLatin1String("Override"), Qt::CaseInsensitive)) {
      overrideSize = size;
    } else {
      physical = size;
    }
  }
  if (overrideSize.isValid()) {
    return overrideSize;
  }
  return physical;
}

SizeUpdate applySizeInPlace(int originX, int originY, int width, int height, const QSize &next)
{
  SizeUpdate update;
  update.originX = originX;
  update.originY = originY;
  update.width = width;
  update.height = height;
  if (!next.isValid() || next.width() < 1 || next.height() < 1) {
    return update;
  }
  update.changed = next.width() != width || next.height() != height;
  update.width = next.width();
  update.height = next.height();
  return update;
}

int parseBatteryPercent(const QString &dumpsys)
{
  static const QRegularExpression pattern(QStringLiteral("(?m)^\\s*level:\\s*(\\d{1,3})\\s*$"));
  const auto match = pattern.match(dumpsys);
  if (!match.hasMatch()) {
    return -1;
  }
  const int level = match.captured(1).toInt();
  return level >= 0 && level <= 100 ? level : -1;
}

QList<PhoneApp> parseLauncherPackages(const QString &text)
{
  QList<PhoneApp> apps;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
  static const QRegularExpression packageLine(QStringLiteral("^package:(\\S+)$"));
  for (const auto &row : rows) {
    const auto trimmed = row.trimmed();
    const auto packaged = packageLine.match(trimmed);
    PhoneApp app;
    if (packaged.hasMatch()) {
      app.packageName = packaged.captured(1);
      app.label = app.packageName;
    } else {
      const auto parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (parts.isEmpty() || !packageNameOk(parts.at(0))) {
        continue;
      }
      app.packageName = parts.at(0);
      app.label = parts.size() > 1 ? parts.mid(1).join(QLatin1Char(' ')) : app.packageName;
    }
    if (!packageNameOk(app.packageName)) {
      continue;
    }
    bool duplicate = false;
    for (const auto &existing : apps) {
      if (existing.packageName == app.packageName) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      apps.append(app);
    }
  }
  std::sort(apps.begin(), apps.end(), [](const PhoneApp &left, const PhoneApp &right) {
    return left.label.localeAwareCompare(right.label) < 0;
  });
  return apps;
}

PlacedOrigin placeBelowExisting(const QList<QRect> &existing, int width, int height, int gap)
{
  PlacedOrigin origin;
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  if (existing.isEmpty()) {
    return origin;
  }
  QRect bounds = existing.first();
  for (const auto &rect : existing) {
    bounds = bounds.united(rect);
  }
  origin.x = bounds.center().x() - width / 2;
  origin.y = bounds.y() + bounds.height() + std::max(1, gap);
  return origin;
}

QList<HotEdge> hotEdges(const QList<LayoutScreen> &screens, const QList<PhoneLayoutItem> &phones)
{
  QList<HotEdge> edges;
  const ScreenEdge sides[] = {ScreenEdge::Left, ScreenEdge::Right, ScreenEdge::Top, ScreenEdge::Bottom};
  for (const auto &phone : phones) {
    if (!phone.placed || !phone.online || phone.width < 1 || phone.height < 1) {
      continue;
    }
    const QRect phoneRect(phone.originX, phone.originY, phone.width, phone.height);
    for (const auto &screen : screens) {
      if (!screen.server || screen.local.width() < 1 || screen.local.height() < 1) {
        continue;
      }
      const QRect server = layoutRect(screen);
      for (const auto side : sides) {
        int begin = 0;
        int end = 0;
        if (!spansTouch(server, phoneRect, side, begin, end)) {
          continue;
        }
        QList<Range> blocked;
        for (const auto &other : screens) {
          if (other.server || other.local.width() < 1 || other.local.height() < 1) {
            continue;
          }
          int otherBegin = 0;
          int otherEnd = 0;
          if (spansTouch(server, layoutRect(other), side, otherBegin, otherEnd)) {
            blocked.append(Range{otherBegin, otherEnd});
          }
        }
        const auto free = subtractRange(Range{begin, end}, blocked);
        const Range *best = nullptr;
        for (const auto &piece : free) {
          if (piece.end - piece.begin < kMinimumFreeSpan) {
            continue;
          }
          if (best == nullptr || piece.end - piece.begin > best->end - best->begin) {
            best = &piece;
          }
        }
        if (best == nullptr) {
          continue;
        }
        HotEdge edge;
        edge.phoneId = phone.id;
        edge.monitorName = screen.monitorName;
        edge.side = side;
        edge.serverOriginX = screen.originX;
        edge.serverOriginY = screen.originY;
        edge.monitorX = screen.local.x();
        edge.monitorY = screen.local.y();
        edge.monitorWidth = screen.local.width();
        edge.monitorHeight = screen.local.height();
        edge.phoneOriginX = phone.originX;
        edge.phoneOriginY = phone.originY;
        edge.phoneWidth = phone.width;
        edge.phoneHeight = phone.height;
        if (side == ScreenEdge::Left || side == ScreenEdge::Right) {
          edge.alongBegin = best->begin - screen.originY;
          edge.alongEnd = best->end - screen.originY;
          edge.edgePosition = side == ScreenEdge::Left ? screen.local.x() : screen.local.x() + screen.local.width() - 1;
        } else {
          edge.alongBegin = best->begin - screen.originX;
          edge.alongEnd = best->end - screen.originX;
          edge.edgePosition = side == ScreenEdge::Top ? screen.local.y() : screen.local.y() + screen.local.height() - 1;
        }
        edges.append(edge);
      }
    }
  }
  return edges;
}

double alongFraction(const HotEdge &edge, const QPoint &localPointer)
{
  double raw = 0.0;
  if (edge.side == ScreenEdge::Left || edge.side == ScreenEdge::Right) {
    const int layoutY = edge.serverOriginY + localPointer.y();
    raw = edge.phoneHeight < 1 ? 0.0 : (layoutY - edge.phoneOriginY) / static_cast<double>(edge.phoneHeight);
  } else {
    const int layoutX = edge.serverOriginX + localPointer.x();
    raw = edge.phoneWidth < 1 ? 0.0 : (layoutX - edge.phoneOriginX) / static_cast<double>(edge.phoneWidth);
  }
  return std::clamp(raw, 0.0, 1.0);
}

std::optional<EdgeHit> hitTestEdge(const HotEdge &edge, const QPoint &localPointer, int zone)
{
  if (zone < 1 || edge.alongEnd <= edge.alongBegin) {
    return std::nullopt;
  }
  bool inside = false;
  switch (edge.side) {
  case ScreenEdge::Left:
    inside = localPointer.x() >= edge.edgePosition && localPointer.x() < edge.edgePosition + zone &&
             localPointer.y() >= edge.alongBegin && localPointer.y() < edge.alongEnd;
    break;
  case ScreenEdge::Right:
    inside = localPointer.x() <= edge.edgePosition && localPointer.x() > edge.edgePosition - zone &&
             localPointer.y() >= edge.alongBegin && localPointer.y() < edge.alongEnd;
    break;
  case ScreenEdge::Top:
    inside = localPointer.y() >= edge.edgePosition && localPointer.y() < edge.edgePosition + zone &&
             localPointer.x() >= edge.alongBegin && localPointer.x() < edge.alongEnd;
    break;
  case ScreenEdge::Bottom:
    inside = localPointer.y() <= edge.edgePosition && localPointer.y() > edge.edgePosition - zone &&
             localPointer.x() >= edge.alongBegin && localPointer.x() < edge.alongEnd;
    break;
  }
  if (!inside) {
    return std::nullopt;
  }
  EdgeHit hit;
  hit.phoneId = edge.phoneId;
  hit.side = edge.side;
  hit.alongFraction = alongFraction(edge, localPointer);
  return hit;
}

DockGeometry dockOnMonitor(
    const QRect &monitor, ScreenEdge side, int phoneWidth, int phoneHeight, double alongFraction, int bezelThickness,
    double maxAlongFraction
)
{
  DockGeometry dock;
  dock.side = side;
  const int bezel = std::max(32, bezelThickness);
  const double fraction = std::clamp(alongFraction, 0.0, 1.0);
  const double alongCap = std::clamp(maxAlongFraction, 0.30, 1.0);
  const bool verticalEdge = side == ScreenEdge::Left || side == ScreenEdge::Right;
  const int alongAvailable = verticalEdge ? monitor.height() : monitor.width();
  const int thickAvailable = std::max(1, (verticalEdge ? monitor.width() : monitor.height()) - bezel);
  const int safePhoneW = std::max(1, phoneWidth);
  const int safePhoneH = std::max(1, phoneHeight);
  int along = std::max(1, static_cast<int>(std::lround(alongAvailable * alongCap)));
  int thick = verticalEdge ? static_cast<int>(std::lround(along * (safePhoneW / static_cast<double>(safePhoneH))))
                           : static_cast<int>(std::lround(along * (safePhoneH / static_cast<double>(safePhoneW))));
  if (thick > thickAvailable) {
    thick = thickAvailable;
    along = verticalEdge ? static_cast<int>(std::lround(thick * (safePhoneH / static_cast<double>(safePhoneW))))
                         : static_cast<int>(std::lround(thick * (safePhoneW / static_cast<double>(safePhoneH))));
  }
  along = std::clamp(along, 1, std::max(1, alongAvailable));
  thick = std::clamp(thick, 1, thickAvailable);
  const int alongOffset = std::max(0, (alongAvailable - along) / 2);

  if (side == ScreenEdge::Left || side == ScreenEdge::Right) {
    const int x = side == ScreenEdge::Left ? monitor.left() : monitor.left() + monitor.width() - thick - bezel;
    const int y = monitor.top() + alongOffset;
    const int videoX = side == ScreenEdge::Left ? x : x + bezel;
    const int bezelX = side == ScreenEdge::Left ? x + thick : x;
    dock.video = QRect(videoX, y, thick, along);
    dock.bezel = QRect(bezelX, y, bezel, along);
  } else {
    const int y = side == ScreenEdge::Top ? monitor.top() : monitor.top() + monitor.height() - thick - bezel;
    const int x = monitor.left() + alongOffset;
    const int videoY = side == ScreenEdge::Top ? y : y + bezel;
    const int bezelY = side == ScreenEdge::Top ? y + thick : y;
    dock.video = QRect(x, videoY, along, thick);
    dock.bezel = QRect(x, bezelY, along, bezel);
  }
  dock.combined = dock.video.united(dock.bezel);
  const int alongPos = verticalEdge
                           ? dock.video.top() + static_cast<int>(std::lround(fraction * (dock.video.height() - 1)))
                           : dock.video.left() + static_cast<int>(std::lround(fraction * (dock.video.width() - 1)));
  if (verticalEdge) {
    const int into = side == ScreenEdge::Left ? dock.video.left() + 2 : dock.video.right() - 2;
    dock.pointer = QPoint(std::clamp(into, dock.video.left(), dock.video.right()), alongPos);
  } else {
    const int into = side == ScreenEdge::Top ? dock.video.top() + 2 : dock.video.bottom() - 2;
    dock.pointer = QPoint(alongPos, std::clamp(into, dock.video.top(), dock.video.bottom()));
  }
  return dock;
}

DockGeometry alignPointer(DockGeometry dock, const QPoint &cursor, const QRect &monitor)
{
  const bool verticalEdge = dock.side == ScreenEdge::Left || dock.side == ScreenEdge::Right;
  int delta = verticalEdge ? cursor.y() - dock.pointer.y() : cursor.x() - dock.pointer.x();
  auto shift = [&](int amount) {
    if (verticalEdge) {
      dock.video.translate(0, amount);
      dock.bezel.translate(0, amount);
      dock.pointer.ry() += amount;
    } else {
      dock.video.translate(amount, 0);
      dock.bezel.translate(amount, 0);
      dock.pointer.rx() += amount;
    }
    dock.combined = dock.video.united(dock.bezel);
  };
  shift(delta);
  if (verticalEdge) {
    if (dock.combined.top() < monitor.top()) {
      shift(monitor.top() - dock.combined.top());
    }
    if (dock.combined.bottom() > monitor.bottom()) {
      shift(monitor.bottom() - dock.combined.bottom());
    }
  } else {
    if (dock.combined.left() < monitor.left()) {
      shift(monitor.left() - dock.combined.left());
    }
    if (dock.combined.right() > monitor.right()) {
      shift(monitor.right() - dock.combined.right());
    }
  }
  return dock;
}

bool combinedContains(
    const QRect &video, const QRect &bezel, const QRect &edgeBar, const QPoint &point, bool dragActive
)
{
  if (dragActive) {
    return true;
  }
  return video.contains(point) || bezel.contains(point) || edgeBar.contains(point);
}

SessionStep reduceSession(const SessionState &state, const SessionConfig &config, SessionEvent event)
{
  SessionStep step;
  step.state = state;
  step.pointerFrozen = false;
  const bool ownsProcess = state.phase == SessionPhase::Prewarming || state.phase == SessionPhase::HiddenReady ||
                           state.phase == SessionPhase::Visible;

  if (event == SessionEvent::DeviceOffline) {
    step.state.phase = SessionPhase::Idle;
    step.state.pinned = false;
    step.action = ownsProcess ? SessionAction::Stop : SessionAction::None;
    return step;
  }

  switch (state.phase) {
  case SessionPhase::Idle:
    if (event == SessionEvent::DeviceOnline && config.prewarmHidden) {
      step.state.phase = SessionPhase::Prewarming;
      step.action = SessionAction::SpawnHidden;
    } else if (event == SessionEvent::EdgeEntered) {
      step.state.phase = SessionPhase::Visible;
      step.action = SessionAction::SpawnVisible;
    } else if (event == SessionEvent::Adopted) {
      step.state.phase = SessionPhase::HiddenReady;
    }
    break;
  case SessionPhase::Prewarming:
    if (event == SessionEvent::PrewarmOk || event == SessionEvent::Adopted) {
      step.state.phase = SessionPhase::HiddenReady;
    } else if (event == SessionEvent::PrewarmFailed) {
      step.state.phase = SessionPhase::Failed;
      step.action = SessionAction::Notify;
      step.notice = QStringLiteral("prewarm-failed");
    } else if (event == SessionEvent::EdgeEntered) {
      step.state.phase = SessionPhase::Visible;
      step.action = SessionAction::Show;
    } else if (event == SessionEvent::HideRequested) {
      step.state.phase = config.hideKeepsProcess ? SessionPhase::HiddenReady : SessionPhase::Idle;
      step.action = config.hideKeepsProcess ? SessionAction::Hide : SessionAction::Stop;
    }
    break;
  case SessionPhase::HiddenReady:
    if (event == SessionEvent::EdgeEntered) {
      step.state.phase = SessionPhase::Visible;
      step.action = SessionAction::Show;
    } else if (event == SessionEvent::Adopted) {
      step.action = SessionAction::None;
    }
    break;
  case SessionPhase::Visible:
    if (event == SessionEvent::HideRequested) {
      if (config.hideKeepsProcess) {
        step.state.phase = SessionPhase::HiddenReady;
        step.action = SessionAction::Hide;
      } else {
        step.state.phase = SessionPhase::Idle;
        step.action = SessionAction::Stop;
      }
    } else if (event == SessionEvent::EdgeEntered || event == SessionEvent::Adopted ||
               event == SessionEvent::DeviceOnline) {
      step.action = SessionAction::None;
    }
    break;
  case SessionPhase::Failed:
    if (event == SessionEvent::EdgeEntered) {
      step.state.phase = SessionPhase::Visible;
      step.action = SessionAction::SpawnVisible;
    } else if (event == SessionEvent::DeviceOnline && config.prewarmHidden) {
      step.state.phase = SessionPhase::Prewarming;
      step.action = SessionAction::SpawnHidden;
    }
    break;
  }
  return step;
}

LeaveResult advanceLeave(const LeaveTracker &tracker, const LeaveSample &sample, int dtMs)
{
  LeaveResult result;
  result.tracker = tracker;
  if (sample.pinned || sample.insideCombined) {
    result.tracker = {};
    result.effect = LeaveEffect::Stay;
    return result;
  }
  result.tracker.outside = true;
  result.tracker.outsideMs += std::max(0, dtMs);
  if (result.tracker.outsideMs >= std::max(0, sample.hysteresisMs)) {
    result.effect = LeaveEffect::Hide;
    result.tracker = {};
  }
  return result;
}

WaylandInside waylandPointerInside(const WaylandPointer &pointer)
{
  WaylandInside result;
  result.bezelHot = pointer.bezelHot || pointer.onBezel;
  if (pointer.drag || pointer.onBezel || pointer.onEdge) {
    result.inside = true;
    return result;
  }
  if (pointer.onSensor && pointer.bezelHot) {
    result.inside = false;
    result.bezelHot = true;
    return result;
  }
  if (pointer.onSensor) {
    result.inside = true;
    result.bezelHot = false;
    return result;
  }
  // The pointer is over the scrcpy window, which this process cannot see.
  result.inside = true;
  return result;
}

bool isPlainText(const QString &text)
{
  for (const auto ch : text) {
    const ushort code = ch.unicode();
    if (code == u'\n' || code == u'\r' || code == u'\t') {
      continue;
    }
    if (code < 0x20 || code == 0x7F) {
      return false;
    }
  }
  return true;
}

ClipPlan planClipboard(ClipboardIntent intent, const QString &text, bool canFocusAndPaste)
{
  ClipPlan plan;
  if (!isPlainText(text)) {
    plan.steps.append(ClipStep{ClipOp::Error, {}, QStringLiteral("not-plain-text")});
    return plan;
  }
  if (intent == ClipboardIntent::Send) {
    plan.steps.append(ClipStep{ClipOp::SetDeviceClipboard, text, {}});
    return plan;
  }
  plan.steps.append(ClipStep{ClipOp::SetDeviceClipboard, text, {}});
  plan.steps.append(ClipStep{ClipOp::SetComputerClipboard, text, {}});
  if (!canFocusAndPaste) {
    plan.steps.append(ClipStep{ClipOp::Error, text, QStringLiteral("cannot-focus-phone-window")});
    return plan;
  }
  plan.steps.append(ClipStep{ClipOp::FocusWindow, text, {}});
  plan.steps.append(ClipStep{ClipOp::InjectPaste, text, {}});
  plan.pastes = true;
  return plan;
}

AdbCommand readClipboardCommand(const QString &serial)
{
  return AdbCommand{
      {QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("clipboard"),
       QStringLiteral("get-text")}
  };
}

AdbCommand writeClipboardCommand(const QString &serial, const QString &text)
{
  return AdbCommand{
      {QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("clipboard"),
       QStringLiteral("set-text"), shellSingleQuote(text)}
  };
}

LaunchPlan planAppLaunch(const QString &serial, const QString &packageName, bool windowHidden)
{
  LaunchPlan plan;
  plan.showWindowFirst = windowHidden;
  if (!packageNameOk(packageName)) {
    return plan;
  }
  plan.command.args = {QStringLiteral("-s"),    serial,
                       QStringLiteral("shell"), QStringLiteral("monkey"),
                       QStringLiteral("-p"),    packageName,
                       QStringLiteral("-c"),    QStringLiteral("android.intent.category.LAUNCHER"),
                       QStringLiteral("1")};
  return plan;
}

AdbCommand pushFileCommand(const QString &serial, const QString &localPath)
{
  const auto name =
      localPath.mid(std::max(localPath.lastIndexOf(QLatin1Char('/')), localPath.lastIndexOf(QLatin1Char('\\'))) + 1);
  if (serial.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String(".") ||
      name == QLatin1String("..")) {
    return {};
  }
  return AdbCommand{
      {QStringLiteral("-s"), serial, QStringLiteral("push"), localPath, QStringLiteral("/sdcard/Download/%1").arg(name)}
  };
}

QString scrcpyWindowTitle(const QString &phoneId)
{
  QString safe;
  safe.reserve(phoneId.size());
  for (const auto ch : phoneId) {
    safe.append(ch.isLetterOrNumber() ? ch : QLatin1Char('_'));
  }
  if (safe.isEmpty()) {
    safe = QStringLiteral("phone");
  }
  return QStringLiteral("deskflow-phone-%1").arg(safe);
}

QStringList scrcpyArguments(const ScrcpyRequest &request)
{
  QStringList args;
  if (!request.serial.isEmpty()) {
    args << QStringLiteral("-s") << request.serial;
  }
  args << QStringLiteral("--keyboard=uhid");
  args << QStringLiteral("--window-title=%1").arg(scrcpyWindowTitle(request.phoneId));
  args << QStringLiteral("--window-borderless");
  if (request.displayId >= 0) {
    args << QStringLiteral("--display-id=%1").arg(request.displayId);
  }
  if (!request.audio) {
    args << QStringLiteral("--no-audio");
  }
  if (request.placeWindow) {
    args << QStringLiteral("--window-x=%1").arg(request.x) << QStringLiteral("--window-y=%1").arg(request.y)
         << QStringLiteral("--window-width=%1").arg(std::max(1, request.width))
         << QStringLiteral("--window-height=%1").arg(std::max(1, request.height));
  }
  return args;
}

KeyPlan planKey(PhoneKey key, bool canInject, const QString &serial)
{
  KeyPlan plan;
  plan.inject = canInject;
  switch (key) {
  case PhoneKey::Back:
    plan.xdotoolKey = QStringLiteral("alt+b");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("4")};
    break;
  case PhoneKey::Home:
    plan.xdotoolKey = QStringLiteral("alt+h");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("3")};
    break;
  case PhoneKey::Recents:
    plan.xdotoolKey = QStringLiteral("alt+s");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("187")};
    break;
  case PhoneKey::Power:
    plan.xdotoolKey = QStringLiteral("alt+p");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("26")};
    break;
  case PhoneKey::VolumeUp:
    plan.xdotoolKey = QStringLiteral("alt+Up");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("24")};
    break;
  case PhoneKey::VolumeDown:
    plan.xdotoolKey = QStringLiteral("alt+Down");
    plan.adb.args = {QStringLiteral("-s"),       serial,
                     QStringLiteral("shell"),    QStringLiteral("input"),
                     QStringLiteral("keyevent"), QStringLiteral("25")};
    break;
  case PhoneKey::Notifications:
    plan.xdotoolKey = QStringLiteral("alt+n");
    plan.adb.args = {QStringLiteral("-s"),        serial,
                     QStringLiteral("shell"),     QStringLiteral("cmd"),
                     QStringLiteral("statusbar"), QStringLiteral("expand-notifications")};
    break;
  case PhoneKey::ScreenOff:
    plan.xdotoolKey = QStringLiteral("alt+o");
    plan.unsupportedWithoutInject = true;
    break;
  case PhoneKey::ScreenOn:
    plan.xdotoolKey = QStringLiteral("alt+shift+o");
    plan.unsupportedWithoutInject = true;
    break;
  case PhoneKey::KeyboardLayout:
    plan.xdotoolKey = QStringLiteral("alt+k");
    plan.unsupportedWithoutInject = true;
    break;
  case PhoneKey::Paste:
    plan.xdotoolKey = QStringLiteral("alt+v");
    plan.unsupportedWithoutInject = true;
    break;
  }
  if (!canInject && plan.unsupportedWithoutInject) {
    plan.adb = {};
    plan.inject = false;
  }
  if (canInject) {
    plan.adb = {};
  }
  return plan;
}

ExecutableChoice
chooseExecutable(const QString &configured, bool configuredUsable, const QString &pathHit, const QStringList &wellKnown)
{
  if (!configured.trimmed().isEmpty() && configuredUsable) {
    return {ExecutableSource::Configured, configured.trimmed()};
  }
  if (!pathHit.isEmpty()) {
    return {ExecutableSource::Path, pathHit};
  }
  for (const auto &candidate : wellKnown) {
    if (!candidate.isEmpty()) {
      return {ExecutableSource::WellKnown, candidate};
    }
  }
  return {};
}

WindowControl windowControlFor(const QString &sessionType, bool xdotoolFound, bool unixLike)
{
  WindowControl control;
  if (!unixLike) {
    control.limit = WindowLimit::UnsupportedOs;
    return control;
  }
  if (sessionType.compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0) {
    control.limit = WindowLimit::Wayland;
    return control;
  }
  if (!xdotoolFound) {
    control.limit = WindowLimit::NoXdotool;
    return control;
  }
  control.findWindow = true;
  control.hideWindow = true;
  control.activateWindow = true;
  control.warpPointer = true;
  control.injectKeys = true;
  control.limit = WindowLimit::None;
  return control;
}

QList<RunningScrcpy> parseScrcpyProcessList(const QString &text)
{
  QList<RunningScrcpy> found;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
  for (const auto &row : rows) {
    const auto trimmed = row.trimmed();
    if (!trimmed.contains(QLatin1String("scrcpy"))) {
      continue;
    }
    const auto space = trimmed.indexOf(QLatin1Char(' '));
    bool pidOk = false;
    const auto pidToken = space < 0 ? trimmed : trimmed.left(space);
    const qint64 pid = pidToken.toLongLong(&pidOk);
    if (!pidOk || pid <= 0) {
      continue;
    }
    const auto args = space < 0 ? QString() : trimmed.mid(space + 1);
    if (!args.contains(QLatin1String("scrcpy"))) {
      continue;
    }
    found.append(RunningScrcpy{pid, args});
  }
  return found;
}

bool scrcpyCommandOwnsSerial(const QString &args, const QString &serial)
{
  if (serial.isEmpty()) {
    return false;
  }
  const auto parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (int index = 0; index < parts.size(); ++index) {
    if ((parts.at(index) == QLatin1String("-s") || parts.at(index) == QLatin1String("--serial")) &&
        index + 1 < parts.size() && parts.at(index + 1) == serial) {
      return true;
    }
    if (parts.at(index) == QStringLiteral("--serial=%1").arg(serial)) {
      return true;
    }
  }
  return false;
}

QList<SinkInput> parsePactlSinkInputs(const QString &text)
{
  QList<SinkInput> inputs;
  SinkInput current;
  bool inInput = false;
  const auto rows = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")));
  for (const auto &row : rows) {
    const auto trimmed = row.trimmed();
    if (trimmed.startsWith(QLatin1String("Sink Input #"))) {
      if (inInput && current.id >= 0) {
        inputs.append(current);
      }
      current = {};
      inInput = true;
      current.id = trimmed.mid(QStringLiteral("Sink Input #").size()).toInt();
      continue;
    }
    if (!inInput) {
      continue;
    }
    if (trimmed.startsWith(QLatin1String("application.name = "))) {
      auto name = trimmed.mid(QStringLiteral("application.name = ").size()).trimmed();
      if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')) && name.size() >= 2) {
        name = name.mid(1, name.size() - 2);
      }
      current.application = name;
    }
  }
  if (inInput && current.id >= 0) {
    inputs.append(current);
  }
  return inputs;
}

QStringList proxyVariablesToDrop()
{
  return {QStringLiteral("http_proxy"),  QStringLiteral("https_proxy"), QStringLiteral("all_proxy"),
          QStringLiteral("ftp_proxy"),   QStringLiteral("socks_proxy"), QStringLiteral("HTTP_PROXY"),
          QStringLiteral("HTTPS_PROXY"), QStringLiteral("ALL_PROXY"),   QStringLiteral("FTP_PROXY"),
          QStringLiteral("SOCKS_PROXY")};
}

QString storageKeyForId(const QString &id)
{
  return QString::fromLatin1(id.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace deskflow::phone
