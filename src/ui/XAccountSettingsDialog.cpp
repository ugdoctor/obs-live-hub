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

#include "XAccountSettingsDialog.hpp"
#include "core/PluginConfig.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

XAccountSettingsDialog::XAccountSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("X アカウント設定");
	setMinimumWidth(480);

	auto makeEdit = [this]() -> QLineEdit * {
		auto *e = new QLineEdit(this);
		e->setEchoMode(QLineEdit::Password);
		return e;
	};

	apiKeyEdit_            = makeEdit();
	apiSecretEdit_         = makeEdit();
	accessTokenEdit_       = makeEdit();
	accessTokenSecretEdit_ = makeEdit();

	auto *note = new QLabel(
		"X Developer Portal (developer.twitter.com) でアプリを作成し、\n"
		"OAuth 1.0a の認証情報（User Context）を入力してください。\n"
		"投稿にはアプリの「Read and Write」権限が必要です。",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: gray; font-size: 10px;");

	auto *form = new QFormLayout();
	form->setSpacing(8);
	form->addRow("API Key:",             apiKeyEdit_);
	form->addRow("API Secret:",          apiSecretEdit_);
	form->addRow("Access Token:",        accessTokenEdit_);
	form->addRow("Access Token Secret:", accessTokenSecretEdit_);

	buttonBox_ = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(note);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addSpacing(8);
	layout->addWidget(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this,
	                 &XAccountSettingsDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this,
	                 &QDialog::reject);

	loadFromConfig();
}

void XAccountSettingsDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	apiKeyEdit_           ->setText(QString::fromStdString(cfg.xApiKey));
	apiSecretEdit_        ->setText(QString::fromStdString(cfg.xApiSecret));
	accessTokenEdit_      ->setText(QString::fromStdString(cfg.xAccessToken));
	accessTokenSecretEdit_->setText(QString::fromStdString(cfg.xAccessTokenSecret));
}

void XAccountSettingsDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.xApiKey            = apiKeyEdit_           ->text().toStdString();
	cfg.xApiSecret         = apiSecretEdit_        ->text().toStdString();
	cfg.xAccessToken       = accessTokenEdit_      ->text().toStdString();
	cfg.xAccessTokenSecret = accessTokenSecretEdit_->text().toStdString();
	cfg.save();
}

void XAccountSettingsDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
