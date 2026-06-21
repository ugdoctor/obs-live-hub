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

#include "ConversationOverlayDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QButtonGroup>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

ConversationOverlayDialog::ConversationOverlayDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub チャットオーバーレイ設定");
	setMinimumWidth(380);

	auto *note = new QLabel(
		"TTS読み上げ開始タイミングでバブルを表示するチャット風オーバーレイの設定です。\n"
		"OBSブラウザソースで conversation_overlay.html を開いて使います。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	maxBubblesSpin_ = new QSpinBox(this);
	maxBubblesSpin_->setRange(1, 5);
	maxBubblesSpin_->setSuffix(" 件");
	maxBubblesSpin_->setFixedWidth(80);

	hOffsetSpin_ = new QSpinBox(this);
	hOffsetSpin_->setRange(0, 300);
	hOffsetSpin_->setSuffix(" px");
	hOffsetSpin_->setFixedWidth(80);
	hOffsetSpin_->setToolTip("左バブルの右余白・右バブルの左余白。大きいほど内側に寄る（0=全幅）");

	auto *zigzagGroup  = new QGroupBox("ジグザグ配置モード", this);
	auto *radioLayout  = new QVBoxLayout(zigzagGroup);
	alternateRadio_    = new QRadioButton("交互　（発言順に左右交互）", zigzagGroup);
	fixedRadio_        = new QRadioButton("固定　（同じユーザーは常に同じ側）", zigzagGroup);
	auto *btnGroup     = new QButtonGroup(this);
	btnGroup->addButton(alternateRadio_);
	btnGroup->addButton(fixedRadio_);
	radioLayout->addWidget(alternateRadio_);
	radioLayout->addWidget(fixedRadio_);

	auto *form = new QFormLayout();
	form->setSpacing(10);
	form->addRow("表示件数（上限 5 件）:", maxBubblesSpin_);
	form->addRow("左右の内側余白:", hOffsetSpin_);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(note);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addWidget(zigzagGroup);
	layout->addSpacing(4);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this,
	                 &ConversationOverlayDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void ConversationOverlayDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	maxBubblesSpin_->setValue(cfg.conversationMaxBubbles);
	hOffsetSpin_->setValue(cfg.conversationHorizontalOffset);
	if (cfg.conversationZigzagMode == "fixed")
		fixedRadio_->setChecked(true);
	else
		alternateRadio_->setChecked(true);
}

void ConversationOverlayDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.conversationMaxBubbles        = maxBubblesSpin_->value();
	cfg.conversationHorizontalOffset  = hOffsetSpin_->value();
	cfg.conversationZigzagMode        = fixedRadio_->isChecked() ? "fixed" : "alternate";
	cfg.save();
}

void ConversationOverlayDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
