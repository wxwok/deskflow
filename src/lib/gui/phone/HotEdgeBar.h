/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

namespace deskflow::phone {

//! Thin online-only hint on the registered edge. Entering it shows that phone.
class HotEdgeBar : public QWidget
{
  Q_OBJECT

public:
  explicit HotEdgeBar(const QString &phoneId, QWidget *parent = nullptr);

  void place(const QRect &screenRect);
  QString phoneId() const
  {
    return m_phoneId;
  }

Q_SIGNALS:
  void entered(const QString &phoneId, double alongFraction);

protected:
  void enterEvent(QEnterEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  QString m_phoneId;
};

} // namespace deskflow::phone
