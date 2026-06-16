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

#include "SettingsDialog.hpp"
#include "OverlayUtils.hpp"
#include "auth/GoogleOAuth.hpp"
#include "auth/TwitchOAuth.hpp"
#include "core/PluginConfig.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 設定");
	setMinimumWidth(500);

	// ---- YouTube ----
	youtubeApiKeyEdit_ = new QLineEdit(this);
	youtubeApiKeyEdit_->setPlaceholderText("AIza...");

	youtubeBroadcastIdEdit_ = new QLineEdit(this);
	youtubeBroadcastIdEdit_->setPlaceholderText("me（自動検出） または 動画ID");
	youtubeBroadcastIdEdit_->setToolTip(
		"空または 'me': OAuth で配信中のブロードキャストを自動検出します。\n"
		"特定の動画 ID を入力した場合は API キーのみで検出します。");

	youtubeClientIdEdit_ = new QLineEdit(this);
	youtubeClientIdEdit_->setPlaceholderText(
		"Google Cloud Console の OAuth 2.0 クライアント ID");
	youtubeClientIdEdit_->setToolTip(
		"Google Cloud Console > API とサービス > 認証情報 >\n"
		"OAuth 2.0 クライアント ID（デスクトップアプリ）で作成してください。");

	youtubeClientSecretEdit_ = new QLineEdit(this);
	youtubeClientSecretEdit_->setEchoMode(QLineEdit::Password);
	youtubeClientSecretEdit_->setPlaceholderText("OAuth Client Secret");

	googleAuthStatusLabel_ = new QLabel("未連携", this);
	googleAuthStatusLabel_->setStyleSheet("color: gray; font-weight: bold;");

	googleAuthBtn_ = new QPushButton("Googleアカウントと連携", this);
	googleAuthBtn_->setToolTip(
		"ブラウザで Google アカウントにログインし、YouTube へのアクセスを許可します。\n"
		"Client ID と Client Secret を先に入力してください。\n"
		"Refresh Token が自動保存され、Access Token は自動更新されます。\n\n"
		"注: トークン交換は WinHTTP (Windows SChannel) で行うため\n"
		"Qt TLS エラーの影響を受けません。");

	// ---- YouTube コメント取得間隔 ----
	youtubePollIntervalSpinBox_ = new QSpinBox(this);
	youtubePollIntervalSpinBox_->setRange(5, 120);
	youtubePollIntervalSpinBox_->setValue(5);
	youtubePollIntervalSpinBox_->setSuffix(" 秒");
	youtubePollIntervalSpinBox_->setToolTip(
		"YouTube Live Chat のコメント取得間隔です。\n"
		"長くするとクォータ消費を抑えられますが、コメント表示が遅れます。");

	youtubePollQuotaLabel_ = new QLabel(this);
	youtubePollQuotaLabel_->setWordWrap(true);
	youtubePollQuotaLabel_->setStyleSheet("color: gray; font-size: 10px;");

	// ---- YouTube クォータ設定 ----
	youtubeQuotaInfoLabel_ = new QLabel(this);
	youtubeQuotaInfoLabel_->setWordWrap(true);
	youtubeQuotaInfoLabel_->setText(
		"YouTube Data API の無料枠は1日 10,000 ユニットです。\n"
		"liveChatMessages.list は 1 回 5 ユニット消費（5 秒間隔で約 2.8 時間/日）。\n"
		"超過する場合は Google Cloud Console で課金を有効化してください。\n"
		"課金有効化後、下のチェックをオンにするとクォータ上限を無視して継続します。");
	youtubeQuotaInfoLabel_->setStyleSheet("color: gray; font-size: 10px;");

	youtubeIgnoreQuotaCheck_ =
		new QCheckBox("課金を有効化済み・クォータ上限を無視する", this);

	// ---- Twitch ----
	twitchOAuthEdit_ = new QLineEdit(this);
	twitchOAuthEdit_->setEchoMode(QLineEdit::Password);
	twitchOAuthEdit_->setPlaceholderText("oauth:xxxxxx");

	twitchUsernameEdit_ = new QLineEdit(this);
	twitchUsernameEdit_->setPlaceholderText("your_twitch_username");

	twitchChannelEdit_ = new QLineEdit(this);
	twitchChannelEdit_->setPlaceholderText("channel_name");

	twitchClientIdEdit_ = new QLineEdit(this);
	twitchClientIdEdit_->setPlaceholderText("Helix API 用・OAuth 連携用 Twitch Client ID");

	twitchAuthStatusLabel_ = new QLabel("未連携", this);
	twitchAuthStatusLabel_->setStyleSheet("color: gray; font-weight: bold;");

	twitchAuthBtn_ = new QPushButton("Twitchアカウントと連携", this);
	twitchAuthBtn_->setToolTip(
		"ブラウザで Twitch アカウントにログインし、チャットへのアクセスを許可します。\n"
		"Client ID を先に入力してください。\n\n"
		"事前準備:\n"
		"1. https://dev.twitch.tv/console を開く\n"
		"2. アプリケーションを登録（またはすでに登録済みのものを編集）\n"
		"3. リダイレクト URI に http://localhost （ポートなし）を追加して保存\n"
		"4. Client ID をこのダイアログの「Twitch Client ID」欄に貼り付ける");

	// ---- WebSocket ----
	wsPortSpinBox_ = new QSpinBox(this);
	wsPortSpinBox_->setRange(1024, 65535);
	wsPortSpinBox_->setValue(8765);
	wsPortSpinBox_->setToolTip("OBS ブラウザソース用 WebSocket サーバーのポート番号");

	openOverlayBtn_ = new QPushButton("オーバーレイ HTML を開く", this);
	openOverlayBtn_->setToolTip(
		"overlay.html をブラウザで開きます。\n"
		"OBS ブラウザソースにこのファイルのパスを設定してください。");

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	layout_ = new QFormLayout(this);
	layout_->addRow("YouTube API Key:", youtubeApiKeyEdit_);
	layout_->addRow("YouTube Broadcast ID:", youtubeBroadcastIdEdit_);
	layout_->addRow("Google OAuth Client ID:", youtubeClientIdEdit_);
	layout_->addRow("Google OAuth Client Secret:", youtubeClientSecretEdit_);
	layout_->addRow("Google アカウント連携:", googleAuthStatusLabel_);
	layout_->addRow(googleAuthBtn_);
	{
		auto *note = new QLabel(
			"<span style='color:orange;font-size:10px;'>"
			"※スコープ変更後（配信一括設定機能追加）は再連携が必要です"
			"</span>",
			this);
		note->setTextFormat(Qt::RichText);
		layout_->addRow(note);
	}
	layout_->addRow("コメント取得間隔:", youtubePollIntervalSpinBox_);
	layout_->addRow(youtubePollQuotaLabel_);
	layout_->addRow(youtubeQuotaInfoLabel_);
	layout_->addRow(youtubeIgnoreQuotaCheck_);
	layout_->addRow("Twitch OAuth Token:", twitchOAuthEdit_);
	layout_->addRow("Twitch Username:", twitchUsernameEdit_);
	layout_->addRow("Twitch Channel:", twitchChannelEdit_);
	layout_->addRow("Twitch Client ID:", twitchClientIdEdit_);
	layout_->addRow("Twitchアカウント連携:", twitchAuthStatusLabel_);
	layout_->addRow(twitchAuthBtn_);
	{
		auto *note = new QLabel(
			"<span style='color:orange;font-size:10px;'>"
			"※スコープ変更後（配信一括設定機能追加）は再連携が必要です"
			"</span>",
			this);
		note->setTextFormat(Qt::RichText);
		layout_->addRow(note);
	}
	layout_->addRow("WebSocket ポート:", wsPortSpinBox_);
	layout_->addRow("オーバーレイ:", openOverlayBtn_);
	layout_->addRow(buttonBox_);

	QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
	QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
	QObject::connect(youtubePollIntervalSpinBox_,
			 QOverload<int>::of(&QSpinBox::valueChanged),
			 this, &SettingsDialog::onPollIntervalChanged);
	QObject::connect(openOverlayBtn_, &QPushButton::clicked, this,
			 &SettingsDialog::onOpenOverlay);
	QObject::connect(googleAuthBtn_, &QPushButton::clicked, this,
			 &SettingsDialog::onGoogleAuthClicked);
	QObject::connect(twitchAuthBtn_, &QPushButton::clicked, this,
			 &SettingsDialog::onTwitchAuthClicked);

	loadFromConfig();
}

void SettingsDialog::updateAuthStatus()
{
	const auto &cfg = PluginConfig::instance();
	if (!cfg.youtubeRefreshToken.empty()) {
		googleAuthStatusLabel_->setText("連携済み ✓");
		googleAuthStatusLabel_->setStyleSheet("color: green; font-weight: bold;");
		googleAuthBtn_->setText("再連携");
	} else {
		googleAuthStatusLabel_->setText("未連携");
		googleAuthStatusLabel_->setStyleSheet("color: gray; font-weight: bold;");
		googleAuthBtn_->setText("Googleアカウントと連携");
	}
}

void SettingsDialog::loadFromConfig()
{
	auto &cfg = PluginConfig::instance();
	youtubeApiKeyEdit_->setText(QString::fromStdString(cfg.youtubeApiKey));
	youtubeBroadcastIdEdit_->setText(QString::fromStdString(cfg.youtubeBroadcastId));
	youtubeClientIdEdit_->setText(QString::fromStdString(cfg.youtubeClientId));
	youtubeClientSecretEdit_->setText(QString::fromStdString(cfg.youtubeClientSecret));
	youtubeIgnoreQuotaCheck_->setChecked(cfg.youtubeIgnoreQuota);
	youtubePollIntervalSpinBox_->setValue(cfg.youtubePollInterval);
	onPollIntervalChanged(cfg.youtubePollInterval);
	twitchOAuthEdit_->setText(QString::fromStdString(cfg.twitchOAuthToken));
	twitchUsernameEdit_->setText(QString::fromStdString(cfg.twitchUsername));
	twitchChannelEdit_->setText(QString::fromStdString(cfg.twitchChannel));
	twitchClientIdEdit_->setText(QString::fromStdString(cfg.twitchClientId));
	wsPortSpinBox_->setValue(cfg.wsPort);
	updateAuthStatus();
	updateTwitchAuthStatus();
}

void SettingsDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();
	cfg.youtubeApiKey = youtubeApiKeyEdit_->text().trimmed().toStdString();
	cfg.youtubeBroadcastId = youtubeBroadcastIdEdit_->text().trimmed().toStdString();
	cfg.youtubeClientId = youtubeClientIdEdit_->text().trimmed().toStdString();
	cfg.youtubeClientSecret = youtubeClientSecretEdit_->text().trimmed().toStdString();
	cfg.youtubeIgnoreQuota = youtubeIgnoreQuotaCheck_->isChecked();
	cfg.youtubePollInterval = youtubePollIntervalSpinBox_->value();
	cfg.twitchOAuthToken = twitchOAuthEdit_->text().toStdString();
	cfg.twitchUsername = twitchUsernameEdit_->text().toLower().toStdString();
	cfg.twitchChannel = twitchChannelEdit_->text().toLower().toStdString();
	cfg.twitchClientId = twitchClientIdEdit_->text().toStdString();
	cfg.wsPort = wsPortSpinBox_->value();
	// refresh/access token と expiry は OAuth フローで直接 cfg に書き込まれるため上書きしない
	cfg.save();
}

void SettingsDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}

void SettingsDialog::onOpenOverlay()
{
	const QString path = findOverlayHtmlPath();
	if (path.isEmpty()) {
		QMessageBox::warning(
			this, "obs-live-hub",
			"overlay.html が見つかりません。\n\n"
			"以下のいずれかの場所に配置してください:\n"
			"  %APPDATA%\\obs-studio\\plugins\\obs-live-hub\\overlay.html\n"
			"  obs-plugins\\64bit\\obs-live-hub\\overlay.html\n"
			"  data\\obs-plugins\\obs-live-hub\\overlay.html");
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
		QMessageBox::warning(this, "obs-live-hub", "HTML ファイルを開けませんでした。");
}

void SettingsDialog::onGoogleAuthClicked()
{
	const QString clientId = youtubeClientIdEdit_->text().trimmed();
	const QString clientSecret = youtubeClientSecretEdit_->text().trimmed();

	if (clientId.isEmpty() || clientSecret.isEmpty()) {
		QMessageBox::warning(
			this, "obs-live-hub",
			"Google OAuth Client ID と Client Secret を入力してください。\n\n"
			"取得方法:\n"
			"1. Google Cloud Console を開く\n"
			"2. API とサービス > 認証情報\n"
			"3. 認証情報を作成 > OAuth 2.0 クライアント ID\n"
			"4. アプリケーションの種類: デスクトップアプリ\n"
			"5. 作成したクライアントの ID と Secret をここに貼り付ける");
		return;
	}

	pendingClientId_ = clientId;
	pendingClientSecret_ = clientSecret;

	googleAuthBtn_->setEnabled(false);
	googleAuthBtn_->setText("ブラウザで認証中...");
	googleAuthStatusLabel_->setText("ブラウザを開いています...");
	googleAuthStatusLabel_->setStyleSheet("color: orange; font-weight: bold;");

	// コールバックサーバーを起動してからブラウザを開く。
	// onCode は detach スレッドから呼ばれる → invokeMethod でメインスレッドに転送。
	QPointer<SettingsDialog> self = this;
	GoogleOAuthCallbackServer::startAsync([self](const std::string &code) {
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, code]() {
				if (self)
					self->onOAuthCodeReceived(
						QString::fromStdString(code));
			},
			Qt::QueuedConnection);
	});

	// OAuth 認証 URL を構築してブラウザで開く
	QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
	QUrlQuery query;
	query.addQueryItem("client_id", clientId);
	query.addQueryItem("redirect_uri", "http://localhost:8766/callback");
	query.addQueryItem("response_type", "code");
	query.addQueryItem("scope", "https://www.googleapis.com/auth/youtube");
	query.addQueryItem("access_type", "offline");
	query.addQueryItem("prompt", "consent"); // 毎回 refresh_token を取得するために必須
	authUrl.setQuery(query);
	QDesktopServices::openUrl(authUrl);
}

void SettingsDialog::onOAuthCodeReceived(const QString &code)
{
	if (code.isEmpty()) {
		googleAuthBtn_->setEnabled(true);
		googleAuthBtn_->setText("再試行");
		googleAuthStatusLabel_->setText("タイムアウトまたはキャンセル");
		googleAuthStatusLabel_->setStyleSheet("color: red; font-weight: bold;");
		return;
	}

	googleAuthStatusLabel_->setText("トークン取得中（WinHTTP）...");
	googleAuthStatusLabel_->setStyleSheet("color: orange; font-weight: bold;");

	// WinHTTP で authorization code → access_token + refresh_token を取得。
	// Qt の QNetworkAccessManager / TLS を一切使わないため TLS 初期化エラーが発生しない。
	QPointer<SettingsDialog> self = this;
	const std::string clientId = pendingClientId_.toStdString();
	const std::string clientSecret = pendingClientSecret_.toStdString();
	const std::string codeStr = code.toStdString();

	GoogleOAuthTokenClient::exchangeCodeAsync(
		clientId, clientSecret, codeStr, "http://localhost:8766/callback",
		[self](GoogleTokenResult res) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, res]() {
					if (self)
						self->onTokenExchangeResult(
							res.ok, res.accessToken,
							res.refreshToken, res.expiresIn,
							res.error);
				},
				Qt::QueuedConnection);
		});
}

void SettingsDialog::onTokenExchangeResult(bool ok, const std::string &accessToken,
					   const std::string &refreshToken, int64_t expiresIn,
					   const std::string &error)
{
	googleAuthBtn_->setEnabled(true);

	if (!ok) {
		googleAuthStatusLabel_->setText("エラー: " + QString::fromStdString(error));
		googleAuthStatusLabel_->setStyleSheet("color: red; font-weight: bold;");
		googleAuthBtn_->setText("再試行");
		return;
	}

	// 取得したトークンを PluginConfig に保存（即座に cfg.save() する）
	auto &cfg = PluginConfig::instance();
	cfg.youtubeClientId = pendingClientId_.toStdString();
	cfg.youtubeClientSecret = pendingClientSecret_.toStdString();
	cfg.youtubeAccessToken = accessToken;
	cfg.youtubeRefreshToken = refreshToken;
	cfg.youtubeTokenExpiry = QDateTime::currentSecsSinceEpoch() + expiresIn;
	cfg.save();

	// UI フィールドも最新値に同期
	youtubeClientIdEdit_->setText(pendingClientId_);
	youtubeClientSecretEdit_->setText(pendingClientSecret_);

	googleAuthStatusLabel_->setText("連携済み ✓");
	googleAuthStatusLabel_->setStyleSheet("color: green; font-weight: bold;");
	googleAuthBtn_->setText("再連携");
}

void SettingsDialog::updateTwitchAuthStatus()
{
	const auto &cfg = PluginConfig::instance();
	if (!cfg.twitchOAuthToken.empty()) {
		twitchAuthStatusLabel_->setText("連携済み ✓");
		twitchAuthStatusLabel_->setStyleSheet("color: green; font-weight: bold;");
		twitchAuthBtn_->setText("再連携");
	} else {
		twitchAuthStatusLabel_->setText("未連携");
		twitchAuthStatusLabel_->setStyleSheet("color: gray; font-weight: bold;");
		twitchAuthBtn_->setText("Twitchアカウントと連携");
	}
}

void SettingsDialog::onTwitchAuthClicked()
{
	const QString clientId = twitchClientIdEdit_->text().trimmed();

	if (clientId.isEmpty()) {
		QMessageBox::warning(
			this, "obs-live-hub",
			"Twitch Client ID を入力してください。\n\n"
			"取得方法:\n"
			"1. https://dev.twitch.tv/console を開く\n"
			"2. アプリケーションを登録\n"
			"3. OAuth リダイレクト URI に http://localhost （ポートなし）を追加\n"
			"4. Client ID をここに貼り付ける");
		return;
	}

	pendingTwitchClientId_ = clientId;

	twitchAuthBtn_->setEnabled(false);
	twitchAuthBtn_->setText("ブラウザで認証中...");
	twitchAuthStatusLabel_->setText("ブラウザを開いています...");
	twitchAuthStatusLabel_->setStyleSheet("color: orange; font-weight: bold;");

	// Twitch Developer Console に登録するリダイレクトURIは
	// http://localhost （ポートなし）である必要があります。
	// コールバックサーバーはポート 80 → 8767 の順で試みます。
	// onToken/onError は detach スレッドから呼ばれる → invokeMethod でメインスレッドに転送。
	QPointer<SettingsDialog> self = this;
	TwitchOAuthCallbackServer::startAsync(
		[self](const std::string &token) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, token]() {
					if (self)
						self->onTwitchTokenReceived(
							QString::fromStdString(token));
				},
				Qt::QueuedConnection);
		},
		[self](const std::string &error) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, error]() {
					if (!self)
						return;
					self->twitchAuthBtn_->setEnabled(true);
					self->twitchAuthBtn_->setText("再試行");
					self->twitchAuthStatusLabel_->setText("ポートエラー");
					self->twitchAuthStatusLabel_->setStyleSheet(
						"color: red; font-weight: bold;");
					QMessageBox::warning(self, "obs-live-hub",
							     QString::fromStdString(error));
				},
				Qt::QueuedConnection);
		});

	// OAuth 認証 URL を構築してブラウザで開く（Implicit Grant）
	// redirect_uri は http://localhost （ポートなし）。
	// Twitch Developer Console のリダイレクト URI と完全一致させること。
	QUrl authUrl("https://id.twitch.tv/oauth2/authorize");
	QUrlQuery query;
	query.addQueryItem("client_id", clientId);
	query.addQueryItem("redirect_uri", "http://localhost");
	query.addQueryItem("response_type", "token");
	query.addQueryItem("scope", "chat:read chat:edit channel:manage:broadcast user:edit:broadcast");
	authUrl.setQuery(query);
	QDesktopServices::openUrl(authUrl);
}

void SettingsDialog::onTwitchTokenReceived(const QString &token)
{
	twitchAuthBtn_->setEnabled(true);

	if (token.isEmpty()) {
		twitchAuthStatusLabel_->setText("タイムアウトまたはキャンセル");
		twitchAuthStatusLabel_->setStyleSheet("color: red; font-weight: bold;");
		twitchAuthBtn_->setText("再試行");
		return;
	}

	// トークンを PluginConfig に保存
	auto &cfg = PluginConfig::instance();
	cfg.twitchClientId = pendingTwitchClientId_.toStdString();
	cfg.twitchOAuthToken = token.toStdString();
	cfg.save();

	// UI フィールドも最新値に同期
	twitchClientIdEdit_->setText(pendingTwitchClientId_);
	twitchOAuthEdit_->setText(token);

	twitchAuthStatusLabel_->setText("連携済み ✓");
	twitchAuthStatusLabel_->setStyleSheet("color: green; font-weight: bold;");
	twitchAuthBtn_->setText("再連携");

	// plugin-main.cpp の reconnectTwitch() をトリガーする
	emit twitchTokenUpdated();
}

void SettingsDialog::onPollIntervalChanged(int seconds)
{
	const double perHour = (3600.0 / seconds) * 5.0;
	const double hoursToLimit = 10000.0 / perHour;

	QString quotaText =
		QString("取得間隔: %1秒 → 1時間あたり %2 ユニット消費\n")
			.arg(seconds)
			.arg(static_cast<int>(perHour));

	if (hoursToLimit >= 24.0) {
		quotaText += "1日の上限（10,000ユニット）まで 1日以上（クォータ内で配信可能）";
	} else {
		quotaText += QString("1日の上限（10,000ユニット）まで 約 %1 時間")
				     .arg(hoursToLimit, 0, 'f', 1);
	}

	youtubePollQuotaLabel_->setText(quotaText);
}
