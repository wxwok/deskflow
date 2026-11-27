/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/phone/ClipboardEditor.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace deskflow::phone {

ClipboardEditor::ClipboardEditor(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Phone clipboard"));
  setModal(false);
  auto *layout = new QVBoxLayout(this);
  auto *hint = new QLabel(
      tr("Plain text only. Type here with the computer input method, including Chinese. "
         "Send puts the text on the phone clipboard. Send and paste also injects a paste into the phone window."),
      this
  );
  hint->setWordWrap(true);
  layout->addWidget(hint);
  m_text = new QPlainTextEdit(this);
  m_text->setPlaceholderText(tr("Draft text for the phone"));
  layout->addWidget(m_text);
  auto *load = new QPushButton(tr("Load phone clipboard"), this);
  auto *send = new QPushButton(tr("Send to phone clipboard"), this);
  auto *sendPaste = new QPushButton(tr("Send and paste"), this);
  layout->addWidget(load);
  layout->addWidget(send);
  layout->addWidget(sendPaste);
  connect(load, &QPushButton::clicked, this, &ClipboardEditor::loadRequested);
  connect(send, &QPushButton::clicked, this, [this]() { Q_EMIT sendRequested(m_text->toPlainText()); });
  connect(sendPaste, &QPushButton::clicked, this, [this]() { Q_EMIT sendPasteRequested(m_text->toPlainText()); });
  resize(420, 320);
}

void ClipboardEditor::setPhoneText(const QString &text)
{
  m_text->setPlainText(text);
}

QString ClipboardEditor::text() const
{
  return m_text->toPlainText();
}

} // namespace deskflow::phone
