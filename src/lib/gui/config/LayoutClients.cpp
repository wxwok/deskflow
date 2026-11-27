/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/config/LayoutClients.h"

#include "common/Settings.h"

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

namespace deskflow {

namespace {

constexpr int kPlaceholderWidth = 1920;
constexpr int kPlaceholderHeight = 1080;

QString fold(const QString &name)
{
  return name.trimmed().toLower();
}

bool sameName(const QString &left, const QString &right)
{
  return fold(left) == fold(right) && !fold(left).isEmpty();
}

QString canonicalKnownName(const QString &name)
{
  for (const auto &known : Settings::knownComputers()) {
    if (sameName(known, name)) {
      return known;
    }
  }
  return name.trimmed();
}

void setBoth(const QString &key, const QVariant &value)
{
  Settings::setValue(key, value);
  Settings::proxy().setValue(key, value);
}

void writeLayoutArray(QSettings &store, bool useMonitorLayout, const QList<LayoutComputer> &layout)
{
  store.beginGroup(QStringLiteral("internalConfig"));
  store.setValue(QStringLiteral("useMonitorLayout"), useMonitorLayout);
  store.remove(QStringLiteral("monitorLayout"));
  store.beginWriteArray(QStringLiteral("monitorLayout"));
  for (int index = 0; index < layout.size(); ++index) {
    store.setArrayIndex(index);
    const auto &computer = layout.at(index);
    store.setValue(QStringLiteral("name"), computer.name);
    store.setValue(QStringLiteral("originX"), computer.originX);
    store.setValue(QStringLiteral("originY"), computer.originY);
    store.setValue(QStringLiteral("z"), computer.z);
    store.beginWriteArray(QStringLiteral("monitors"));
    for (int monitorIndex = 0; monitorIndex < computer.monitors.size(); ++monitorIndex) {
      store.setArrayIndex(monitorIndex);
      const auto &monitor = computer.monitors.at(monitorIndex);
      store.setValue(QStringLiteral("name"), monitor.name);
      store.setValue(QStringLiteral("x"), monitor.x);
      store.setValue(QStringLiteral("y"), monitor.y);
      store.setValue(QStringLiteral("width"), monitor.width);
      store.setValue(QStringLiteral("height"), monitor.height);
    }
    store.endArray();
  }
  store.endArray();
  store.endGroup();
  store.sync();
}

QStringList layoutSectionLines(const QList<LayoutComputer> &layout)
{
  QStringList lines;
  lines.append(QStringLiteral("section: layout"));
  for (const auto &computer : layout) {
    lines.append(QStringLiteral("\t%1:").arg(computer.name));
    lines.append(QStringLiteral("\t\torigin = %1, %2").arg(computer.originX).arg(computer.originY));
    lines.append(QStringLiteral("\t\tz = %1").arg(computer.z));
    for (const auto &monitor : computer.monitors) {
      lines.append(QStringLiteral("\t\tmonitor = %1, %2, %3, %4, %5")
                       .arg(monitor.name)
                       .arg(monitor.x)
                       .arg(monitor.y)
                       .arg(monitor.width)
                       .arg(monitor.height));
    }
  }
  lines.append(QStringLiteral("end"));
  return lines;
}

bool parseIntField(const QString &text, int &out)
{
  bool ok = false;
  const auto value = text.trimmed().toInt(&ok);
  if (!ok) {
    return false;
  }
  out = value;
  return true;
}

} // namespace

QList<ClientPresence>
unplacedClients(const QStringList &connected, const QStringList &reported, const QStringList &placed)
{
  QStringList placedFolded;
  placedFolded.reserve(placed.size());
  for (const auto &name : placed) {
    const auto key = fold(name);
    if (!key.isEmpty()) {
      placedFolded.append(key);
    }
  }

  QMap<QString, ClientPresence> found;
  const auto consider = [&](const QString &name, bool isConnected, bool isReported) {
    const auto trimmed = name.trimmed();
    const auto key = fold(trimmed);
    if (key.isEmpty() || placedFolded.contains(key)) {
      return;
    }
    auto &slot = found[key];
    if (slot.name.isEmpty()) {
      slot.name = trimmed;
    }
    slot.connected = slot.connected || isConnected;
    slot.reported = slot.reported || isReported;
  };

  for (const auto &name : connected) {
    consider(name, true, false);
  }
  for (const auto &name : reported) {
    consider(name, false, true);
  }

  QList<ClientPresence> result;
  result.reserve(found.size());
  for (const auto &presence : found) {
    result.append(presence);
  }
  return result;
}

QStringList reportedComputerNames()
{
  auto &settings = Settings::proxy();
  settings.beginGroup(QStringLiteral("internalConfig"));
  settings.beginGroup(QStringLiteral("reportedGeometry"));
  const auto proxyKeys = settings.get().childKeys();
  settings.endGroup();
  settings.endGroup();

  Settings::store().beginGroup(QStringLiteral("internalConfig"));
  Settings::store().beginGroup(QStringLiteral("reportedGeometry"));
  auto keys = Settings::store().childKeys();
  Settings::store().endGroup();
  Settings::store().endGroup();
  for (const auto &key : proxyKeys) {
    if (!keys.contains(key)) {
      keys.append(key);
    }
  }
  return keys;
}

QStringList connectedComputerNames()
{
  auto names = Settings::value(Settings::InternalConfig::ConnectedClients).toStringList();
  if (names.isEmpty()) {
    names = Settings::proxy().value(Settings::InternalConfig::ConnectedClients).toStringList();
  }
  return names;
}

QStringList hotkeyTargetNames()
{
  QStringList names;
  for (const auto &name : Settings::knownComputers()) {
    if (name.isEmpty() || isComputerDisabled(name)) {
      continue;
    }
    names.append(name);
  }
  names.sort(Qt::CaseInsensitive);
  return names;
}

bool isValidLayoutComputerName(const QString &name)
{
  static const auto pattern = QRegularExpression(QStringLiteral("^[\\w\\._-]{1,255}$"));
  return pattern.match(name.trimmed()).hasMatch();
}

bool isComputerDisabled(const QString &name)
{
  const auto canonical = canonicalKnownName(name);
  if (canonical.isEmpty()) {
    return false;
  }
  return Settings::value(Settings::Computer::Disabled.arg(canonical)).toBool();
}

ComputerRegistration registerComputer(const QString &name)
{
  const auto requested = name.trimmed();
  if (!isValidLayoutComputerName(requested)) {
    return ComputerRegistration::Rejected;
  }
  for (const auto &known : Settings::knownComputers()) {
    if (sameName(known, requested)) {
      return ComputerRegistration::Existing;
    }
  }
  setBoth(Settings::Computer::Name.arg(requested), requested);
  setBoth(Settings::Computer::Disabled.arg(requested), false);
  Settings::proxy().sync();
  return ComputerRegistration::Created;
}

void setComputerDisabled(const QString &name, bool disabled)
{
  if (registerComputer(name) == ComputerRegistration::Rejected) {
    return;
  }
  const auto stored = canonicalKnownName(name);
  setBoth(Settings::Computer::Disabled.arg(stored), disabled);
  Settings::proxy().sync();
}

void unregisterComputer(const QString &name)
{
  const auto group = QStringLiteral("computer_%1").arg(name.trimmed());
  Settings::store().remove(group);
  Settings::store().sync();
  Settings::proxy().remove(group);
  Settings::proxy().sync();
}

QList<LayoutMonitor> parseMonitorBody(const QString &body)
{
  QList<LayoutMonitor> monitors;
  const auto records = body.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  for (const auto &record : records) {
    const auto fields = record.split(QLatin1Char(','));
    if (fields.size() != 5) {
      continue;
    }
    LayoutMonitor monitor;
    monitor.name = fields.at(0);
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    if (!parseIntField(fields.at(1), x) || !parseIntField(fields.at(2), y) || !parseIntField(fields.at(3), width) ||
        !parseIntField(fields.at(4), height) || width <= 0 || height <= 0) {
      continue;
    }
    monitor.x = x;
    monitor.y = y;
    monitor.width = width;
    monitor.height = height;
    monitors.append(monitor);
  }
  return monitors;
}

QList<LayoutMonitor> reportedMonitors(const QString &computerName)
{
  const auto key = QStringLiteral("internalConfig/reportedGeometry/%1").arg(computerName);
  auto body = Settings::value(key).toString();
  if (body.isEmpty()) {
    body = Settings::proxy().value(key).toString();
  }
  return parseMonitorBody(body);
}

QList<LayoutComputer> parseLayoutSection(const QString &configText)
{
  QString normalized = configText;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  const auto lines = normalized.split(QLatin1Char('\n'));
  QList<LayoutComputer> layout;
  bool inSection = false;
  LayoutComputer current;
  bool haveCurrent = false;
  const auto commit = [&]() {
    if (haveCurrent && !current.name.isEmpty() && !current.monitors.isEmpty()) {
      layout.append(current);
    }
    current = {};
    haveCurrent = false;
  };

  for (const auto &line : lines) {
    const auto trimmed = line.trimmed();
    if (!inSection) {
      if (trimmed == QStringLiteral("section: layout")) {
        inSection = true;
      }
      continue;
    }
    if (trimmed == QStringLiteral("end")) {
      commit();
      break;
    }
    if (trimmed.endsWith(QLatin1Char(':'))) {
      commit();
      current.name = trimmed.left(trimmed.size() - 1).trimmed();
      haveCurrent = !current.name.isEmpty();
      continue;
    }
    const auto separator = trimmed.indexOf(QLatin1Char('='));
    if (!haveCurrent || separator < 0) {
      continue;
    }
    const auto key = trimmed.left(separator).trimmed();
    const auto value = trimmed.mid(separator + 1).trimmed();
    if (key == QStringLiteral("origin")) {
      const auto parts = value.split(QLatin1Char(','));
      int originX = 0;
      int originY = 0;
      if (parts.size() == 2 && parseIntField(parts.at(0), originX) && parseIntField(parts.at(1), originY)) {
        current.originX = originX;
        current.originY = originY;
      }
    } else if (key == QStringLiteral("z")) {
      int z = 0;
      if (parseIntField(value, z)) {
        current.z = z;
      }
    } else if (key == QStringLiteral("monitor")) {
      const auto parts = value.split(QLatin1Char(','));
      if (parts.size() < 5) {
        continue;
      }
      const auto count = parts.size();
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;
      if (!parseIntField(parts.at(count - 4), x) || !parseIntField(parts.at(count - 3), y) ||
          !parseIntField(parts.at(count - 2), width) || !parseIntField(parts.at(count - 1), height) || width <= 0 ||
          height <= 0) {
        continue;
      }
      QStringList nameParts;
      for (int index = 0; index < count - 4; ++index) {
        nameParts.append(parts.at(index).trimmed());
      }
      current.monitors.append(LayoutMonitor{nameParts.join(QStringLiteral(", ")), x, y, width, height});
    }
  }
  return layout;
}

QString replaceLayoutSection(const QString &configText, const QList<LayoutComputer> &layout)
{
  const bool crlf = configText.contains(QStringLiteral("\r\n"));
  QString normalized = configText;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  auto lines = normalized.split(QLatin1Char('\n'));

  int start = -1;
  int end = -1;
  for (int index = 0; index < lines.size(); ++index) {
    if (start < 0 && lines.at(index).trimmed() == QStringLiteral("section: layout")) {
      start = index;
      continue;
    }
    if (start >= 0 && lines.at(index).trimmed() == QStringLiteral("end")) {
      end = index;
      break;
    }
  }

  const auto section = layoutSectionLines(layout);
  QStringList out;
  if (start < 0 || end < start) {
    if (!lines.isEmpty() && lines.last().isEmpty()) {
      lines.removeLast();
    }
    if (!lines.isEmpty() && !lines.last().isEmpty()) {
      lines.append(QString());
    }
    lines.append(section);
    lines.append(QString());
    out = lines;
  } else {
    for (int index = 0; index < start; ++index) {
      out.append(lines.at(index));
    }
    out.append(section);
    for (int index = end + 1; index < lines.size(); ++index) {
      out.append(lines.at(index));
    }
  }

  auto joined = out.join(QLatin1Char('\n'));
  if (crlf) {
    joined.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
  }
  return joined;
}

LayoutComputer
placeToTheRight(const QList<LayoutComputer> &layout, const QString &name, const QList<LayoutMonitor> &monitors)
{
  int right = 0;
  bool any = false;
  for (const auto &computer : layout) {
    for (const auto &monitor : computer.monitors) {
      any = true;
      right = std::max(right, computer.originX + monitor.x + monitor.width);
    }
  }

  LayoutComputer placed;
  placed.name = name;
  placed.originX = any ? right : 0;
  placed.originY = 0;
  if (monitors.isEmpty()) {
    placed.monitors.append(LayoutMonitor{name, 0, 0, kPlaceholderWidth, kPlaceholderHeight});
  } else {
    placed.monitors = monitors;
  }
  return placed;
}

LayoutWriteResult writeLayoutFile(const QString &path, const QList<LayoutComputer> &layout)
{
  LayoutWriteResult result;
  QFile input(path);
  if (!input.exists() || !input.open(QIODevice::ReadOnly)) {
    result.error = LayoutWriteError::ReadFailed;
    return result;
  }
  const auto original = QString::fromUtf8(input.readAll());
  input.close();

  const auto updated = replaceLayoutSection(original, layout);
  const auto backupPath = path + QStringLiteral(".bak");
  QFile::remove(backupPath);
  if (!QFile::copy(path, backupPath)) {
    result.error = LayoutWriteError::BackupFailed;
    return result;
  }

  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    result.error = LayoutWriteError::WriteFailed;
    return result;
  }
  const auto bytes = updated.toUtf8();
  if (output.write(bytes) != bytes.size() || !output.commit()) {
    result.error = LayoutWriteError::WriteFailed;
    return result;
  }
  result.ok = true;
  result.backupPath = backupPath;
  return result;
}

void saveInternalMonitorLayout(const ServerConfig &config)
{
  writeLayoutArray(Settings::store(), config.useMonitorLayout(), config.monitorLayout());
  writeLayoutArray(Settings::proxy().get(), config.useMonitorLayout(), config.monitorLayout());
}

AddComputerResult addComputerToActiveLayout(
    bool external, const QString &configPath, ServerConfig *internal, const QList<LayoutComputer> &layout,
    const QString &name, const QList<LayoutMonitor> &monitors
)
{
  AddComputerResult result;
  result.layout = layout;
  const auto requested = name.trimmed();
  if (!isValidLayoutComputerName(requested)) {
    result.error = LayoutWriteError::InvalidName;
    return result;
  }
  for (const auto &computer : layout) {
    if (sameName(computer.name, requested)) {
      result.error = LayoutWriteError::AlreadyPlaced;
      return result;
    }
  }

  const auto registration = registerComputer(requested);
  if (registration == ComputerRegistration::Rejected) {
    result.error = LayoutWriteError::InvalidName;
    return result;
  }
  const auto storedName = canonicalKnownName(requested);
  auto updated = layout;
  updated.append(placeToTheRight(layout, storedName, monitors));

  if (external) {
    const auto written = writeLayoutFile(configPath, updated);
    if (!written.ok) {
      if (registration == ComputerRegistration::Created) {
        unregisterComputer(storedName);
      }
      result.error = written.error;
      return result;
    }
    result.backupPath = written.backupPath;
  } else if (internal != nullptr) {
    internal->setMonitorLayout(updated);
    saveInternalMonitorLayout(*internal);
  }

  result.ok = true;
  result.layout = updated;
  return result;
}

} // namespace deskflow
