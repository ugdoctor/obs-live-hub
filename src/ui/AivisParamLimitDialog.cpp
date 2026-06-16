/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "AivisParamLimitDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

AivisParamLimitDialog::AivisParamLimitDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub AivisSpeechモデル制限");
	setMinimumWidth(380);

	// QDoubleSpinBox を生成するヘルパー
	auto makeSpin = [this](double lo, double hi, double step, int decimals) -> QDoubleSpinBox * {
		auto *sb = new QDoubleSpinBox(this);
		sb->setRange(lo, hi);
		sb->setSingleStep(step);
		sb->setDecimals(decimals);
		sb->setFixedWidth(80);
		return sb;
	};

	// min ～ max ペアウィジェット生成
	auto makeRange = [this](QDoubleSpinBox *mn, QDoubleSpinBox *mx) -> QWidget * {
		auto *w = new QWidget(this);
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(mn);
		h->addWidget(new QLabel("〜", this));
		h->addWidget(mx);
		h->addStretch();
		return w;
	};

	speedMin_       = makeSpin(0.01, 10.0, 0.1,  2);
	speedMax_       = makeSpin(0.01, 10.0, 0.1,  2);
	pitchMin_       = makeSpin(-2.0,  2.0, 0.01, 3);
	pitchMax_       = makeSpin(-2.0,  2.0, 0.01, 3);
	intonationMin_  = makeSpin(0.0,  10.0, 0.1,  2);
	intonationMax_  = makeSpin(0.0,  10.0, 0.1,  2);
	volumeScaleMin_ = makeSpin(0.0,  10.0, 0.1,  2);
	volumeScaleMax_ = makeSpin(0.0,  10.0, 0.1,  2);
	emotionMin_     = makeSpin(0.0,  10.0, 0.1,  2);
	emotionMax_     = makeSpin(0.0,  10.0, 0.1,  2);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *note = new QLabel(
		"視聴者が [olh] コマンドで指定できるパラメータの上下限を設定します。\n"
		"範囲外の値は自動的にクランプされます。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	auto *form = new QFormLayout();
	form->setSpacing(8);
	form->addRow("話速 (aivis_speed):",         makeRange(speedMin_,       speedMax_));
	form->addRow("音高 (aivis_pitch):",          makeRange(pitchMin_,       pitchMax_));
	form->addRow("抑揚 (aivis_intonation):",     makeRange(intonationMin_,  intonationMax_));
	form->addRow("音量倍率 (aivis_volume):",     makeRange(volumeScaleMin_, volumeScaleMax_));
	form->addRow("感情表現 (aivis_emotion):",    makeRange(emotionMin_,     emotionMax_));

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(note);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addSpacing(8);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this,
	                 &AivisParamLimitDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void AivisParamLimitDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	speedMin_->setValue(cfg.aivisSpeedMin);
	speedMax_->setValue(cfg.aivisSpeedMax);
	pitchMin_->setValue(cfg.aivisPitchMin);
	pitchMax_->setValue(cfg.aivisPitchMax);
	intonationMin_->setValue(cfg.aivisIntonationMin);
	intonationMax_->setValue(cfg.aivisIntonationMax);
	volumeScaleMin_->setValue(cfg.aivisVolumeScaleMin);
	volumeScaleMax_->setValue(cfg.aivisVolumeScaleMax);
	emotionMin_->setValue(cfg.aivisEmotionMin);
	emotionMax_->setValue(cfg.aivisEmotionMax);
}

void AivisParamLimitDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.aivisSpeedMin       = static_cast<float>(speedMin_->value());
	cfg.aivisSpeedMax       = static_cast<float>(speedMax_->value());
	cfg.aivisPitchMin       = static_cast<float>(pitchMin_->value());
	cfg.aivisPitchMax       = static_cast<float>(pitchMax_->value());
	cfg.aivisIntonationMin  = static_cast<float>(intonationMin_->value());
	cfg.aivisIntonationMax  = static_cast<float>(intonationMax_->value());
	cfg.aivisVolumeScaleMin = static_cast<float>(volumeScaleMin_->value());
	cfg.aivisVolumeScaleMax = static_cast<float>(volumeScaleMax_->value());
	cfg.aivisEmotionMin     = static_cast<float>(emotionMin_->value());
	cfg.aivisEmotionMax     = static_cast<float>(emotionMax_->value());
	cfg.save();
}

void AivisParamLimitDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
