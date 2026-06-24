/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "XPostConfirmDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

XPostConfirmDialog::XPostConfirmDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("X 投稿確認");
	setMinimumWidth(480);

	auto *titleLabel = new QLabel(
		"配信を開始しました。X(Twitter)に投稿しますか？", this);
	titleLabel->setWordWrap(true);

	textEdit_          = new QPlainTextEdit(this);
	textEdit_->setMinimumHeight(100);
	linkPlatformCombo_ = new QComboBox(this);
	linkPlatformCombo_->addItem("なし",    "none");
	linkPlatformCombo_->addItem("Twitch",  "twitch");
	linkPlatformCombo_->addItem("YouTube", "youtube");
	linkPlatformCombo_->addItem("その他",  "other");

	auto *form = new QFormLayout();
	form->addRow("本文:",   textEdit_);
	form->addRow("リンク:", linkPlatformCombo_);

	buttonBox_ = new QDialogButtonBox(this);
	buttonBox_->addButton("投稿する",     QDialogButtonBox::AcceptRole);
	buttonBox_->addButton("キャンセル",   QDialogButtonBox::RejectRole);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(titleLabel);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addSpacing(8);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted,
	                 this, &QDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected,
	                 this, &QDialog::reject);

	loadDefaultTemplate();
}

void XPostConfirmDialog::loadDefaultTemplate()
{
	const auto &cfg = PluginConfig::instance();
	if (cfg.xTemplates.empty())
		return;
	const int idx =
		(cfg.xDefaultTemplateIndex >= 0 &&
		 cfg.xDefaultTemplateIndex < static_cast<int>(cfg.xTemplates.size()))
			? cfg.xDefaultTemplateIndex
			: 0;
	const auto &t  = cfg.xTemplates[static_cast<size_t>(idx)];
	textEdit_->setPlainText(QString::fromStdString(t.text));
	const int lpIdx = linkPlatformCombo_->findData(
		QString::fromStdString(t.linkPlatform));
	linkPlatformCombo_->setCurrentIndex(lpIdx >= 0 ? lpIdx : 0);
}

QString XPostConfirmDialog::postText() const
{
	return textEdit_->toPlainText();
}
