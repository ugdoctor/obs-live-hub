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

#include "XPostDock.hpp"
#include "core/PluginConfig.hpp"

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

XPostDock::XPostDock(QWidget *parent) : QDockWidget(parent)
{
	setObjectName("XPostDock");

	auto *container = new QWidget(this);
	setWidget(container);

	// ── テンプレート・本文 ──
	templateCombo_ = new QComboBox(container);
	textEdit_      = new QPlainTextEdit(container);
	textEdit_->setPlaceholderText("投稿テキストを入力...");
	textEdit_->setMinimumHeight(80);

	// ── リンクプラットフォーム ──
	linkPlatformCombo_ = new QComboBox(container);
	linkPlatformCombo_->addItem("なし",    "none");
	linkPlatformCombo_->addItem("Twitch",  "twitch");
	linkPlatformCombo_->addItem("YouTube", "youtube");
	linkPlatformCombo_->addItem("その他",  "other");

	// ── 投稿ボタン ──
	postButton_ = new QPushButton("投稿する（API）", container);

	// ── 配信開始時の自動表示モード（ラジオボタン3択） ──
	auto *autoGroup  = new QGroupBox("配信開始時の自動表示", container);
	radioOff_    = new QRadioButton("自動表示しない",                          autoGroup);
	radioApi_    = new QRadioButton("API投稿確認ダイアログを表示",              autoGroup);
	radioManual_ = new QRadioButton("手動投稿ダイアログ（Web Intent）を表示",  autoGroup);

	// QButtonGroup でグループ化（排他制御）
	auto *btnGroup = new QButtonGroup(autoGroup);
	btnGroup->addButton(radioOff_,    0);
	btnGroup->addButton(radioApi_,    1);
	btnGroup->addButton(radioManual_, 2);

	auto *radioLayout = new QVBoxLayout(autoGroup);
	radioLayout->setSpacing(2);
	radioLayout->addWidget(radioOff_);
	radioLayout->addWidget(radioApi_);
	radioLayout->addWidget(radioManual_);

	// ── レイアウト組み立て ──
	auto *tmplRow = new QHBoxLayout();
	tmplRow->addWidget(new QLabel("テンプレート:", container));
	tmplRow->addWidget(templateCombo_, 1);

	auto *linkRow = new QHBoxLayout();
	linkRow->addWidget(new QLabel("リンク:", container));
	linkRow->addWidget(linkPlatformCombo_, 1);

	auto *layout = new QVBoxLayout(container);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	layout->addLayout(tmplRow);
	layout->addWidget(textEdit_, 1);
	layout->addLayout(linkRow);
	layout->addWidget(postButton_);
	layout->addWidget(autoGroup);

	// ── シグナル接続 ──
	QObject::connect(templateCombo_,
	                 QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, &XPostDock::onTemplateChanged);
	QObject::connect(postButton_, &QPushButton::clicked,
	                 this, &XPostDock::onPostClicked);
	QObject::connect(btnGroup, &QButtonGroup::idClicked,
	                 this, [this](int) { onAutoPostModeChanged(); });

	populateTemplates();

	// PluginConfig の値をラジオボタンに反映
	const int mode = PluginConfig::instance().xAutoPostOnStreamStart;
	if      (mode == 1) radioApi_   ->setChecked(true);
	else if (mode == 2) radioManual_->setChecked(true);
	else                radioOff_   ->setChecked(true);
}

void XPostDock::populateTemplates()
{
	const QSignalBlocker blocker(templateCombo_);
	templateCombo_->clear();
	const auto &cfg = PluginConfig::instance();
	for (const auto &t : cfg.xTemplates)
		templateCombo_->addItem(QString::fromStdString(t.name));
	if (templateCombo_->count() > 0) {
		const int def = cfg.xDefaultTemplateIndex;
		templateCombo_->setCurrentIndex(
			(def >= 0 && def < templateCombo_->count()) ? def : 0);
		onTemplateChanged(templateCombo_->currentIndex());
	}
}

void XPostDock::refresh()
{
	populateTemplates();

	const int mode = PluginConfig::instance().xAutoPostOnStreamStart;
	if      (mode == 1) radioApi_   ->setChecked(true);
	else if (mode == 2) radioManual_->setChecked(true);
	else                radioOff_   ->setChecked(true);
}

void XPostDock::onTemplateChanged(int index)
{
	const auto &cfg = PluginConfig::instance();
	if (index < 0 || index >= static_cast<int>(cfg.xTemplates.size()))
		return;
	const auto &t   = cfg.xTemplates[static_cast<size_t>(index)];
	textEdit_->setPlainText(QString::fromStdString(t.text));
	const int lpIdx = linkPlatformCombo_->findData(
		QString::fromStdString(t.linkPlatform));
	linkPlatformCombo_->setCurrentIndex(lpIdx >= 0 ? lpIdx : 0);
}

void XPostDock::onPostClicked()
{
	emit postRequested(textEdit_->toPlainText());
}

void XPostDock::onAutoPostModeChanged()
{
	int mode = 0;
	if      (radioApi_   ->isChecked()) mode = 1;
	else if (radioManual_->isChecked()) mode = 2;

	PluginConfig::instance().xAutoPostOnStreamStart = mode;
	PluginConfig::instance().save();
}
