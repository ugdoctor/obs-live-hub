#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <QObject>
#include <QString>

#include "TlsSocket.hpp"
#include "core/EventBus.hpp"
#include "core/PlatformInterface.hpp"

// Twitch IRC over TLS (irc.chat.twitch.tv:6697)
// WinSock2 + SChannel で TLS 接続し、std::thread で受信ループを回す。
// connect() / disconnect() はメインスレッドから呼ぶ。
// emit は別スレッドから行われるが Qt::AutoConnection が QueuedConnection に
// 昇格するため UI スレッドへ安全に到達する。
class TwitchPlatform : public QObject, public PlatformInterface {
	Q_OBJECT
public:
	explicit TwitchPlatform(EventBus<CommentEvent> &bus, const QString &oauthToken,
				const QString &username, const QString &channel,
				const QString &clientId = {},
				QObject *parent = nullptr);
	~TwitchPlatform() override;

	void connect() override;
	void disconnect() override;
	std::string getPlatformName() const override { return "Twitch"; }
	bool isConnected() const override { return connected_.load(); }

signals:
	void errorOccurred(const QString &errorMessage);
	void authFailed();       // NOTICE "Login authentication failed"
	void connectionLost();   // 接続断・再接続開始
	void joinSucceeded();    // JOIN 成功（376/001 受信後）

	// ── 課金イベントシグナル ──────────────────────────────────────────────────
	// bits: チア数。userId/displayName が空の場合はログ欠損やアノニマス。
	void bitsReceived(const QString &userId, const QString &displayName,
	                  int bits, const QString &message);
	// msgId: sub / resub / subgift / submysterygift。
	// subPlan: Prime / 1000 / 2000 / 3000。giftCount: subgift 一括件数（通常 1）。
	void subscriptionReceived(const QString &userId, const QString &displayName,
	                          const QString &msgId, const QString &subPlan,
	                          int giftCount, const QString &message);

private:
	void receiverLoop();
	void sendRaw(const std::string &line);
	void parseLine(const std::string &line);
	void parseUserNotice(const std::string &line); // USERNOTICE（サブスク系）処理
	// user-id からアバター URL を返す（キャッシュ済みならキャッシュを返す）
	std::string fetchAvatarUrl(const std::string &userId);
	// WinHTTP で Twitch Helix API を呼び出して profile_image_url を取得する
	std::string doFetchAvatarUrl(const std::string &userId);

	EventBus<CommentEvent> &bus_;
	TlsSocket socket_;
	std::thread receiverThread_;
	std::atomic<bool> connected_{false};
	std::atomic<bool> stopRequested_{false};
	std::string oauthToken_;
	std::string username_;
	std::string channel_;
	std::string clientId_;

	bool authErrorActive_ = false;  // 認証エラー中フラグ（復帰時のみ error_clear を送る）
	bool connErrorActive_ = false;  // 接続断エラー中フラグ（復帰時のみ error_clear を送る）

	std::unordered_map<std::string, std::string> avatarCache_;
	std::mutex avatarCacheMutex_;

	static constexpr const char *TWITCH_HOST = "irc.chat.twitch.tv";
	static constexpr uint16_t TWITCH_PORT = 6697;
};
