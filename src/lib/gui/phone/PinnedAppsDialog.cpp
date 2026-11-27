/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PinnedAppsDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace deskflow::phone {

PinnedAppsDialog::PinnedAppsDialog(const QList<PhoneApp> &available, const QList<PhoneApp> &pinned, QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle(tr("Pin phone apps"));
  auto *layout = new QVBoxLayout(this);
  auto *hint = new QLabel(tr("Pinned apps are saved for this phone only. A click launches that app."), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  m_list = new QListWidget(this);
  QList<PhoneApp> rows = available;
  for (const auto &app : pinned) {
    bool found = false;
    for (const auto &existing : rows) {
      if (existing.packageName == app.packageName) {
        found = true;
        break;
      }
    }
    if (!found) {
      rows.append(app);
    }
  }
  for (const auto &app : rows) {
    auto *item = new QListWidgetItem(app.label.isEmpty() ? app.packageName : app.label, m_list);
    item->setData(Qt::UserRole, app.packageName);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    bool checked = false;
    for (const auto &pin : pinned) {
      if (pin.packageName == app.packageName) {
        checked = true;
        break;
      }
    }
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  }
  layout->addWidget(m_list);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  resize(360, 420);
}

QList<PhoneApp> PinnedAppsDialog::selected() const
{
  QList<PhoneApp> apps;
  for (int index = 0; index < m_list->count(); ++index) {
    const auto *item = m_list->item(index);
    if (item->checkState() != Qt::Checked) {
      continue;
    }
    PhoneApp app;
    app.packageName = item->data(Qt::UserRole).toString();
    app.label = item->text();
    apps.append(app);
  }
  return apps;
}

} // namespace deskflow::phone
