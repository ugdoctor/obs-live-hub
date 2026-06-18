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

#include "BouyomiParamLimitDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

BouyomiParamLimitDialog::BouyomiParamLimitDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 棒読みちゃんパラメータ制限");
	setMinimumWidth(380);

	auto makeSpin = [this](int lo, int hi) -> QSpinBox * {
		auto *sb = new QSpinBox(this);
		sb->setRange(lo, hi);
		sb->setFixedWidth(80);
		return sb;
	};

	auto makeRange = [this](QSpinBox *mn, QSpinBox *mx) -> QWidget * {
		auto *w = new QWidget(this);
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(mn);
		h->addWidget(new QLabel("〜", this));
		h->addWidget(mx);
		h->addStretch();
		return w;
	};

	volumeMin_ = makeSpin(0,    100);
	volumeMax_ = makeSpin(0,    100);
	speedMin_  = makeSpin(50,   300);
	speedMax_  = makeSpin(50,   300);
	toneMin_   = makeSpin(-100, 100);
	toneMax_   = makeSpin(-100, 100);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *note = new QLabel(
		"視聴者が [olh] コマンドで指定できる棒読みちゃんパラメータの上下限を設定します。\n"
		"-1（現在設定を使用）は常に許可されます。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	auto *form = new QFormLayout();
	form->setSpacing(8);
	form->addRow("音量 (bouyomi_volume):",  makeRange(volumeMin_, volumeMax_));
	form->addRow("速度 (bouyomi_speed):",   makeRange(speedMin_,  speedMax_));
	form->addRow("音程 (bouyomi_tone):",    makeRange(toneMin_,   toneMax_));

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(note);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addSpacing(8);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this,
	                 &BouyomiParamLimitDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void BouyomiParamLimitDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	volumeMin_->setValue(cfg.bouyomiVolumeMin);
	volumeMax_->setValue(cfg.bouyomiVolumeMax);
	speedMin_->setValue(cfg.bouyomiSpeedMin);
	speedMax_->setValue(cfg.bouyomiSpeedMax);
	toneMin_->setValue(cfg.bouyomiToneMin);
	toneMax_->setValue(cfg.bouyomiToneMax);
}

void BouyomiParamLimitDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.bouyomiVolumeMin = volumeMin_->value();
	cfg.bouyomiVolumeMax = volumeMax_->value();
	cfg.bouyomiSpeedMin  = speedMin_->value();
	cfg.bouyomiSpeedMax  = speedMax_->value();
	cfg.bouyomiToneMin   = toneMin_->value();
	cfg.bouyomiToneMax   = toneMax_->value();
	cfg.save();
}

void BouyomiParamLimitDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
