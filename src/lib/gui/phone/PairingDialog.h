/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/phone/PhoneTypes.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;

namespace deskflow::phone {

class PairingDialog : public QDialog
{
  Q_OBJECT

public:
  explicit PairingDialog(QWidget *parent = nullptr);

  //! Validated plan. The pairing code is not written to settings.
  PairPlan plan() const;

private:
  QLineEdit *m_host = nullptr;
  QSpinBox *m_pairPort = nullptr;
  QLineEdit *m_code = nullptr;
  QSpinBox *m_connectPort = nullptr;
};

} // namespace deskflow::phone
