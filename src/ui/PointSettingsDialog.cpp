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

#include "PointSettingsDialog.hpp"
#include "modules/PointManager.hpp"
#include "core/PluginConfig.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

PointSettingsDialog::PointSettingsDialog(PointManager *manager, QWidget *parent)
	: QDialog(parent), manager_(manager)
{
	setWindowTitle("ポイント設定");
	setMinimumWidth(580);
	setMinimumHeight(420);

	auto *mainLayout = new QVBoxLayout(this);
	auto *tabs       = new QTabWidget(this);
	mainLayout->addWidget(tabs);

	// ─── Tab 1: 設定 ────────────────────────────────────────────────────────
	{
		auto *w  = new QWidget;
		auto *fl = new QFormLayout(w);
		fl->setRowWrapPolicy(QFormLayout::DontWrapRows);
		fl->setContentsMargins(12, 12, 12, 12);
		fl->setVerticalSpacing(10);

		enabledCheck_ = new QCheckBox("ポイント機能を有効にする");
		fl->addRow(enabledCheck_);

		commentAmtSpin_ = new QSpinBox;
		commentAmtSpin_->setRange(0, 100);
		fl->addRow("コメントポイント付与量:", commentAmtSpin_);

		watchIntervalSpin_ = new QSpinBox;
		watchIntervalSpin_->setRange(1, 60);
		watchIntervalSpin_->setSuffix(" 分");
		fl->addRow("視聴ポイント付与間隔:", watchIntervalSpin_);

		watchAmtSpin_ = new QSpinBox;
		watchAmtSpin_->setRange(0, 100);
		fl->addRow("視聴ポイント付与量:", watchAmtSpin_);

		commentCooldownSpin_ = new QSpinBox;
		commentCooldownSpin_->setRange(0, 300);
		commentCooldownSpin_->setSuffix(" 秒");
		commentCooldownSpin_->setSpecialValueText("無効 (0秒)");
		fl->addRow("コメントポイントのクールダウン:", commentCooldownSpin_);

		useCooldownSpin_ = new QSpinBox;
		useCooldownSpin_->setRange(0, 300);
		useCooldownSpin_->setSuffix(" 秒");
		useCooldownSpin_->setSpecialValueText("無効 (0秒)");
		fl->addRow("point_use実行のクールダウン:", useCooldownSpin_);

		fl->addRow(new QLabel(
			"※ 視聴ポイントは初回発言後から付与間隔ごとに全ユーザーへ付与されます"));

		tabs->addTab(w, "設定");
	}

	// ─── Tab 2: アクション一覧 ─────────────────────────────────────────────
	{
		auto *w  = new QWidget;
		auto *vl = new QVBoxLayout(w);
		vl->setContentsMargins(8, 8, 8, 8);

		actionTable_ = new QTableWidget(0, 4);
		actionTable_->setHorizontalHeaderLabels({"名前", "コマンド", "コスト", "アクション種別"});
		actionTable_->horizontalHeader()->setStretchLastSection(true);
		actionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
		actionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
		actionTable_->verticalHeader()->setVisible(false);
		vl->addWidget(actionTable_);

		auto *btnRow  = new QHBoxLayout;
		auto *openBtn   = new QPushButton("フォルダを開く");
		auto *reloadBtn = new QPushButton("再読み込み");
		btnRow->addWidget(openBtn);
		btnRow->addWidget(reloadBtn);
		btnRow->addStretch();
		vl->addLayout(btnRow);

		connect(openBtn,   &QPushButton::clicked, this, &PointSettingsDialog::onOpenFolder);
		connect(reloadBtn, &QPushButton::clicked, this, &PointSettingsDialog::onReloadActions);

		tabs->addTab(w, "アクション一覧");
	}

	// ─── Tab 3: ユーザーポイント ────────────────────────────────────────────
	{
		auto *w  = new QWidget;
		auto *vl = new QVBoxLayout(w);
		vl->setContentsMargins(8, 8, 8, 8);

		auto *searchRow  = new QHBoxLayout;
		searchEdit_ = new QLineEdit;
		searchEdit_->setPlaceholderText("ユーザー名で絞り込み...");
		auto *refreshBtn = new QPushButton("更新");
		searchRow->addWidget(searchEdit_);
		searchRow->addWidget(refreshBtn);
		vl->addLayout(searchRow);

		userTable_ = new QTableWidget(0, 3);
		userTable_->setHorizontalHeaderLabels({"ユーザー名", "プラットフォーム", "ポイント"});
		userTable_->horizontalHeader()->setStretchLastSection(true);
		userTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
		userTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
		userTable_->verticalHeader()->setVisible(false);
		vl->addWidget(userTable_);

		auto *adjRow   = new QHBoxLayout;
		auto *adjLabel = new QLabel("選択ユーザーのポイント:");
		adjustSpin_    = new QSpinBox;
		adjustSpin_->setRange(0, 999999);
		auto *adjBtn = new QPushButton("変更");
		adjRow->addWidget(adjLabel);
		adjRow->addWidget(adjustSpin_);
		adjRow->addWidget(adjBtn);
		adjRow->addStretch();
		vl->addLayout(adjRow);

		connect(refreshBtn, &QPushButton::clicked, this,
		        &PointSettingsDialog::onRefreshUserTable);
		connect(searchEdit_, &QLineEdit::textChanged, this,
		        &PointSettingsDialog::onSearchChanged);
		connect(adjBtn, &QPushButton::clicked, this, &PointSettingsDialog::onAdjustPoints);
		connect(userTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
			const int row = userTable_->currentRow();
			if (row < 0 || !userTable_->item(row, 2))
				return;
			bool ok;
			const int pts = userTable_->item(row, 2)->text().toInt(&ok);
			if (ok)
				adjustSpin_->setValue(pts);
		});

		tabs->addTab(w, "ユーザーポイント");
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	mainLayout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &PointSettingsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadSettings();
	buildActionTable();
	buildUserTable();
}

void PointSettingsDialog::loadSettings()
{
	const auto &cfg = PluginConfig::instance();
	enabledCheck_->setChecked(cfg.pointEnabled);
	commentAmtSpin_->setValue(cfg.pointCommentAmount);
	watchIntervalSpin_->setValue(cfg.pointWatchInterval);
	watchAmtSpin_->setValue(cfg.pointWatchAmount);
	commentCooldownSpin_->setValue(cfg.pointCommentCooldown);
	useCooldownSpin_->setValue(cfg.pointUseCooldown);
}

void PointSettingsDialog::accept()
{
	auto &cfg = PluginConfig::instance();
	cfg.pointEnabled          = enabledCheck_->isChecked();
	cfg.pointCommentAmount    = commentAmtSpin_->value();
	cfg.pointWatchInterval    = watchIntervalSpin_->value();
	cfg.pointWatchAmount      = watchAmtSpin_->value();
	cfg.pointCommentCooldown  = commentCooldownSpin_->value();
	cfg.pointUseCooldown      = useCooldownSpin_->value();
	cfg.save();
	if (manager_)
		manager_->reloadSettings();
	QDialog::accept();
}

void PointSettingsDialog::buildActionTable()
{
	if (!manager_)
		return;
	const auto &actions = manager_->actions();
	actionTable_->setRowCount(static_cast<int>(actions.size()));
	for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
		const auto &a = actions[i];
		actionTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(a.name)));
		actionTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(a.command)));
		actionTable_->setItem(i, 2, new QTableWidgetItem(QString::number(a.cost)));
		actionTable_->setItem(i, 3,
		                      new QTableWidgetItem(QString::fromStdString(a.actionType)));
	}
	actionTable_->resizeColumnsToContents();
}

void PointSettingsDialog::buildUserTable(const QString &filter)
{
	if (!manager_)
		return;
	const auto users = manager_->allUserPoints();
	userTable_->setRowCount(0);
	for (const auto &u : users) {
		if (!filter.isEmpty() && !u.userId.contains(filter, Qt::CaseInsensitive))
			continue;
		const int row = userTable_->rowCount();
		userTable_->insertRow(row);
		userTable_->setItem(row, 0, new QTableWidgetItem(u.userId));
		userTable_->setItem(row, 1, new QTableWidgetItem(u.platform));
		userTable_->setItem(row, 2, new QTableWidgetItem(QString::number(u.points)));
	}
}

void PointSettingsDialog::onOpenFolder()
{
	if (!manager_)
		return;
	const QString dir = manager_->actionsDir();
	if (dir.isEmpty())
		return;
	QDir().mkpath(dir);
#ifdef _WIN32
	ShellExecuteW(nullptr, L"explore",
	              reinterpret_cast<LPCWSTR>(dir.utf16()),
	              nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void PointSettingsDialog::onReloadActions()
{
	if (!manager_)
		return;
	manager_->loadActions(manager_->actionsDir());
	buildActionTable();
}

void PointSettingsDialog::onRefreshUserTable()
{
	buildUserTable(searchEdit_->text());
}

void PointSettingsDialog::onSearchChanged(const QString &text)
{
	buildUserTable(text);
}

void PointSettingsDialog::onAdjustPoints()
{
	if (!manager_)
		return;
	const int row = userTable_->currentRow();
	if (row < 0 || !userTable_->item(row, 0) || !userTable_->item(row, 1))
		return;
	const QString userId   = userTable_->item(row, 0)->text();
	const QString platform = userTable_->item(row, 1)->text();
	const int newPoints    = adjustSpin_->value();
	manager_->setPoints(userId, platform, newPoints);
	userTable_->item(row, 2)->setText(QString::number(newPoints));
}
