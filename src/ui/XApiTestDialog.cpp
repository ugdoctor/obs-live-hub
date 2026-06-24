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

#include "XApiTestDialog.hpp"
#include "core/PluginConfig.hpp"
#include "modules/XClient.hpp"

#include <QApplication>
#include <QDateTime>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <thread>
#include <string>

XApiTestDialog::XApiTestDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("X API テスト");
	setMinimumWidth(480);

	// ── 認証確認グループ ──
	auto *verifyGroup  = new QGroupBox("認証確認", this);
	auto *verifyNote   = new QLabel(
		"GET /2/users/me を呼び出して認証情報が有効かを確認します（投稿は発生しません）。",
		verifyGroup);
	verifyNote->setWordWrap(true);
	verifyNote->setStyleSheet("color: gray; font-size: 10px;");
	verifyBtn_    = new QPushButton("認証確認", verifyGroup);
	verifyStatus_ = new QLabel("—", verifyGroup);
	verifyStatus_->setWordWrap(true);

	auto *vLayout = new QVBoxLayout(verifyGroup);
	vLayout->addWidget(verifyNote);
	vLayout->addWidget(verifyBtn_);
	vLayout->addWidget(verifyStatus_);

	// ── テスト投稿グループ ──
	auto *postGroup  = new QGroupBox("テスト投稿", this);
	auto *postNote   = new QLabel(
		"現在の日時を含む固定テキストを実際に X へ投稿します（確認後に実行）。",
		postGroup);
	postNote->setWordWrap(true);
	postNote->setStyleSheet("color: gray; font-size: 10px;");
	testPostBtn_ = new QPushButton("テスト投稿", postGroup);
	postStatus_  = new QLabel("—", postGroup);
	postStatus_->setWordWrap(true);

	auto *pLayout = new QVBoxLayout(postGroup);
	pLayout->addWidget(postNote);
	pLayout->addWidget(testPostBtn_);
	pLayout->addWidget(postStatus_);

	// ── 閉じるボタン ──
	closeBtn_ = new QPushButton("閉じる", this);

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(10);
	layout->addWidget(verifyGroup);
	layout->addWidget(postGroup);
	layout->addWidget(closeBtn_, 0, Qt::AlignRight);

	QObject::connect(verifyBtn_,    &QPushButton::clicked, this, &XApiTestDialog::onVerifyClicked);
	QObject::connect(testPostBtn_,  &QPushButton::clicked, this, &XApiTestDialog::onTestPostClicked);
	QObject::connect(closeBtn_,     &QPushButton::clicked, this, &QDialog::accept);
}

void XApiTestDialog::setBusy(bool busy)
{
	verifyBtn_  ->setEnabled(!busy);
	testPostBtn_->setEnabled(!busy);
	closeBtn_   ->setEnabled(!busy);
}

void XApiTestDialog::setVerifyStatus(const QString &text, bool ok)
{
	verifyStatus_->setText(text);
	verifyStatus_->setStyleSheet(ok ? "color: #44bb44;" : "color: #ff5555;");
}

void XApiTestDialog::setPostStatus(const QString &text, bool ok)
{
	postStatus_->setText(text);
	postStatus_->setStyleSheet(ok ? "color: #44bb44;" : "color: #ff5555;");
}

void XApiTestDialog::onVerifyClicked()
{
	const auto &cfg = PluginConfig::instance();
	if (cfg.xApiKey.empty() || cfg.xApiSecret.empty() ||
	    cfg.xAccessToken.empty() || cfg.xAccessTokenSecret.empty()) {
		setVerifyStatus(
			"エラー: 認証情報が未設定です。\n"
			"「X投稿 → Xアカウント設定」から入力してください。", false);
		return;
	}

	setBusy(true);
	setVerifyStatus("確認中...", true);

	const std::string apiKey            = cfg.xApiKey;
	const std::string apiSecret         = cfg.xApiSecret;
	const std::string accessToken       = cfg.xAccessToken;
	const std::string accessTokenSecret = cfg.xAccessTokenSecret;

	QPointer<XApiTestDialog> self(this);
	std::thread([self, apiKey, apiSecret, accessToken, accessTokenSecret]() {
		const auto result = XClient::verifyCredentials(
			apiKey, apiSecret, accessToken, accessTokenSecret);

		QMetaObject::invokeMethod(qApp, [self, result]() {
			if (!self) return;
			self->setBusy(false);
			if (result.ok) {
				self->setVerifyStatus(
					QString("認証成功: @%1（%2）\nHTTP %3")
						.arg(QString::fromStdString(result.username))
						.arg(QString::fromStdString(result.displayName))
						.arg(result.httpStatus),
					true);
			} else {
				self->setVerifyStatus(
					QString("認証失敗: HTTP %1\n%2")
						.arg(result.httpStatus)
						.arg(QString::fromStdString(result.errorDesc)),
					false);
			}
		}, Qt::QueuedConnection);
	}).detach();
}

void XApiTestDialog::onTestPostClicked()
{
	const auto &cfg = PluginConfig::instance();
	if (cfg.xApiKey.empty() || cfg.xApiSecret.empty() ||
	    cfg.xAccessToken.empty() || cfg.xAccessTokenSecret.empty()) {
		setPostStatus(
			"エラー: 認証情報が未設定です。\n"
			"「X投稿 → Xアカウント設定」から入力してください。", false);
		return;
	}

	const QString now = QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss");
	const QString testText = QString("obs-live-hub 接続テスト [%1]").arg(now);

	const auto ret = QMessageBox::question(
		this, "テスト投稿の確認",
		QString("以下のテキストを実際に X へ投稿します。よろしいですか？\n\n%1")
			.arg(testText),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	if (ret != QMessageBox::Yes)
		return;

	setBusy(true);
	setPostStatus("投稿中...", true);

	const std::string apiKey            = cfg.xApiKey;
	const std::string apiSecret         = cfg.xApiSecret;
	const std::string accessToken       = cfg.xAccessToken;
	const std::string accessTokenSecret = cfg.xAccessTokenSecret;
	const std::string text              = testText.toStdString();

	QPointer<XApiTestDialog> self(this);
	std::thread([self, apiKey, apiSecret, accessToken, accessTokenSecret, text]() {
		const auto result = XClient::postTweet(
			apiKey, apiSecret, accessToken, accessTokenSecret, text);

		QMetaObject::invokeMethod(qApp, [self, result]() {
			if (!self) return;
			self->setBusy(false);
			if (result.ok) {
				const QString id = QString::fromStdString(result.tweetId);
				self->setPostStatus(
					QString("投稿成功: HTTP %1\nツイートID: %2")
						.arg(result.httpStatus)
						.arg(id),
					true);
			} else {
				self->setPostStatus(
					QString("投稿失敗: HTTP %1\n%2")
						.arg(result.httpStatus)
						.arg(QString::fromStdString(result.responseBody)
							.left(200)),
					false);
			}
		}, Qt::QueuedConnection);
	}).detach();
}
