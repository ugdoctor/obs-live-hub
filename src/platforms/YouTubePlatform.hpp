#pragma once
#include <cstdint>
#include <string>

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QTimeZone>

#include "core/PlatformInterface.hpp"

// YouTube Live Chat API v3 によるポーリング実装。
// broadcastId が空または "me" の場合、OAuth トークンを使って
// liveBroadcasts.list?broadcastStatus=active&mine=true で liveChatId を自動取得する。
// 特定の broadcastId が指定された場合は videos.list で liveChatId を解決する。
// クォータ管理: 1日 10,000 ユニット、liveChatMessages.list は 5 ユニット/回。
// Access Token は期限切れ5分前に Refresh Token で自動更新する。
// すべての HTTPS 呼び出しは WinHTTP (Windows SChannel) を使用。QNetworkAccessManager 不使用。
class YouTubePlatform : public QObject, public PlatformInterface {
	Q_OBJECT
public:
	explicit YouTubePlatform(const QString &apiKey, const QString &broadcastId,
				 const QString &accessToken, bool ignoreQuota = false,
				 QObject *parent = nullptr);
	~YouTubePlatform() override;

	void connect() override;
	void disconnect() override;
	std::string getPlatformName() const override { return "YouTube Live"; }
	bool isConnected() const override { return connected_; }

signals:
	void commentReceived(const QString &author, const QString &message,
			     const QString &avatarUrl);
	void quotaWarning(int remainingMinutes);
	void quotaStopped();
	void quotaCleared();    // UTC 0:00 クォータリセット時
	void authFailed();      // HTTP 401 受信時
	void pollSucceeded();   // liveChatMessages.list HTTP 200 成功時
	void errorOccurred(const QString &errorMessage);

private slots:
	void fetchMessages(); // タイマーから呼ばれる入口

private:
	void fetchActiveBroadcast(); // liveBroadcasts.list?mine=true
	void fetchVideoInfo();       // videos.list?id={broadcastId_}
	void startPolling();
	bool consumeQuota();    // 5 ユニット消費。停止すべきなら false を返す
	void doFetchMessages(); // 実際の liveChatMessages.list リクエスト

	// WinHTTP GET コールバック（メインスレッドで実行）
	void onBroadcastInfoResult(bool ok, int statusCode, const std::string &body,
				   const std::string &error);
	void onVideoInfoResult(bool ok, int statusCode, const std::string &body,
			       const std::string &error);
	void onMessagesResult(bool ok, int statusCode, const std::string &body,
			      const std::string &error);
	// WinHTTP トークン更新コールバック（メインスレッドで実行）
	void onTokenRefreshResult(bool ok, const std::string &newToken, int64_t expiresIn);

	QTimer *pollTimer_;
	QString apiKey_;
	QString broadcastId_;
	QString oauthToken_; // 現在の Access Token
	QString liveChatId_;
	QString nextPageToken_;
	bool connected_ = false;
	bool ignoreQuota_;
	bool refreshingToken_ = false;    // Token 更新中フラグ（二重エントリ防止）
	bool authErrorActive_ = false;    // 401 エラー中フラグ（復帰時のみ error_clear を送る）
	int broadcastRetryCount_ = 0;     // liveBroadcasts.list "ready" 待ちリトライ回数

	// クォータ追跡（UTC 0:00 でリセット）
	int dailyUnitsUsed_ = 0;
	QDateTime quotaResetTime_;
	bool quotaWarningSent_ = false;

	static constexpr int DEFAULT_POLL_INTERVAL_MS = 5000;
	static constexpr int DAILY_QUOTA_LIMIT = 10000;
	static constexpr int UNITS_PER_CALL = 5;
	static constexpr double UNITS_PER_HOUR = 720.0;
	static constexpr int WARNING_MINUTES = 30;
	static constexpr int64_t TOKEN_REFRESH_MARGIN_SEC = 300;
	static constexpr int BROADCAST_READY_RETRY_INTERVAL_MS = 15000;
	static constexpr int BROADCAST_READY_RETRY_MAX = 3;
};
