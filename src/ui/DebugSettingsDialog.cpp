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

#include "DebugSettingsDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QLabel>
#include <QVBoxLayout>

DebugSettingsDialog::DebugSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub デバッグ設定");
	setMinimumWidth(320);

	auto *note = new QLabel(
		"debug.html の各パネルの表示/非表示を設定します。\n"
		"OK を押すと設定が保存され、接続中の debug.html に即時反映されます。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	showConnectionCheck_    = new QCheckBox("接続状態パネルを表示", this);
	showTtsCheck_           = new QCheckBox("TTS設定パネルを表示", this);
	showQuotaCheck_         = new QCheckBox("クォータパネルを表示", this);
	showVoteCheck_          = new QCheckBox("投票状態パネルを表示", this);
	showLogCheck_           = new QCheckBox("broadcastログを表示", this);
	showCommentDetailCheck_ = new QCheckBox("最新コメント詳細を表示", this);
	showEffectCheck_        = new QCheckBox("エフェクト状態パネルを表示", this);
	showPointCheck_         = new QCheckBox("ポイント状態パネルを表示", this);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(note);
	layout->addSpacing(8);
	layout->addWidget(showConnectionCheck_);
	layout->addWidget(showTtsCheck_);
	layout->addWidget(showQuotaCheck_);
	layout->addWidget(showVoteCheck_);
	layout->addWidget(showLogCheck_);
	layout->addWidget(showCommentDetailCheck_);
	layout->addWidget(showEffectCheck_);
	layout->addWidget(showPointCheck_);
	layout->addSpacing(8);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this,
			 &DebugSettingsDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void DebugSettingsDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	showConnectionCheck_->setChecked(cfg.debugShowConnection);
	showTtsCheck_->setChecked(cfg.debugShowTts);
	showQuotaCheck_->setChecked(cfg.debugShowQuota);
	showVoteCheck_->setChecked(cfg.debugShowVote);
	showLogCheck_->setChecked(cfg.debugShowLog);
	showCommentDetailCheck_->setChecked(cfg.debugShowCommentDetail);
	showEffectCheck_->setChecked(cfg.debugShowEffect);
	showPointCheck_->setChecked(cfg.debugShowPoint);
}

void DebugSettingsDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.debugShowConnection    = showConnectionCheck_->isChecked();
	cfg.debugShowTts           = showTtsCheck_->isChecked();
	cfg.debugShowQuota         = showQuotaCheck_->isChecked();
	cfg.debugShowVote          = showVoteCheck_->isChecked();
	cfg.debugShowLog           = showLogCheck_->isChecked();
	cfg.debugShowCommentDetail = showCommentDetailCheck_->isChecked();
	cfg.debugShowEffect        = showEffectCheck_->isChecked();
	cfg.debugShowPoint         = showPointCheck_->isChecked();
	cfg.save();
}

void DebugSettingsDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
