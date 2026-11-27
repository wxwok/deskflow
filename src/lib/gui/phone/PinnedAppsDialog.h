/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/phone/PhoneTypes.h"

#include <QDialog>

class QListWidget;

namespace deskflow::phone {

class PinnedAppsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit PinnedAppsDialog(const QList<PhoneApp> &available, const QList<PhoneApp> &pinned, QWidget *parent = nullptr);

  QList<PhoneApp> selected() const;

private:
  QListWidget *m_list = nullptr;
};

} // namespace deskflow::phone
