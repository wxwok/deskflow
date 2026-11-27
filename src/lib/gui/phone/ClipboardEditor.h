/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace deskflow::phone {

//! Plain-text editor that uses the computer IME, including Chinese input.
class ClipboardEditor : public QDialog
{
  Q_OBJECT

public:
  explicit ClipboardEditor(QWidget *parent = nullptr);

  void setPhoneText(const QString &text);
  QString text() const;

Q_SIGNALS:
  void loadRequested();
  void sendRequested(const QString &text);
  void sendPasteRequested(const QString &text);

private:
  QPlainTextEdit *m_text = nullptr;
};

} // namespace deskflow::phone
