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

#include "YouTubePlatform.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <string>
#include <thread>

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <obs-module.h>
#include <plugin-support.h>

#include "auth/GoogleOAuth.hpp"
#include "core/PluginConfig.hpp"

static const char *TAG = "YouTubePlatform";

// ============================================================
// WinHTTP GET ヘルパー
// Qt TLS / QNetworkAccessManager を一切使わない。
// Windows SChannel が TLS を処理するため obs-deps の OpenSSL 不要。
// ============================================================

struct YouTubeHttpResult {
	bool ok = false;
	int statusCode = 0;
	std::string body;
	std::string error;
};

#ifdef _WIN32
static YouTubeHttpResult doWinHttpGet(const std::wstring &host, const std::wstring &path,
				      const std::string &bearerToken = "")
{
	YouTubeHttpResult result;

	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		result.error = "WinHttpOpen failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		return result;
	}

	HINTERNET hConnect =
		WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		result.error = "WinHttpConnect failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hSession);
		return result;
	}

	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
				   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		result.error =
			"WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return result;
	}

	// Authorization ヘッダーを追加（OAuth トークンがある場合）
	// Bearer トークンは ASCII のみなので wstring への変換は安全
	if (!bearerToken.empty()) {
		const std::wstring bearerW(bearerToken.begin(), bearerToken.end());
		const std::wstring authHeader = L"Authorization: Bearer " + bearerW + L"\r\n";
		WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), static_cast<DWORD>(-1),
					 WINHTTP_ADDREQ_FLAG_ADD);
	}

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) {
		result.error =
			"WinHttpSendRequest failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return result;
	}

	if (!WinHttpReceiveResponse(hRequest, nullptr)) {
		result.error =
			"WinHttpReceiveResponse failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return result;
	}

	// HTTP ステータスコードを取得
	DWORD statusCode = 0;
	DWORD statusCodeSize = sizeof(statusCode);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
			    WINHTTP_NO_HEADER_INDEX);
	result.statusCode = static_cast<int>(statusCode);

	// レスポンスボディを読み取る（チャンク分割に対応）
	DWORD bytesAvailable = 0;
	while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
		std::string chunk(bytesAvailable, '\0');
		DWORD bytesRead = 0;
		if (!WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead))
			break;
		result.body.append(chunk, 0, bytesRead);
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	result.ok = true;
	return result;
}
#else
static YouTubeHttpResult doWinHttpGet(const std::wstring &, const std::wstring &,
				      const std::string & = "")
{
	YouTubeHttpResult r;
	r.error = "Windows only";
	return r;
}
#endif

// ============================================================
// YouTubePlatform 実装
// ============================================================

YouTubePlatform::YouTubePlatform(const QString &apiKey, const QString &broadcastId,
				 const QString &accessToken, bool ignoreQuota, QObject *parent)
	: QObject(parent),
	  apiKey_(apiKey),
	  broadcastId_(broadcastId),
	  oauthToken_(accessToken),
	  ignoreQuota_(ignoreQuota)
{
	pollTimer_ = new QTimer(this);
	pollTimer_->setSingleShot(true);
	QObject::connect(pollTimer_, &QTimer::timeout, this, &YouTubePlatform::fetchMessages);
}

YouTubePlatform::~YouTubePlatform()
{
	disconnect();
}

// ---- 接続 ----

void YouTubePlatform::connect()
{
	if (connected_)
		return;
	if (apiKey_.isEmpty()) {
		emit errorOccurred("YouTube: API キーが設定されていません");
		return;
	}

	// 動画ID・クォータカウンターを初期化
	resolvedBroadcastId_.clear();
	dailyUnitsUsed_ = 0;
	quotaWarningSent_ = false;
	const QDate tomorrow = QDateTime::currentDateTimeUtc().date().addDays(1);
	quotaResetTime_ = QDateTime(tomorrow, QTime(0, 0, 0), QTimeZone::utc());

	if (broadcastId_.isEmpty() || broadcastId_ == "me") {
		if (oauthToken_.isEmpty()) {
			emit errorOccurred(
				"YouTube: 自動検出には Access Token が必要です。\n"
				"設定ダイアログで Google アカウントと連携してください。");
			return;
		}
		broadcastRetryCount_ = 0;
		fetchActiveBroadcast();
	} else {
		fetchVideoInfo();
	}
}

void YouTubePlatform::disconnect()
{
	connected_ = false;
	refreshingToken_ = false;
	retryingAfter401_ = false;
	pollTimer_->stop();
	liveChatId_.clear();
	nextPageToken_.clear();
	resolvedBroadcastId_.clear();
	obs_log(LOG_INFO, "[%s] Disconnected", TAG);
}

// ---- liveBroadcasts.list?mine=true（OAuth 必須）----

void YouTubePlatform::fetchActiveBroadcast()
{
	obs_log(LOG_INFO, "[%s] Fetching active broadcast via WinHTTP (mine=true)...", TAG);

	QUrl url("https://www.googleapis.com/youtube/v3/liveBroadcasts");
	QUrlQuery query;
	query.addQueryItem("part", "snippet,status");
	query.addQueryItem("mine", "true");
	if (!apiKey_.isEmpty())
		query.addQueryItem("key", apiKey_);
	url.setQuery(query);

	const std::wstring wpath =
		(url.path() + "?" + url.query(QUrl::FullyEncoded)).toStdWString();
	const std::string token = oauthToken_.toStdString();

	QPointer<YouTubePlatform> self = this;
	std::thread([self, wpath, token]() {
		auto res = doWinHttpGet(L"www.googleapis.com", wpath, token);
		const bool ok = res.ok;
		const int code = res.statusCode;
		std::string body = std::move(res.body);
		std::string error = std::move(res.error);
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, ok, code, body, error]() {
				if (self)
					self->onBroadcastInfoResult(ok, code, body, error);
			},
			Qt::QueuedConnection);
	}).detach();
}

void YouTubePlatform::onBroadcastInfoResult(bool ok, int statusCode, const std::string &body,
					    const std::string &error)
{
	if (!ok) {
		const QString msg =
			QString("YouTube WinHTTP エラー（liveBroadcasts.list）: %1")
				.arg(QString::fromStdString(error));
		obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
		emit errorOccurred(msg);
		return;
	}

	if (statusCode != 200) {
		const QString msg =
			QString("YouTube liveBroadcasts.list HTTP %1").arg(statusCode);
		obs_log(LOG_WARNING, "[%s] %s: %.*s", TAG, msg.toUtf8().constData(),
			static_cast<int>(std::min(body.size(), size_t(200))), body.c_str());
		if (statusCode == 401) {
			const auto &cfg = PluginConfig::instance();
			if (!retryingAfter401_ && !cfg.youtubeRefreshToken.empty() &&
			    !cfg.youtubeClientId.empty() && !cfg.youtubeClientSecret.empty()) {
				retryingAfter401_ = true;
				refreshingToken_ = true;
				obs_log(LOG_INFO,
					"[%s] HTTP 401 (liveBroadcasts) - attempting token refresh...",
					TAG);
				QPointer<YouTubePlatform> self = this;
				GoogleOAuthTokenClient::refreshAccessTokenAsync(
					cfg.youtubeClientId, cfg.youtubeClientSecret,
					cfg.youtubeRefreshToken, [self](GoogleTokenResult res) {
						QMetaObject::invokeMethod(
							QCoreApplication::instance(),
							[self, res]() {
								if (!self)
									return;
								self->refreshingToken_ = false;
								if (res.ok && !res.accessToken.empty()) {
									self->oauthToken_ =
										QString::fromStdString(
											res.accessToken);
									auto &cfg2 =
										PluginConfig::instance();
									cfg2.youtubeAccessToken =
										res.accessToken;
									cfg2.youtubeTokenExpiry =
										QDateTime::currentSecsSinceEpoch() +
										res.expiresIn;
									cfg2.save();
									obs_log(LOG_INFO,
										"[GoogleOAuth] Token refreshed successfully");
									self->fetchActiveBroadcast();
								} else {
									obs_log(LOG_WARNING,
										"[GoogleOAuth] Token refresh failed: %s",
										res.error.c_str());
									self->retryingAfter401_ = false;
									emit self->authFailed();
								}
							},
							Qt::QueuedConnection);
					});
				return;
			}
			retryingAfter401_ = false;
			emit authFailed();
			return;
		}
		emit errorOccurred(msg);
		return;
	}

	const QJsonDocument doc =
		QJsonDocument::fromJson(QByteArray::fromStdString(body));
	const QJsonArray items = doc.object()["items"].toArray();

	if (items.isEmpty()) {
		const QString msg = "YouTube: ブロードキャストが見つかりません (mine=true)";
		obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
		emit errorOccurred(msg);
		return;
	}

	// lifeCycleStatus が "live"/"liveStarting" を優先し、"ready" を次点候補とする。
	// broadcastStatus と mine=true は同時使用不可のためクライアント側でフィルタする。
	QString readyCandidateChatId;
	QString resolvedVideoId; // item.id が YouTube 動画ID
	for (const QJsonValue &item : items) {
		const QJsonObject obj = item.toObject();
		const QString lifeCycleStatus =
			obj["status"].toObject()["lifeCycleStatus"].toString();
		obs_log(LOG_INFO, "[%s] broadcast lifeCycleStatus: %s", TAG,
			lifeCycleStatus.toUtf8().constData());
		if (lifeCycleStatus == "live" || lifeCycleStatus == "liveStarting") {
			liveChatId_    = obj["snippet"].toObject()["liveChatId"].toString();
			resolvedVideoId = obj["id"].toString();
			break;
		}
		if (lifeCycleStatus == "ready" && readyCandidateChatId.isEmpty())
			readyCandidateChatId = obj["snippet"].toObject()["liveChatId"].toString();
	}

	if (!liveChatId_.isEmpty()) {
		// "live" / "liveStarting" が見つかった
		obs_log(LOG_INFO, "[%s] liveChatId 自動取得完了: %s", TAG,
			liveChatId_.toUtf8().constData());
		resolvedBroadcastId_ = resolvedVideoId;
		obs_log(LOG_INFO, "[%s] broadcastResolved: videoId=%s", TAG,
			resolvedBroadcastId_.toUtf8().constData());
		broadcastRetryCount_ = 0;
		startPolling();
		emit broadcastResolved(resolvedBroadcastId_);
		return;
	}

	if (!readyCandidateChatId.isEmpty() &&
	    broadcastRetryCount_ < BROADCAST_READY_RETRY_MAX) {
		// "ready" のみ見つかった → 15 秒後にリトライ
		++broadcastRetryCount_;
		obs_log(LOG_INFO,
			"[%s] ブロードキャストが ready 状態です。15 秒後にリトライします（%d/%d）...",
			TAG, broadcastRetryCount_, BROADCAST_READY_RETRY_MAX);
		QTimer::singleShot(BROADCAST_READY_RETRY_INTERVAL_MS, this,
				   &YouTubePlatform::fetchActiveBroadcast);
		return;
	}

	// "ready" でのリトライ上限超過、または候補なし
	const QString msg = readyCandidateChatId.isEmpty()
				    ? "YouTube: live/liveStarting/ready 状態のブロードキャストが見つかりません"
				    : QString("YouTube: ブロードキャストが %1 回リトライしても live になりませんでした")
					      .arg(BROADCAST_READY_RETRY_MAX);
	obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
	emit errorOccurred(msg);
}

// ---- videos.list（手動 broadcastId 指定時）----

void YouTubePlatform::fetchVideoInfo()
{
	obs_log(LOG_INFO, "[%s] Fetching live chat ID via WinHTTP for broadcast: %s", TAG,
		broadcastId_.toUtf8().constData());

	QUrl url("https://www.googleapis.com/youtube/v3/videos");
	QUrlQuery query;
	query.addQueryItem("part", "liveStreamingDetails");
	query.addQueryItem("id", broadcastId_);
	query.addQueryItem("key", apiKey_);
	url.setQuery(query);

	const std::wstring wpath =
		(url.path() + "?" + url.query(QUrl::FullyEncoded)).toStdWString();

	QPointer<YouTubePlatform> self = this;
	std::thread([self, wpath]() {
		auto res = doWinHttpGet(L"www.googleapis.com", wpath);
		const bool ok = res.ok;
		const int code = res.statusCode;
		std::string body = std::move(res.body);
		std::string error = std::move(res.error);
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, ok, code, body, error]() {
				if (self)
					self->onVideoInfoResult(ok, code, body, error);
			},
			Qt::QueuedConnection);
	}).detach();
}

void YouTubePlatform::onVideoInfoResult(bool ok, int statusCode, const std::string &body,
					const std::string &error)
{
	if (!ok) {
		const QString msg = QString("YouTube WinHTTP エラー（videos.list）: %1")
					    .arg(QString::fromStdString(error));
		obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
		emit errorOccurred(msg);
		return;
	}

	if (statusCode != 200) {
		const QString msg = QString("YouTube videos.list HTTP %1").arg(statusCode);
		obs_log(LOG_WARNING, "[%s] %s: %.*s", TAG, msg.toUtf8().constData(),
			static_cast<int>(std::min(body.size(), size_t(200))), body.c_str());
		emit errorOccurred(msg);
		return;
	}

	const QJsonDocument doc =
		QJsonDocument::fromJson(QByteArray::fromStdString(body));
	const QJsonArray items = doc.object()["items"].toArray();

	if (items.isEmpty()) {
		const QString msg =
			"YouTube: 指定したブロードキャストが見つからないか、配信中ではありません";
		obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
		emit errorOccurred(msg);
		return;
	}

	liveChatId_ = items[0]
			      .toObject()["liveStreamingDetails"]
			      .toObject()["activeLiveChatId"]
			      .toString();

	if (liveChatId_.isEmpty()) {
		const QString msg =
			"YouTube: このブロードキャストにはアクティブなライブチャットがありません";
		obs_log(LOG_WARNING, "[%s] %s", TAG, msg.toUtf8().constData());
		emit errorOccurred(msg);
		return;
	}

	obs_log(LOG_INFO, "[%s] liveChatId 取得完了: %s", TAG,
		liveChatId_.toUtf8().constData());
	// broadcastId_ が動画IDそのもの（fetchVideoInfo は手動ID指定ルート）
	resolvedBroadcastId_ = broadcastId_;
	obs_log(LOG_INFO, "[%s] broadcastResolved: videoId=%s", TAG,
		resolvedBroadcastId_.toUtf8().constData());
	startPolling();
	emit broadcastResolved(resolvedBroadcastId_);
}

// ---- ポーリング間隔取得 ----

int YouTubePlatform::pollIntervalMs() const
{
	const int sec = qMax(5, PluginConfig::instance().youtubePollInterval);
	return sec * 1000;
}

// ---- ポーリング開始 ----

void YouTubePlatform::startPolling()
{
	connected_ = true;
	retryingAfter401_ = false;
	const int ms = pollIntervalMs();
	obs_log(LOG_INFO, "[%s] ポーリング開始（間隔: %d ms）", TAG, ms);
	pollTimer_->start(ms);
}

// ---- クォータ消費チェック ----
// 5 ユニット消費。クォータ超過なら false を返し quotaStopped() を emit する。
bool YouTubePlatform::consumeQuota()
{
	if (ignoreQuota_)
		return true;

	// UTC 0:00 を過ぎていたらリセット
	if (QDateTime::currentDateTimeUtc() >= quotaResetTime_) {
		obs_log(LOG_INFO, "[%s] クォータリセット（UTC 0:00 経過）", TAG);
		dailyUnitsUsed_ = 0;
		quotaWarningSent_ = false;
		const QDate tomorrow = QDateTime::currentDateTimeUtc().date().addDays(1);
		quotaResetTime_ = QDateTime(tomorrow, QTime(0, 0, 0), QTimeZone::utc());
		emit quotaCleared();
	}

	dailyUnitsUsed_ += UNITS_PER_CALL;
	const int remaining = DAILY_QUOTA_LIMIT - dailyUnitsUsed_;

	if (remaining <= 0) {
		obs_log(LOG_WARNING, "[%s] 日次クォータ上限到達。ポーリングを停止します。", TAG);
		connected_ = false;
		emit quotaStopped();
		return false;
	}

	const int remainingMinutes = static_cast<int>(remaining * 60.0 / UNITS_PER_HOUR);
	if (!quotaWarningSent_ && remainingMinutes <= WARNING_MINUTES) {
		quotaWarningSent_ = true;
		obs_log(LOG_WARNING, "[%s] クォータ警告: 残り約 %d 分", TAG, remainingMinutes);
		emit quotaWarning(remainingMinutes);
	}

	return true;
}

// ---- コメントポーリング（タイマー入口）----
// Access Token の有効期限を確認し、必要なら更新してから doFetchMessages() を呼ぶ。
void YouTubePlatform::fetchMessages()
{
	if (!connected_ || refreshingToken_)
		return;

	// Refresh Token がある場合のみ有効期限チェックを行う
	const auto &cfg = PluginConfig::instance();
	if (!cfg.youtubeRefreshToken.empty() && cfg.youtubeTokenExpiry > 0) {
		const int64_t now = QDateTime::currentSecsSinceEpoch();
		if (now >= cfg.youtubeTokenExpiry - TOKEN_REFRESH_MARGIN_SEC) {
			if (cfg.youtubeClientId.empty() || cfg.youtubeClientSecret.empty()) {
				obs_log(LOG_WARNING,
					"[%s] Token expired but client credentials missing, proceeding anyway",
					TAG);
				doFetchMessages();
				return;
			}

			refreshingToken_ = true;
			obs_log(LOG_INFO, "[%s] Access Token 更新中（WinHTTP）...", TAG);

			QPointer<YouTubePlatform> self = this;
			GoogleOAuthTokenClient::refreshAccessTokenAsync(
				cfg.youtubeClientId, cfg.youtubeClientSecret,
				cfg.youtubeRefreshToken, [self](GoogleTokenResult res) {
					QMetaObject::invokeMethod(
						QCoreApplication::instance(),
						[self, res]() {
							if (self)
								self->onTokenRefreshResult(
									res.ok, res.accessToken,
									res.expiresIn);
						},
						Qt::QueuedConnection);
				});
			return;
		}
	}

	doFetchMessages();
}

// ---- Access Token 更新応答（メインスレッドで実行）----
void YouTubePlatform::onTokenRefreshResult(bool ok, const std::string &newToken,
					   int64_t expiresIn)
{
	refreshingToken_ = false;

	if (ok && !newToken.empty()) {
		oauthToken_ = QString::fromStdString(newToken);
		auto &cfg = PluginConfig::instance();
		cfg.youtubeAccessToken = newToken;
		cfg.youtubeTokenExpiry = QDateTime::currentSecsSinceEpoch() + expiresIn;
		cfg.save();
		obs_log(LOG_INFO, "[%s] Access Token 更新完了（有効期限: %lld 秒後）", TAG,
			static_cast<long long>(expiresIn));
	} else {
		obs_log(LOG_WARNING, "[%s] Token refresh 失敗", TAG);
	}

	if (connected_)
		doFetchMessages();
}

// ---- 実際の liveChatMessages.list リクエスト（WinHTTP GET）----
void YouTubePlatform::doFetchMessages()
{
	if (!consumeQuota())
		return;

	QUrl url("https://www.googleapis.com/youtube/v3/liveChat/messages");
	QUrlQuery query;
	query.addQueryItem("part", "snippet,authorDetails");
	query.addQueryItem("liveChatId", liveChatId_);
	query.addQueryItem("key", apiKey_);
	if (!nextPageToken_.isEmpty())
		query.addQueryItem("pageToken", nextPageToken_);
	url.setQuery(query);

	const std::wstring wpath =
		(url.path() + "?" + url.query(QUrl::FullyEncoded)).toStdWString();
	const std::string token = oauthToken_.toStdString();

	QPointer<YouTubePlatform> self = this;
	std::thread([self, wpath, token]() {
		auto res = doWinHttpGet(L"www.googleapis.com", wpath, token);
		const bool ok = res.ok;
		const int code = res.statusCode;
		std::string body = std::move(res.body);
		std::string error = std::move(res.error);
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, ok, code, body, error]() {
				if (self)
					self->onMessagesResult(ok, code, body, error);
			},
			Qt::QueuedConnection);
	}).detach();
}

void YouTubePlatform::onMessagesResult(bool ok, int statusCode, const std::string &body,
				       const std::string &error)
{
	if (!ok) {
		obs_log(LOG_WARNING, "[%s] WinHTTP GET エラー（liveChatMessages.list）: %s", TAG,
			error.c_str());
		if (connected_)
			pollTimer_->start(pollIntervalMs());
		return;
	}

	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body));
	const QJsonObject root = doc.object();

	// API レベルのエラー（403 = クォータ超過 or 権限なし）
	if (root.contains("error")) {
		const int code = root["error"].toObject()["code"].toInt();
		const QString apiMsg = root["error"].toObject()["message"].toString();
		obs_log(LOG_WARNING, "[%s] API エラー %d: %s", TAG, code,
			apiMsg.toUtf8().constData());
		if (code == 403) {
			connected_ = false;
			emit quotaStopped();
			return;
		}
		if (connected_)
			pollTimer_->start(pollIntervalMs());
		return;
	}

	// HTTP 200 以外はリトライ
	if (statusCode != 200) {
		obs_log(LOG_WARNING, "[%s] liveChatMessages.list HTTP %d", TAG, statusCode);
		if (statusCode == 401) {
			const auto &cfg = PluginConfig::instance();
			if (!retryingAfter401_ && !cfg.youtubeRefreshToken.empty() &&
			    !cfg.youtubeClientId.empty() && !cfg.youtubeClientSecret.empty()) {
				retryingAfter401_ = true;
				refreshingToken_ = true;
				obs_log(LOG_INFO,
					"[%s] HTTP 401 (liveChatMessages) - attempting token refresh...",
					TAG);
				QPointer<YouTubePlatform> self = this;
				GoogleOAuthTokenClient::refreshAccessTokenAsync(
					cfg.youtubeClientId, cfg.youtubeClientSecret,
					cfg.youtubeRefreshToken, [self](GoogleTokenResult res) {
						QMetaObject::invokeMethod(
							QCoreApplication::instance(),
							[self, res]() {
								if (!self)
									return;
								self->refreshingToken_ = false;
								if (res.ok && !res.accessToken.empty()) {
									self->oauthToken_ =
										QString::fromStdString(
											res.accessToken);
									auto &cfg2 =
										PluginConfig::instance();
									cfg2.youtubeAccessToken =
										res.accessToken;
									cfg2.youtubeTokenExpiry =
										QDateTime::currentSecsSinceEpoch() +
										res.expiresIn;
									cfg2.save();
									obs_log(LOG_INFO,
										"[GoogleOAuth] Token refreshed successfully");
									if (self->connected_)
										self->doFetchMessages();
								} else {
									obs_log(LOG_WARNING,
										"[GoogleOAuth] Token refresh failed: %s",
										res.error.c_str());
									self->retryingAfter401_ = false;
									if (!self->authErrorActive_) {
										self->authErrorActive_ = true;
										emit self->authFailed();
									}
									if (self->connected_)
										self->pollTimer_->start(
											self->pollIntervalMs());
								}
							},
							Qt::QueuedConnection);
					});
				return;
			}
			// リフレッシュ済みの再試行も 401 、またはリフレッシュ資格情報なし
			retryingAfter401_ = false;
			if (!authErrorActive_) {
				authErrorActive_ = true;
				emit authFailed();
			}
		}
		if (connected_)
			pollTimer_->start(pollIntervalMs());
		return;
	}

	// API 指定のポーリング間隔とユーザー設定値の大きい方を採用（Google 規約準拠）
	const int configMs = pollIntervalMs();
	const int interval = qMax(configMs, root["pollingIntervalMillis"].toInt(configMs));
	nextPageToken_ = root["nextPageToken"].toString();
	retryingAfter401_ = false;

	if (authErrorActive_) {
		authErrorActive_ = false;
		emit pollSucceeded();
	}

	for (const QJsonValue &item : root["items"].toArray()) {
		const QJsonObject obj = item.toObject();
		const QJsonObject authorDetails = obj["authorDetails"].toObject();
		const QString author = authorDetails["displayName"].toString();
		const QString avatarUrl = authorDetails["profileImageUrl"].toString();
		const QString message = obj["snippet"].toObject()["displayMessage"].toString();
		if (!message.isEmpty())
			emit commentReceived(author, message, avatarUrl);
	}

	if (connected_)
		pollTimer_->start(interval);
}
