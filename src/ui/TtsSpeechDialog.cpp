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

#include "TtsSpeechDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QHBoxLayout>

TtsSpeechDialog::TtsSpeechDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 読み上げ設定");
	setMinimumWidth(380);

	enabledCheck_ = new QCheckBox("読み上げを有効にする", this);

	volumeSlider_ = new QSlider(Qt::Horizontal, this);
	volumeSlider_->setRange(0, 100);
	volumeLabel_ = new QLabel(this);
	volumeLabel_->setMinimumWidth(40);

	rateSlider_ = new QSlider(Qt::Horizontal, this);
	rateSlider_->setRange(50, 200);
	rateLabel_ = new QLabel(this);
	rateLabel_->setMinimumWidth(40);

	pitchSlider_ = new QSlider(Qt::Horizontal, this);
	pitchSlider_->setRange(0, 200);
	pitchLabel_ = new QLabel(this);
	pitchLabel_->setMinimumWidth(40);

	readUsernameCheck_ = new QCheckBox("ユーザー名を読み上げる", this);

	maxLengthSpin_ = new QSpinBox(this);
	maxLengthSpin_->setRange(0, 500);
	maxLengthSpin_->setSuffix(" 文字");
	maxLengthSpin_->setSpecialValueText("無制限");

	twitchCheck_  = new QCheckBox("Twitch コメントを読み上げる", this);
	youtubeCheck_ = new QCheckBox("YouTube コメントを読み上げる", this);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto makeRow = [this](QSlider *sl, QLabel *lb) -> QWidget * {
		auto *w = new QWidget(this);
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(sl, 1);
		h->addWidget(lb);
		return w;
	};

	auto *form = new QFormLayout(this);
	form->setSpacing(8);
	form->addRow("", enabledCheck_);
	form->addRow("音量:", makeRow(volumeSlider_, volumeLabel_));
	form->addRow("速度:", makeRow(rateSlider_, rateLabel_));
	form->addRow("ピッチ:", makeRow(pitchSlider_, pitchLabel_));
	form->addRow("", readUsernameCheck_);
	form->addRow("最大文字数:", maxLengthSpin_);
	form->addRow("", twitchCheck_);
	form->addRow("", youtubeCheck_);
	form->addRow(buttonBox_);

	QObject::connect(volumeSlider_, &QSlider::valueChanged, volumeLabel_, [this](int v) {
		volumeLabel_->setText(QString::number(v) + "%");
	});
	QObject::connect(rateSlider_, &QSlider::valueChanged, rateLabel_, [this](int v) {
		rateLabel_->setText(QString::number(v / 100.0, 'f', 2) + "x");
	});
	QObject::connect(pitchSlider_, &QSlider::valueChanged, pitchLabel_, [this](int v) {
		pitchLabel_->setText(QString::number(v / 100.0, 'f', 2));
	});

	connect(buttonBox_, &QDialogButtonBox::accepted, this, &TtsSpeechDialog::accept);
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void TtsSpeechDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	enabledCheck_->setChecked(cfg.ttsEnabled);
	volumeSlider_->setValue(static_cast<int>(cfg.ttsVolume * 100.0f + 0.5f));
	rateSlider_->setValue(static_cast<int>(cfg.ttsRate * 100.0f + 0.5f));
	pitchSlider_->setValue(static_cast<int>(cfg.ttsPitch * 100.0f + 0.5f));
	readUsernameCheck_->setChecked(cfg.ttsReadUsername);
	maxLengthSpin_->setValue(cfg.ttsMaxLength);
	twitchCheck_->setChecked(cfg.ttsTwitch);
	youtubeCheck_->setChecked(cfg.ttsYoutube);
}

void TtsSpeechDialog::saveToConfig()
{
	auto &cfg         = PluginConfig::instance();
	cfg.ttsEnabled      = enabledCheck_->isChecked();
	cfg.ttsVolume       = volumeSlider_->value() / 100.0f;
	cfg.ttsRate         = rateSlider_->value() / 100.0f;
	cfg.ttsPitch        = pitchSlider_->value() / 100.0f;
	cfg.ttsReadUsername = readUsernameCheck_->isChecked();
	cfg.ttsMaxLength    = maxLengthSpin_->value();
	cfg.ttsTwitch       = twitchCheck_->isChecked();
	cfg.ttsYoutube      = youtubeCheck_->isChecked();
	cfg.save();
}

void TtsSpeechDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
