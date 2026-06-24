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

#include "XTemplateSettingsDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

XTemplateSettingsDialog::XTemplateSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("X テンプレート設定");
	setMinimumSize(600, 420);

	// ── 左ペイン：テンプレートリスト ─────────────────────────────────────────
	list_ = new QListWidget(this);
	list_->setAlternatingRowColors(true);

	addBtn_      = new QPushButton("追加", this);
	deleteBtn_   = new QPushButton("削除", this);
	moveUpBtn_   = new QPushButton("↑", this);
	moveDownBtn_ = new QPushButton("↓", this);

	auto *listBtns = new QHBoxLayout();
	listBtns->addWidget(addBtn_);
	listBtns->addWidget(deleteBtn_);
	listBtns->addStretch();
	listBtns->addWidget(moveUpBtn_);
	listBtns->addWidget(moveDownBtn_);

	auto *leftLayout = new QVBoxLayout();
	leftLayout->addWidget(new QLabel("テンプレート一覧:", this));
	leftLayout->addWidget(list_);
	leftLayout->addLayout(listBtns);

	auto *leftWidget = new QWidget(this);
	leftWidget->setLayout(leftLayout);

	// ── 右ペイン：エディタ ────────────────────────────────────────────────────
	nameEdit_          = new QLineEdit(this);
	textEdit_          = new QPlainTextEdit(this);
	textEdit_->setPlaceholderText("投稿テキストを入力...");
	linkPlatformCombo_ = new QComboBox(this);
	linkPlatformCombo_->addItem("なし",    "none");
	linkPlatformCombo_->addItem("Twitch",  "twitch");
	linkPlatformCombo_->addItem("YouTube", "youtube");
	linkPlatformCombo_->addItem("その他",  "other");

	includeTwitchLinkCheck_  = new QCheckBox("Twitchリンクを含める", this);
	includeYoutubeLinkCheck_ = new QCheckBox("YouTubeリンクを含める", this);

	auto *form = new QFormLayout();
	form->setSpacing(8);
	form->addRow("テンプレート名:", nameEdit_);
	form->addRow("本文:",           textEdit_);
	form->addRow("リンク先:",       linkPlatformCombo_);
	form->addRow("",                includeTwitchLinkCheck_);
	form->addRow("",                includeYoutubeLinkCheck_);

	auto *note = new QLabel(
		"※ リンク先: X手動投稿でのURL入力欄のプレースホルダーヒントです。\n"
		"  Twitchリンク/YouTubeリンクを含めるには上のチェックを使用してください。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	auto *rightLayout = new QVBoxLayout();
	rightLayout->addWidget(new QLabel("テンプレート編集:", this));
	rightLayout->addLayout(form);
	rightLayout->addWidget(note);

	auto *rightWidget = new QWidget(this);
	rightWidget->setLayout(rightLayout);

	auto *splitter = new QSplitter(Qt::Horizontal, this);
	splitter->addWidget(leftWidget);
	splitter->addWidget(rightWidget);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);

	buttonBox_ = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(splitter);
	mainLayout->addWidget(buttonBox_);

	// 接続
	QObject::connect(list_, &QListWidget::currentRowChanged,
	                 this, &XTemplateSettingsDialog::onSelectionChanged);
	QObject::connect(addBtn_,      &QPushButton::clicked,
	                 this, &XTemplateSettingsDialog::onAddClicked);
	QObject::connect(deleteBtn_,   &QPushButton::clicked,
	                 this, &XTemplateSettingsDialog::onDeleteClicked);
	QObject::connect(moveUpBtn_,   &QPushButton::clicked,
	                 this, &XTemplateSettingsDialog::onMoveUpClicked);
	QObject::connect(moveDownBtn_, &QPushButton::clicked,
	                 this, &XTemplateSettingsDialog::onMoveDownClicked);
	QObject::connect(nameEdit_,  &QLineEdit::textChanged,
	                 this, &XTemplateSettingsDialog::onEditorChanged);
	QObject::connect(textEdit_,  &QPlainTextEdit::textChanged,
	                 this, &XTemplateSettingsDialog::onEditorChanged);
	QObject::connect(linkPlatformCombo_,
	                 QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, &XTemplateSettingsDialog::onEditorChanged);
	QObject::connect(includeTwitchLinkCheck_,  &QCheckBox::checkStateChanged,
	                 this, &XTemplateSettingsDialog::onEditorChanged);
	QObject::connect(includeYoutubeLinkCheck_, &QCheckBox::checkStateChanged,
	                 this, &XTemplateSettingsDialog::onEditorChanged);
	QObject::connect(buttonBox_, &QDialogButtonBox::accepted,
	                 this, &XTemplateSettingsDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected,
	                 this, &QDialog::reject);

	loadFromConfig();
}

void XTemplateSettingsDialog::loadFromConfig()
{
	templates_ = PluginConfig::instance().xTemplates;
	populateList();
	const int def = PluginConfig::instance().xDefaultTemplateIndex;
	if (def >= 0 && def < static_cast<int>(templates_.size()))
		list_->setCurrentRow(def);
	else if (!templates_.empty())
		list_->setCurrentRow(0);
}

void XTemplateSettingsDialog::populateList()
{
	const int prev = list_->currentRow();
	list_->clear();
	for (const auto &t : templates_)
		list_->addItem(QString::fromStdString(t.name));
	if (prev >= 0 && prev < list_->count())
		list_->setCurrentRow(prev);
	else if (!templates_.empty())
		list_->setCurrentRow(0);
}

void XTemplateSettingsDialog::onSelectionChanged()
{
	const int row = list_->currentRow();
	if (row < 0 || row >= static_cast<int>(templates_.size()))
		return;
	ignoreEditorChanges_ = true;
	const auto &t = templates_[static_cast<size_t>(row)];
	nameEdit_->setText(QString::fromStdString(t.name));
	textEdit_->setPlainText(QString::fromStdString(t.text));
	const int idx = linkPlatformCombo_->findData(QString::fromStdString(t.linkPlatform));
	linkPlatformCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
	includeTwitchLinkCheck_->setChecked(t.includeTwitchLink);
	includeYoutubeLinkCheck_->setChecked(t.includeYoutubeLink);
	ignoreEditorChanges_ = false;
}

void XTemplateSettingsDialog::onEditorChanged()
{
	if (ignoreEditorChanges_)
		return;
	applyEditorToCurrentTemplate();
	const int row = list_->currentRow();
	if (row >= 0 && row < static_cast<int>(templates_.size()) && list_->item(row))
		list_->item(row)->setText(nameEdit_->text());
}

void XTemplateSettingsDialog::applyEditorToCurrentTemplate()
{
	const int row = list_->currentRow();
	if (row < 0 || row >= static_cast<int>(templates_.size()))
		return;
	auto &t              = templates_[static_cast<size_t>(row)];
	t.name               = nameEdit_->text().toStdString();
	t.text               = textEdit_->toPlainText().toStdString();
	t.linkPlatform       = linkPlatformCombo_->currentData().toString().toStdString();
	t.includeTwitchLink  = includeTwitchLinkCheck_->isChecked();
	t.includeYoutubeLink = includeYoutubeLinkCheck_->isChecked();
}

void XTemplateSettingsDialog::onAddClicked()
{
	XTemplate t;
	t.name = "新しいテンプレート " + std::to_string(templates_.size() + 1);
	templates_.push_back(t);
	list_->addItem(QString::fromStdString(t.name));
	list_->setCurrentRow(static_cast<int>(templates_.size()) - 1);
}

void XTemplateSettingsDialog::onDeleteClicked()
{
	const int row = list_->currentRow();
	if (row < 0 || row >= static_cast<int>(templates_.size()))
		return;
	if (templates_.size() <= 1) {
		QMessageBox::information(this, "削除できません",
		                         "最後のテンプレートは削除できません。");
		return;
	}
	templates_.erase(templates_.begin() + row);
	delete list_->takeItem(row);
	list_->setCurrentRow(qMin(row, list_->count() - 1));
}

void XTemplateSettingsDialog::onMoveUpClicked()
{
	const int row = list_->currentRow();
	if (row <= 0 || row >= static_cast<int>(templates_.size()))
		return;
	std::swap(templates_[static_cast<size_t>(row)], templates_[static_cast<size_t>(row - 1)]);
	auto *item = list_->takeItem(row);
	list_->insertItem(row - 1, item);
	list_->setCurrentRow(row - 1);
}

void XTemplateSettingsDialog::onMoveDownClicked()
{
	const int row = list_->currentRow();
	if (row < 0 || row >= static_cast<int>(templates_.size()) - 1)
		return;
	std::swap(templates_[static_cast<size_t>(row)], templates_[static_cast<size_t>(row + 1)]);
	auto *item = list_->takeItem(row);
	list_->insertItem(row + 1, item);
	list_->setCurrentRow(row + 1);
}

void XTemplateSettingsDialog::saveToConfig()
{
	applyEditorToCurrentTemplate();
	auto &cfg                 = PluginConfig::instance();
	cfg.xTemplates            = templates_;
	const int row             = list_->currentRow();
	cfg.xDefaultTemplateIndex = (row >= 0) ? row : 0;
	cfg.save();
}

void XTemplateSettingsDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
