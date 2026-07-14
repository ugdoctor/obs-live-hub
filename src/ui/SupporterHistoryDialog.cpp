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

#include "SupporterHistoryDialog.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "core/PluginConfig.hpp"

SupporterHistoryDialog::SupporterHistoryDialog(QWidget *parent)
	: QDialog(parent, Qt::Window)
{
	setWindowTitle("サポーター履歴");
	setMinimumSize(860, 520);
	resize(1000, 600);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(8);
	mainLayout->setContentsMargins(12, 12, 12, 12);

	// ── 合計ラベル ──────────────────────────────────────────────────────────
	totalLabel_ = new QLabel("合計: $0.00（スーパーチャット・スーパーステッカー・Bits・サブスクリプション）");
	QFont f = totalLabel_->font();
	f.setPointSize(f.pointSize() + 1);
	f.setBold(true);
	totalLabel_->setFont(f);
	mainLayout->addWidget(totalLabel_);

	// ── 二分割スプリッター ───────────────────────────────────────────────────
	auto *splitter = new QSplitter(Qt::Horizontal, this);

	// 左：ユーザー一覧
	userList_ = new QListWidget(splitter);
	userList_->setAlternatingRowColors(true);
	userList_->setMinimumWidth(220);
	splitter->addWidget(userList_);

	// 右：課金履歴テーブル
	eventTable_ = new QTableWidget(0, 6, splitter);
	eventTable_->setHorizontalHeaderLabels(
		{"日時", "プラットフォーム", "種別", "金額（USD）", "メモ", "メッセージ"});
	eventTable_->horizontalHeader()->setStretchLastSection(true);
	eventTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	eventTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	eventTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	eventTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	eventTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
	eventTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	eventTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	eventTable_->setAlternatingRowColors(true);
	eventTable_->verticalHeader()->setVisible(false);
	splitter->addWidget(eventTable_);

	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 3);
	mainLayout->addWidget(splitter, 1);

	// ── ボタン行 ────────────────────────────────────────────────────────────
	auto *btnLayout = new QHBoxLayout();
	auto *injectBtn = new QPushButton("テストデータ注入（デバッグ）");
	injectBtn->setToolTip("ダミーデータを注入して暗号化・表示をテストします");
	btnLayout->addWidget(injectBtn);
	btnLayout->addStretch();
	auto *closeBtn  = new QPushButton("閉じる");
	btnLayout->addWidget(closeBtn);
	mainLayout->addLayout(btnLayout);

	// ── 接続 ────────────────────────────────────────────────────────────────
	connect(userList_, &QListWidget::currentRowChanged,
	        this, &SupporterHistoryDialog::onUserSelected);
	connect(injectBtn, &QPushButton::clicked,
	        this, &SupporterHistoryDialog::onInjectTestData);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

	// SupporterLedger の更新を自動反映
	connect(&SupporterLedger::instance(), &SupporterLedger::ledgerUpdated,
	        this, &SupporterHistoryDialog::refresh);

	refresh();
}

// ── 全体リフレッシュ ──────────────────────────────────────────────────────────

void SupporterHistoryDialog::refresh()
{
	const int prevRow = userList_->currentRow();
	updateUserList();
	// 選択行を復元（ユーザー数が変わっていなければ同じ行を保持）
	if (prevRow >= 0 && prevRow < userList_->count())
		userList_->setCurrentRow(prevRow);
	else if (userList_->count() > 0)
		userList_->setCurrentRow(0);
}

// ── ユーザー一覧更新 ──────────────────────────────────────────────────────────

void SupporterHistoryDialog::updateUserList()
{
	cachedUsers_ = SupporterLedger::instance().sortedUsers();
	const auto planUsdMap = buildPlanUsdMap();

	// 合計計算（非メンバーシップ分 + プラン価格設定済みメンバーシップ分）
	double grandTotal = 0.0;
	for (const auto &[key, entry] : cachedUsers_)
		grandTotal += SupporterLedger::computeTotalUsd(entry, planUsdMap);

	totalLabel_->setText(
		QString("合計: %1（スーパーチャット・スーパーステッカー・Bits・サブスクリプション）")
		        .arg(formatUsd(grandTotal, true)));

	const QString currentKey = (userList_->currentRow() >= 0 &&
	                             userList_->currentRow() < cachedUsers_.size())
	                                    ? cachedUsers_[userList_->currentRow()].first
	                                    : QString();

	userList_->blockSignals(true);
	userList_->clear();
	for (const auto &[key, entry] : cachedUsers_) {
		const double displayTotal = SupporterLedger::computeTotalUsd(entry, planUsdMap);
		const QString usdText = displayTotal > 0.0
		                                ? formatUsd(displayTotal, true)
		                                : "-";
		const QString platform = entry.platform.isEmpty()
		                                 ? ""
		                                 : "[" + entry.platform + "] ";
		const QString label = platform + entry.displayName + "  " + usdText;
		userList_->addItem(label);
	}
	userList_->blockSignals(false);

	// キー一致で選択を復元
	for (int i = 0; i < cachedUsers_.size(); ++i) {
		if (cachedUsers_[i].first == currentKey) {
			userList_->setCurrentRow(i);
			break;
		}
	}
}

// ── ユーザー選択 → 右ペイン更新 ──────────────────────────────────────────────

void SupporterHistoryDialog::onUserSelected(int row)
{
	if (row < 0 || row >= cachedUsers_.size()) {
		eventTable_->setRowCount(0);
		return;
	}
	updateEventTable(cachedUsers_[row].first);
}

void SupporterHistoryDialog::updateEventTable(const QString &userKey)
{
	const SupporterLedger::UserEntry *entry =
		SupporterLedger::instance().findUser(userKey);
	if (!entry) {
		eventTable_->setRowCount(0);
		return;
	}

	// 新しいイベントが先頭になるよう逆順で表示
	const auto &events = entry->events;
	const auto planUsdMap = buildPlanUsdMap();
	eventTable_->setRowCount(static_cast<int>(events.size()));

	for (int i = 0; i < static_cast<int>(events.size()); ++i) {
		const auto &ev = events[events.size() - 1 - i];
		// 元から金額情報があるイベント（SC・Bits・Sub等）
		const bool hasOriginalAmt = (ev.amountMicros > 0 || !ev.currency.isEmpty());
		// メンバーシップ: プラン価格マッピングで動的解決
		double displayUsd  = ev.usdAmount;
		bool   displayHasAmt = hasOriginalAmt;
		if (!hasOriginalAmt && !ev.planName.isEmpty()) {
			const double resolved = SupporterLedger::resolveMembershipUsd(ev, planUsdMap);
			if (resolved > 0.0) {
				displayUsd   = resolved;
				displayHasAmt = true;
			}
		}

		auto *tsItem   = new QTableWidgetItem(ev.timestamp.toString("MM/dd HH:mm:ss"));
		auto *platItem = new QTableWidgetItem(ev.platform);
		auto *typeItem = new QTableWidgetItem(SupporterLedger::eventTypeToString(ev.type));
		auto *amtItem  = new QTableWidgetItem(formatUsd(displayUsd, displayHasAmt));
		auto *noteItem = new QTableWidgetItem(ev.note);
		auto *msgItem  = new QTableWidgetItem(ev.message);

		amtItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

		eventTable_->setItem(i, 0, tsItem);
		eventTable_->setItem(i, 1, platItem);
		eventTable_->setItem(i, 2, typeItem);
		eventTable_->setItem(i, 3, amtItem);
		eventTable_->setItem(i, 4, noteItem);
		eventTable_->setItem(i, 5, msgItem);
	}
}

// ── テストデータ注入 ──────────────────────────────────────────────────────────

void SupporterHistoryDialog::onInjectTestData()
{
	const auto btn = QMessageBox::question(
		this, "テストデータ注入",
		"ダミーの課金イベントを注入します。\n"
		"これはデバッグ用の機能です。\n\n"
		"実行しますか？",
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (btn != QMessageBox::Yes)
		return;

	SupporterLedger::instance().injectTestData();
	// injectTestData 内で addEvent が呼ばれるため ledgerUpdated → refresh が連鎖する
}

// ── プラン価格マップ構築 ──────────────────────────────────────────────────────

QMap<QString, double> SupporterHistoryDialog::buildPlanUsdMap()
{
	return SupporterLedger::buildPlanUsdMapFromConfig();
}

// ── 書式ヘルパー ──────────────────────────────────────────────────────────────

QString SupporterHistoryDialog::formatUsd(double usd, bool hasAmount)
{
	if (!hasAmount)
		return "（金額情報なし）";
	return QString("$%1").arg(QString::number(usd, 'f', 2));
}
