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

#include "MembershipPlanPriceDialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/PluginConfig.hpp"

MembershipPlanPriceDialog::MembershipPlanPriceDialog(QWidget *parent)
	: QDialog(parent, Qt::Window)
{
	setWindowTitle("メンバーシッププラン価格設定");
	setMinimumSize(480, 320);
	resize(560, 380);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(8);
	mainLayout->setContentsMargins(12, 12, 12, 12);

	auto *desc = new QLabel(
		"プラン名に価格・通貨を設定すると、サポーター履歴でメンバーシップイベントの\n"
		"金額が表示・集計されます。過去のイベントにも遡って反映されます。\n"
		"通貨は ISO 4217 コードで入力してください（例: JPY, USD, EUR）。");
	desc->setWordWrap(true);
	mainLayout->addWidget(desc);

	// テーブル
	table_ = new QTableWidget(0, 3, this);
	table_->setHorizontalHeaderLabels({"プラン名", "金額", "通貨"});
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	table_->verticalHeader()->setVisible(false);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setAlternatingRowColors(true);
	mainLayout->addWidget(table_, 1);

	// PluginConfig から現在の設定を読み込む
	const auto &plans = PluginConfig::instance().membershipPlanPrices;
	table_->setRowCount(static_cast<int>(plans.size()));
	for (int i = 0; i < static_cast<int>(plans.size()); ++i) {
		table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(plans[i].planName)));
		table_->setItem(i, 1, new QTableWidgetItem(QString::number(plans[i].amount, 'f', 0)));
		table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(plans[i].currency)));
	}

	// ボタン行
	auto *btnLayout = new QHBoxLayout();
	auto *addBtn    = new QPushButton("追加");
	auto *delBtn    = new QPushButton("削除");
	btnLayout->addWidget(addBtn);
	btnLayout->addWidget(delBtn);
	btnLayout->addStretch();

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	btnLayout->addWidget(bb);
	mainLayout->addLayout(btnLayout);

	connect(addBtn, &QPushButton::clicked, this, &MembershipPlanPriceDialog::onAdd);
	connect(delBtn, &QPushButton::clicked, this, &MembershipPlanPriceDialog::onDelete);
	connect(bb, &QDialogButtonBox::accepted, this, &MembershipPlanPriceDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MembershipPlanPriceDialog::onAdd()
{
	const int row = table_->rowCount();
	table_->insertRow(row);
	table_->setItem(row, 0, new QTableWidgetItem(""));
	table_->setItem(row, 1, new QTableWidgetItem("0"));
	table_->setItem(row, 2, new QTableWidgetItem("JPY"));
	table_->scrollToBottom();
	table_->setCurrentCell(row, 0);
	table_->editItem(table_->item(row, 0));
}

void MembershipPlanPriceDialog::onDelete()
{
	const int row = table_->currentRow();
	if (row >= 0)
		table_->removeRow(row);
}

void MembershipPlanPriceDialog::accept()
{
	auto &plans = PluginConfig::instance().membershipPlanPrices;
	plans.clear();

	for (int i = 0; i < table_->rowCount(); ++i) {
		const QString name     = table_->item(i, 0) ? table_->item(i, 0)->text().trimmed() : "";
		const QString amountStr= table_->item(i, 1) ? table_->item(i, 1)->text().trimmed() : "0";
		const QString currency = table_->item(i, 2) ? table_->item(i, 2)->text().trimmed().toUpper() : "JPY";

		if (name.isEmpty())
			continue; // プラン名が空の行は無視

		MembershipPlanPrice p;
		p.planName = name.toStdString();
		p.amount   = amountStr.toDouble();
		p.currency = currency.isEmpty() ? "JPY" : currency.toStdString();
		plans.push_back(std::move(p));
	}

	PluginConfig::instance().save();
	QDialog::accept();
}
