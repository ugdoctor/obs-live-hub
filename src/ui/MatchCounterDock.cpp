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

#include "MatchCounterDock.hpp"
#include "core/PluginConfig.hpp"

#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

MatchCounterDock::MatchCounterDock(QWidget *parent) : QDockWidget("勝敗数カウンター", parent)
{
	container_ = new QWidget(this);
	auto *mainLayout = new QVBoxLayout(container_);
	mainLayout->setContentsMargins(6, 6, 6, 6);
	mainLayout->setSpacing(8);

	// ── 勝敗カウント
	auto *countGroup = new QGroupBox("勝敗カウント", container_);
	auto *countForm  = new QFormLayout(countGroup);
	countForm->setSpacing(6);

	winsSpin_ = new QSpinBox(container_);
	winsSpin_->setRange(0, 99999);
	auto *winRow  = new QWidget(container_);
	auto *winRowH = new QHBoxLayout(winRow);
	winRowH->setContentsMargins(0, 0, 0, 0);
	auto *winPlusBtn  = new QPushButton("+1", container_);
	auto *winMinusBtn = new QPushButton("-1", container_);
	winRowH->addWidget(winsSpin_);
	winRowH->addWidget(winPlusBtn);
	winRowH->addWidget(winMinusBtn);
	countForm->addRow("勝利数:", winRow);

	lossesSpin_ = new QSpinBox(container_);
	lossesSpin_->setRange(0, 99999);
	auto *lossRow  = new QWidget(container_);
	auto *lossRowH = new QHBoxLayout(lossRow);
	lossRowH->setContentsMargins(0, 0, 0, 0);
	auto *lossPlusBtn  = new QPushButton("+1", container_);
	auto *lossMinusBtn = new QPushButton("-1", container_);
	lossRowH->addWidget(lossesSpin_);
	lossRowH->addWidget(lossPlusBtn);
	lossRowH->addWidget(lossMinusBtn);
	countForm->addRow("敗北数:", lossRow);

	countForm->addRow(new QLabel(
		"※ 数値欄を直接編集して確定すると、直近の履歴はリセットされます"));

	// ── 目標モード
	auto *targetGroup = new QGroupBox("目標設定", container_);
	auto *targetLayout = new QVBoxLayout(targetGroup);
	targetLayout->setSpacing(4);

	targetNoneRadio_ = new QRadioButton("なし", container_);
	targetWinsRadio_ = new QRadioButton("勝利数目標", container_);
	targetRateRadio_ = new QRadioButton("勝率目標", container_);
	targetModeGroup_ = new QButtonGroup(this);
	targetModeGroup_->addButton(targetNoneRadio_, 0);
	targetModeGroup_->addButton(targetWinsRadio_, 1);
	targetModeGroup_->addButton(targetRateRadio_, 2);
	targetLayout->addWidget(targetNoneRadio_);

	auto *winsTargetRow  = new QWidget(container_);
	auto *winsTargetH    = new QHBoxLayout(winsTargetRow);
	winsTargetH->setContentsMargins(0, 0, 0, 0);
	targetWinsSpin_ = new QSpinBox(container_);
	targetWinsSpin_->setRange(0, 99999);
	winsTargetH->addWidget(targetWinsRadio_);
	winsTargetH->addWidget(targetWinsSpin_, 1);
	targetLayout->addWidget(winsTargetRow);

	auto *rateTargetRow = new QWidget(container_);
	auto *rateTargetH   = new QHBoxLayout(rateTargetRow);
	rateTargetH->setContentsMargins(0, 0, 0, 0);
	targetWinRateSpin_ = new QDoubleSpinBox(container_);
	targetWinRateSpin_->setRange(0.0, 100.0);
	targetWinRateSpin_->setDecimals(1);
	targetWinRateSpin_->setSuffix(" %");
	rateTargetH->addWidget(targetRateRadio_);
	rateTargetH->addWidget(targetWinRateSpin_, 1);
	targetLayout->addWidget(rateTargetRow);

	// ── メモ
	auto *memoGroup = new QGroupBox("配信画面メモ", container_);
	auto *memoLayout = new QVBoxLayout(memoGroup);
	memoEdit_ = new QLineEdit(container_);
	memoEdit_->setPlaceholderText("例: 本日の目標：ダイヤ昇格戦！");
	memoLayout->addWidget(memoEdit_);

	// ── 全リセット
	resetBtn_ = new QPushButton("すべてリセット", container_);

	mainLayout->addWidget(countGroup);
	mainLayout->addWidget(targetGroup);
	mainLayout->addWidget(memoGroup);
	mainLayout->addWidget(resetBtn_);
	mainLayout->addStretch();

	setWidget(container_);

	// ── シグナル接続
	connect(winPlusBtn,   &QPushButton::clicked, this, &MatchCounterDock::onWinPlus);
	connect(winMinusBtn,  &QPushButton::clicked, this, &MatchCounterDock::onWinMinus);
	connect(lossPlusBtn,  &QPushButton::clicked, this, &MatchCounterDock::onLossPlus);
	connect(lossMinusBtn, &QPushButton::clicked, this, &MatchCounterDock::onLossMinus);
	// editingFinished はユーザーの直接編集確定時のみ発火する（setValue()単独では発火しない）
	connect(winsSpin_,   &QSpinBox::editingFinished, this,
	        &MatchCounterDock::onWinsEditingFinished);
	connect(lossesSpin_, &QSpinBox::editingFinished, this,
	        &MatchCounterDock::onLossesEditingFinished);

	connect(targetModeGroup_, &QButtonGroup::idClicked, this,
	        [this](int) { onTargetModeChanged(); });
	connect(targetWinsSpin_,    &QSpinBox::editingFinished, this,
	        &MatchCounterDock::onTargetValueChanged);
	connect(targetWinRateSpin_, &QDoubleSpinBox::editingFinished, this,
	        &MatchCounterDock::onTargetValueChanged);

	// メモは「リアルタイムに通知」だが、1文字ごとの保存/ブロードキャストを避けるため
	// 短いデバウンスを挟む（タイピング中の連続ディスク書き込み・WS送信を抑制）
	memoDebounce_ = new QTimer(this);
	memoDebounce_->setSingleShot(true);
	memoDebounce_->setInterval(250);
	connect(memoEdit_, &QLineEdit::textEdited, this, &MatchCounterDock::onMemoTextEdited);
	connect(memoDebounce_, &QTimer::timeout, this, &MatchCounterDock::onMemoCommit);

	connect(resetBtn_, &QPushButton::clicked, this, &MatchCounterDock::onResetClicked);

	loadFromConfig();
}

void MatchCounterDock::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();

	winsSpin_->blockSignals(true);
	winsSpin_->setValue(cfg.matchWins);
	winsSpin_->blockSignals(false);

	lossesSpin_->blockSignals(true);
	lossesSpin_->setValue(cfg.matchLosses);
	lossesSpin_->blockSignals(false);

	targetWinsSpin_->blockSignals(true);
	targetWinsSpin_->setValue(cfg.matchTargetWins);
	targetWinsSpin_->blockSignals(false);

	targetWinRateSpin_->blockSignals(true);
	targetWinRateSpin_->setValue(cfg.matchTargetWinRate);
	targetWinRateSpin_->blockSignals(false);

	QRadioButton *toCheck = targetNoneRadio_;
	if (cfg.matchTargetMode == 1) toCheck = targetWinsRadio_;
	else if (cfg.matchTargetMode == 2) toCheck = targetRateRadio_;
	targetModeGroup_->blockSignals(true);
	toCheck->setChecked(true);
	targetModeGroup_->blockSignals(false);
	updateTargetInputEnablement();

	memoEdit_->blockSignals(true);
	memoEdit_->setText(QString::fromStdString(cfg.matchMemo));
	memoEdit_->blockSignals(false);
}

void MatchCounterDock::refresh()
{
	loadFromConfig();
}

void MatchCounterDock::updateTargetInputEnablement()
{
	const int mode = targetModeGroup_->checkedId();
	targetWinsSpin_->setEnabled(mode == 1);
	targetWinRateSpin_->setEnabled(mode == 2);
}

// ── 履歴操作（最大10件、古い→新しい順） ──────────────────────────────────

void MatchCounterDock::pushHistory(const char *result)
{
	auto &cfg = PluginConfig::instance();
	cfg.matchHistory.push_back(result);
	if (cfg.matchHistory.size() > 10)
		cfg.matchHistory.erase(cfg.matchHistory.begin());
}

void MatchCounterDock::popHistoryIfLast(const char *result)
{
	auto &cfg = PluginConfig::instance();
	if (!cfg.matchHistory.empty() && cfg.matchHistory.back() == result)
		cfg.matchHistory.pop_back();
}

// ── カウント操作 ──────────────────────────────────────────────────────────

void MatchCounterDock::onWinPlus()
{
	auto &cfg = PluginConfig::instance();
	cfg.matchWins++;
	pushHistory("W");
	winsSpin_->blockSignals(true);
	winsSpin_->setValue(cfg.matchWins);
	winsSpin_->blockSignals(false);
	saveAndBroadcast(false);
}

void MatchCounterDock::onWinMinus()
{
	auto &cfg = PluginConfig::instance();
	if (cfg.matchWins > 0)
		cfg.matchWins--;
	popHistoryIfLast("W");
	winsSpin_->blockSignals(true);
	winsSpin_->setValue(cfg.matchWins);
	winsSpin_->blockSignals(false);
	saveAndBroadcast(false);
}

void MatchCounterDock::onLossPlus()
{
	auto &cfg = PluginConfig::instance();
	cfg.matchLosses++;
	pushHistory("L");
	lossesSpin_->blockSignals(true);
	lossesSpin_->setValue(cfg.matchLosses);
	lossesSpin_->blockSignals(false);
	saveAndBroadcast(false);
}

void MatchCounterDock::onLossMinus()
{
	auto &cfg = PluginConfig::instance();
	if (cfg.matchLosses > 0)
		cfg.matchLosses--;
	popHistoryIfLast("L");
	lossesSpin_->blockSignals(true);
	lossesSpin_->setValue(cfg.matchLosses);
	lossesSpin_->blockSignals(false);
	saveAndBroadcast(false);
}

void MatchCounterDock::onWinsEditingFinished()
{
	auto &cfg = PluginConfig::instance();
	if (winsSpin_->value() == cfg.matchWins)
		return;
	cfg.matchWins = winsSpin_->value();
	cfg.matchHistory.clear(); // 手入力で整合性が崩れるため履歴をクリア
	saveAndBroadcast(true);
}

void MatchCounterDock::onLossesEditingFinished()
{
	auto &cfg = PluginConfig::instance();
	if (lossesSpin_->value() == cfg.matchLosses)
		return;
	cfg.matchLosses = lossesSpin_->value();
	cfg.matchHistory.clear(); // 手入力で整合性が崩れるため履歴をクリア
	saveAndBroadcast(true);
}

void MatchCounterDock::onTargetModeChanged()
{
	updateTargetInputEnablement();
	auto &cfg = PluginConfig::instance();
	cfg.matchTargetMode = targetModeGroup_->checkedId();
	saveAndBroadcast(false);
}

void MatchCounterDock::onTargetValueChanged()
{
	auto &cfg = PluginConfig::instance();
	cfg.matchTargetWins    = targetWinsSpin_->value();
	cfg.matchTargetWinRate = targetWinRateSpin_->value();
	saveAndBroadcast(false);
}

void MatchCounterDock::onMemoTextEdited()
{
	memoDebounce_->start(); // 連続入力中は再スタートし、入力が止まってから確定させる
}

void MatchCounterDock::onMemoCommit()
{
	auto &cfg = PluginConfig::instance();
	const std::string text = memoEdit_->text().toStdString();
	if (text == cfg.matchMemo)
		return;
	cfg.matchMemo = text;
	saveAndBroadcast(false);
}

void MatchCounterDock::onResetClicked()
{
	const auto btn = QMessageBox::question(
		this, "確認",
		"勝敗数・目標値・メモ・履歴をすべて初期値にリセットします。よろしいですか？",
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (btn != QMessageBox::Yes)
		return;

	auto &cfg = PluginConfig::instance();
	cfg.matchWins          = 0;
	cfg.matchLosses        = 0;
	cfg.matchHistory.clear();
	cfg.matchTargetWins    = 0;
	cfg.matchTargetWinRate = 50.0;
	cfg.matchTargetMode    = 0;
	cfg.matchMemo.clear();

	loadFromConfig();
	saveAndBroadcast(true);
}

void MatchCounterDock::saveAndBroadcast(bool isReset)
{
	PluginConfig::instance().save();
	emit matchDataChanged(isReset);
}
