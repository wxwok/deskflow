/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/phone/PhoneTypes.h"

#include <QSettings>

namespace deskflow::phone {

//! Persists registered phones under internalConfig/phones. Never writes a Disabled computer.
class PhoneStore
{
public:
  explicit PhoneStore(QSettings &settings);

  QList<PhoneRecord> phones();
  std::optional<PhoneRecord> phone(const QString &id);
  void save(const PhoneRecord &record);
  void remove(const QString &id);
  void setApps(const QString &id, const QList<PhoneApp> &apps);

private:
  QString groupFor(const QString &id) const;
  PhoneRecord readGroup(const QString &id);

  QSettings &m_settings;
};

} // namespace deskflow::phone
