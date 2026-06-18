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

#include "EffectSettingsDialog.hpp"
#include "core/PluginConfig.hpp"
#include "modules/EffectManager.hpp"

#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

EffectSettingsDialog::EffectSettingsDialog(EffectManager *mgr, QWidget *parent)
	: QDialog(parent), mgr_(mgr)
{
	setWindowTitle("obs-live-hub エフェクト設定");
	setMinimumWidth(420);

	// ── 全体設定グループ ─────────────────────────────────────────────────
	auto *settingsGroup = new QGroupBox("全体設定", this);
	auto *formLayout    = new QFormLayout(settingsGroup);

	positionCombo_ = new QComboBox(this);
	positionCombo_->addItems({"center", "top", "bottom", "top-left", "top-right",
	                           "bottom-left", "bottom-right"});
	formLayout->addRow("デフォルト表示位置:", positionCombo_);

	sizeCombo_ = new QComboBox(this);
	sizeCombo_->addItems({"small", "medium", "large"});
	formLayout->addRow("デフォルトサイズ:", sizeCombo_);

	maxConcurrentSpin_ = new QSpinBox(this);
	maxConcurrentSpin_->setRange(1, 10);
	maxConcurrentSpin_->setValue(3);
	formLayout->addRow("同時表示上限数:", maxConcurrentSpin_);

	maxQueueSpin_ = new QSpinBox(this);
	maxQueueSpin_->setRange(1, 50);
	maxQueueSpin_->setValue(10);
	formLayout->addRow("キュー上限数:", maxQueueSpin_);

	auto *note = new QLabel(
		"エフェクトファイルは effects フォルダに配置してください。\n"
		"各エフェクトは effect.json + 画像ファイル（GIF/PNG/JPG）で構成します。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");
	formLayout->addRow(note);

	// ── エフェクト一覧グループ ──────────────────────────────────────────
	auto *listGroup  = new QGroupBox("読み込み済みエフェクト", this);
	auto *listLayout = new QVBoxLayout(listGroup);

	effectList_ = new QListWidget(this);
	effectList_->setMinimumHeight(120);
	listLayout->addWidget(effectList_);

	auto *btnLayout = new QHBoxLayout;
	openFolderBtn_  = new QPushButton("フォルダを開く", this);
	reloadBtn_      = new QPushButton("再読み込み",    this);
	btnLayout->addWidget(openFolderBtn_);
	btnLayout->addWidget(reloadBtn_);
	btnLayout->addStretch();
	listLayout->addLayout(btnLayout);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(settingsGroup);
	mainLayout->addWidget(listGroup);
	mainLayout->addWidget(buttonBox_);

	QObject::connect(openFolderBtn_, &QPushButton::clicked,
	                 this, &EffectSettingsDialog::onOpenFolder);
	QObject::connect(reloadBtn_, &QPushButton::clicked,
	                 this, &EffectSettingsDialog::onReload);
	QObject::connect(buttonBox_, &QDialogButtonBox::accepted,
	                 this, &EffectSettingsDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected,
	                 this, &QDialog::reject);

	loadFromConfig();
	refreshList();
}

void EffectSettingsDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	const int posIdx = positionCombo_->findText(
		QString::fromStdString(cfg.effectDefaultPosition));
	positionCombo_->setCurrentIndex(posIdx >= 0 ? posIdx : 0);

	const int sizeIdx = sizeCombo_->findText(
		QString::fromStdString(cfg.effectDefaultSize));
	sizeCombo_->setCurrentIndex(sizeIdx >= 0 ? sizeIdx : 1);

	maxConcurrentSpin_->setValue(cfg.effectMaxConcurrent);
	maxQueueSpin_->setValue(cfg.effectMaxQueue);
}

void EffectSettingsDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.effectDefaultPosition = positionCombo_->currentText().toStdString();
	cfg.effectDefaultSize     = sizeCombo_->currentText().toStdString();
	cfg.effectMaxConcurrent   = maxConcurrentSpin_->value();
	cfg.effectMaxQueue        = maxQueueSpin_->value();
	cfg.save();
}

void EffectSettingsDialog::refreshList()
{
	effectList_->clear();
	if (!mgr_ || mgr_->effects().empty()) {
		effectList_->addItem("（エフェクトなし）");
		return;
	}
	for (const auto &ef : mgr_->effects()) {
		effectList_->addItem(
			QString("%1  [%2: %3]  duration=%4ms")
				.arg(QString::fromStdString(ef.name))
				.arg(QString::fromStdString(ef.triggerType))
				.arg(QString::fromStdString(ef.triggerValue))
				.arg(ef.duration));
	}
}

void EffectSettingsDialog::onOpenFolder()
{
	if (!mgr_)
		return;
#ifdef _WIN32
	const QString dir = mgr_->effectsDir();
	QDir().mkpath(dir);
	ShellExecuteW(nullptr, L"open", dir.toStdWString().c_str(),
	              nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void EffectSettingsDialog::onReload()
{
	if (!mgr_)
		return;
	mgr_->loadEffects(mgr_->effectsDir());
	refreshList();
}

void EffectSettingsDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
