/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PhoneStore.h"

namespace deskflow::phone {

namespace {

constexpr auto kIdsKey = "internalConfig/phones/ids";

} // namespace

PhoneStore::PhoneStore(QSettings &settings) : m_settings(settings)
{
}

QString PhoneStore::groupFor(const QString &id) const
{
  return QStringLiteral("internalConfig/phones/%1").arg(storageKeyForId(id));
}

PhoneRecord PhoneStore::readGroup(const QString &id)
{
  PhoneRecord record;
  record.id = id;
  m_settings.beginGroup(groupFor(id));
  record.label = m_settings.value(QStringLiteral("label"), id).toString();
  record.model = m_settings.value(QStringLiteral("model")).toString();
  record.placed = m_settings.value(QStringLiteral("placed"), false).toBool();
  record.originX = m_settings.value(QStringLiteral("originX"), 0).toInt();
  record.originY = m_settings.value(QStringLiteral("originY"), 0).toInt();
  record.width = m_settings.value(QStringLiteral("width"), 1080).toInt();
  record.height = m_settings.value(QStringLiteral("height"), 2640).toInt();
  record.displayId = m_settings.value(QStringLiteral("displayId"), -1).toInt();
  record.pinned = m_settings.value(QStringLiteral("pinned"), false).toBool();
  record.keyboardHintSeen = m_settings.value(QStringLiteral("keyboardHintSeen"), false).toBool();
  record.audioMuted = m_settings.value(QStringLiteral("audioMuted"), false).toBool();
  const int count = m_settings.beginReadArray(QStringLiteral("apps"));
  for (int index = 0; index < count; ++index) {
    m_settings.setArrayIndex(index);
    PhoneApp app;
    app.packageName = m_settings.value(QStringLiteral("package")).toString();
    app.label = m_settings.value(QStringLiteral("label"), app.packageName).toString();
    if (!app.packageName.isEmpty()) {
      record.apps.append(app);
    }
  }
  m_settings.endArray();
  m_settings.endGroup();
  return record;
}

QList<PhoneRecord> PhoneStore::phones()
{
  QList<PhoneRecord> records;
  const auto ids = m_settings.value(QLatin1String(kIdsKey)).toStringList();
  for (const auto &id : ids) {
    if (!id.isEmpty()) {
      records.append(readGroup(id));
    }
  }
  return records;
}

std::optional<PhoneRecord> PhoneStore::phone(const QString &id)
{
  const auto ids = m_settings.value(QLatin1String(kIdsKey)).toStringList();
  if (!ids.contains(id)) {
    return std::nullopt;
  }
  return readGroup(id);
}

void PhoneStore::save(const PhoneRecord &record)
{
  if (record.id.isEmpty()) {
    return;
  }
  auto ids = m_settings.value(QLatin1String(kIdsKey)).toStringList();
  if (!ids.contains(record.id)) {
    ids.append(record.id);
    m_settings.setValue(QLatin1String(kIdsKey), ids);
  }
  m_settings.beginGroup(groupFor(record.id));
  m_settings.setValue(QStringLiteral("label"), record.label);
  m_settings.setValue(QStringLiteral("model"), record.model);
  m_settings.setValue(QStringLiteral("placed"), record.placed);
  m_settings.setValue(QStringLiteral("originX"), record.originX);
  m_settings.setValue(QStringLiteral("originY"), record.originY);
  m_settings.setValue(QStringLiteral("width"), record.width);
  m_settings.setValue(QStringLiteral("height"), record.height);
  m_settings.setValue(QStringLiteral("displayId"), record.displayId);
  m_settings.setValue(QStringLiteral("pinned"), record.pinned);
  m_settings.setValue(QStringLiteral("keyboardHintSeen"), record.keyboardHintSeen);
  m_settings.setValue(QStringLiteral("audioMuted"), record.audioMuted);
  m_settings.beginWriteArray(QStringLiteral("apps"), record.apps.size());
  for (int index = 0; index < record.apps.size(); ++index) {
    m_settings.setArrayIndex(index);
    m_settings.setValue(QStringLiteral("package"), record.apps.at(index).packageName);
    m_settings.setValue(QStringLiteral("label"), record.apps.at(index).label);
  }
  m_settings.endArray();
  m_settings.endGroup();
  m_settings.sync();
}

void PhoneStore::remove(const QString &id)
{
  auto ids = m_settings.value(QLatin1String(kIdsKey)).toStringList();
  ids.removeAll(id);
  if (ids.isEmpty()) {
    m_settings.remove(QLatin1String(kIdsKey));
  } else {
    m_settings.setValue(QLatin1String(kIdsKey), ids);
  }
  m_settings.remove(groupFor(id));
  m_settings.sync();
}

void PhoneStore::setApps(const QString &id, const QList<PhoneApp> &apps)
{
  auto record = phone(id);
  if (!record.has_value()) {
    return;
  }
  record->apps = apps;
  save(*record);
}

} // namespace deskflow::phone
