/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/PairingDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>

namespace deskflow::phone {

PairingDialog::PairingDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Pair wireless debugging"));
  auto *layout = new QFormLayout(this);
  auto *hint = new QLabel(
      tr("On the phone, open Wireless debugging and pair with a code. Use the phone's LAN address. "
         "Deskflow does not use a proxy for adb."),
      this
  );
  hint->setWordWrap(true);
  layout->addRow(hint);
  m_host = new QLineEdit(this);
  m_host->setPlaceholderText(QStringLiteral("192.168.0.10"));
  m_pairPort = new QSpinBox(this);
  m_pairPort->setRange(1, 65535);
  m_pairPort->setValue(37123);
  m_code = new QLineEdit(this);
  m_code->setEchoMode(QLineEdit::Password);
  m_connectPort = new QSpinBox(this);
  m_connectPort->setRange(1, 65535);
  m_connectPort->setValue(5555);
  layout->addRow(tr("Phone IP"), m_host);
  layout->addRow(tr("Pairing port"), m_pairPort);
  layout->addRow(tr("Pairing code"), m_code);
  layout->addRow(tr("Connect port"), m_connectPort);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    const auto planned = plan();
    if (!planned.ok) {
      QMessageBox::warning(
          this, tr("Pair wireless debugging"), tr("Enter a private LAN address, both ports, and the pairing code.")
      );
      return;
    }
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PairPlan PairingDialog::plan() const
{
  return planWirelessPair(m_host->text(), m_pairPort->value(), m_code->text(), m_connectPort->value());
}

} // namespace deskflow::phone
