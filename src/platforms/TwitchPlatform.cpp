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

#include "TwitchPlatform.hpp"

#include <chrono>

#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#include <winhttp.h>
#endif

static const char *TAG = "TwitchPlatform";

TwitchPlatform::TwitchPlatform(EventBus<CommentEvent> &bus, const QString &oauthToken,
			       const QString &username, const QString &channel,
			       const QString &clientId, QObject *parent)
	: QObject(parent),
	  bus_(bus),
	  oauthToken_(oauthToken.toStdString()),
	  username_(username.toLower().toStdString()),
	  channel_(channel.toLower().toStdString()),
	  clientId_(clientId.toStdString())
{
}

TwitchPlatform::~TwitchPlatform()
{
	disconnect();
}

void TwitchPlatform::connect()
{
	if (connected_.load() || receiverThread_.joinable())
		return;

	if (username_.empty() || channel_.empty()) {
		emit errorOccurred("Twitch: username or channel is not configured");
		return;
	}

	stopRequested_.store(false);
	receiverThread_ = std::thread(&TwitchPlatform::receiverLoop, this);
}

void TwitchPlatform::disconnect()
{
	stopRequested_.store(true);
	socket_.close(); // recv() 中のスレッドをソケットエラーで抜けさせる

	if (receiverThread_.joinable())
		receiverThread_.join();

	connected_.store(false);
}

void TwitchPlatform::sendRaw(const std::string &line)
{
	if (!socket_.send(line + "\r\n"))
		obs_log(LOG_WARNING, "[%s] send failed (%zu bytes dropped)", TAG, line.size());
}

// ---- 受信スループ（バックグラウンドスレッド） ----

void TwitchPlatform::receiverLoop()
{
	using Clock = std::chrono::steady_clock;
	// 30 秒ごとにタイムアウト。5 分間データがなければ再接続。
	static constexpr auto KEEPALIVE_TIMEOUT = std::chrono::minutes(5);
	static constexpr int RECONNECT_DELAY_MS = 5000;

	// ---- 外側ループ：切断後に自動再接続 ----
	while (!stopRequested_.load()) {

		// ---- 接続 ----
		obs_log(LOG_INFO, "[%s] Connecting to %s:%u (TLS)...", TAG, TWITCH_HOST, TWITCH_PORT);

		if (!socket_.connectTo(TWITCH_HOST, TWITCH_PORT)) {
			if (stopRequested_.load())
				break;
			obs_log(LOG_WARNING, "[%s] TLS connection failed. Retry in %d s.", TAG,
				RECONNECT_DELAY_MS / 1000);
			emit errorOccurred("Twitch: TLS connection failed");
			if (!connErrorActive_) {
				connErrorActive_ = true;
				emit connectionLost();
			}
			for (int i = 0; i < RECONNECT_DELAY_MS / 100 && !stopRequested_.load(); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		obs_log(LOG_INFO, "[%s] Connected. Authenticating as %s... (token length: %zu)", TAG,
			username_.c_str(), oauthToken_.size());

		// ---- IRC 認証（トークンはログに出さない） ----
		const std::string pass =
			(oauthToken_.find("oauth:") == 0) ? oauthToken_ : "oauth:" + oauthToken_;
		obs_log(LOG_INFO, "[%s] >> PASS oauth:*** (%zu chars)", TAG, pass.size());
		sendRaw("PASS " + pass);
		obs_log(LOG_INFO, "[%s] >> NICK %s", TAG, username_.c_str());
		sendRaw("NICK " + username_);
		obs_log(LOG_INFO, "[%s] >> CAP REQ :twitch.tv/tags twitch.tv/commands", TAG);
		sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands");
		obs_log(LOG_INFO, "[%s] Auth commands sent. Waiting for server response...", TAG);

		// ---- 内側ループ：受信処理 ----
		bool joined = false;
		bool shouldReconnect = false;
		auto lastActivity = Clock::now();
		std::string lineBuf;

		while (!stopRequested_.load()) {
			std::string chunk;
			const bool ok = socket_.receive(chunk, 30000);

			if (!ok) {
				// 接続切断またはエラー
				if (!stopRequested_.load()) {
					const char *phase = joined ? "接続維持中" : "認証中";
					obs_log(LOG_WARNING, "[%s] connection lost (%s)", TAG, phase);
					emit errorOccurred("Twitch: connection lost");
					if (!connErrorActive_) {
						connErrorActive_ = true;
						emit connectionLost();
					}
					shouldReconnect = true;
				}
				break;
			}

			if (chunk.empty()) {
				// タイムアウト（30 秒データなし）— 切断ではない
				const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
					Clock::now() - lastActivity);
				obs_log(LOG_INFO, "[%s] recv timeout (%lld s since last data)", TAG,
					static_cast<long long>(elapsed.count()));

				if (elapsed > KEEPALIVE_TIMEOUT) {
					obs_log(LOG_WARNING,
						"[%s] No data for 5 min — reconnecting...", TAG);
					emit errorOccurred(
						"Twitch: keepalive timeout — 再接続します");
					if (!connErrorActive_) {
						connErrorActive_ = true;
						emit connectionLost();
					}
					shouldReconnect = true;
					break;
				}

				// 接続確認 PING を送信
				obs_log(LOG_INFO, "[%s] >> PING :tmi.twitch.tv", TAG);
				sendRaw("PING :tmi.twitch.tv");
				continue;
			}

			// データ受信 — 最終アクティビティ時刻を更新
			lastActivity = Clock::now();

			// 生データをログ（CR/LF 可視化）
			{
				std::string display;
				display.reserve(chunk.size() * 2);
				for (unsigned char c : chunk) {
					if (c == '\r')
						display += "<CR>";
					else if (c == '\n')
						display += "<LF>";
					else if (c < 0x20 || c > 0x7e)
						display += '?';
					else
						display += static_cast<char>(c);
				}
				obs_log(LOG_INFO, "[%s] RAW recv [%zu bytes]: %s", TAG, chunk.size(),
					display.c_str());
			}

			lineBuf += chunk;

			// CR+LF 区切りで行単位に処理
			size_t pos;
			while ((pos = lineBuf.find("\r\n")) != std::string::npos) {
				const std::string line = lineBuf.substr(0, pos);
				lineBuf.erase(0, pos + 2);

				if (line.empty())
					continue;

				obs_log(LOG_INFO, "[%s] << %s", TAG, line.c_str());

				// PING キープアライブ（サーバー → クライアント）
				if (line.substr(0, 4) == "PING") {
					const std::string pong = "PONG" + line.substr(4);
					obs_log(LOG_INFO, "[%s] >> %s", TAG, pong.c_str());
					sendRaw(pong);
					continue;
				}

				// PONG（クライアント PING への応答）
				if (line.substr(0, 4) == "PONG") {
					obs_log(LOG_INFO, "[%s] << PONG (keepalive confirmed)", TAG);
					continue;
				}

				// RECONNECT
				if (line.find(" RECONNECT") != std::string::npos) {
					obs_log(LOG_INFO, "[%s] Server requested reconnect.", TAG);
					shouldReconnect = true;
					break;
				}

				// NOTICE（認証失敗等のエラーメッセージ）
				if (line.find(" NOTICE ") != std::string::npos) {
					obs_log(LOG_WARNING, "[%s] NOTICE received: %s", TAG,
						line.c_str());
					if (line.find("Login authentication failed") !=
						    std::string::npos ||
					    line.find("Improperly formatted auth") != std::string::npos) {
						emit errorOccurred(
							"Twitch: 認証失敗 — OAuthトークンまたはユーザー名を確認してください");
						if (!authErrorActive_) {
							authErrorActive_ = true;
							emit authFailed();
						}
					}
					continue;
				}

				// 376/001 → JOIN
				if (!joined && (line.find(" 376 ") != std::string::npos ||
						line.find(" 001 ") != std::string::npos)) {
					sendRaw("JOIN #" + channel_);
					connected_.store(true);
					joined = true;
					obs_log(LOG_INFO, "[%s] Joined #%s", TAG, channel_.c_str());
					if (authErrorActive_ || connErrorActive_) {
						authErrorActive_ = false;
						connErrorActive_ = false;
						emit joinSucceeded();
					}
					continue;
				}

				if (line.find(" PRIVMSG ") != std::string::npos)
					parseLine(line);
			}

			if (shouldReconnect)
				break;
		}

		// ---- セッション終了処理 ----
		socket_.close();
		connected_.store(false);

		if (!shouldReconnect || stopRequested_.load())
			break;

		obs_log(LOG_INFO, "[%s] Reconnecting in %d s...", TAG, RECONNECT_DELAY_MS / 1000);
		for (int i = 0; i < RECONNECT_DELAY_MS / 100 && !stopRequested_.load(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	connected_.store(false);
	obs_log(LOG_INFO, "[%s] Receiver loop ended.", TAG);
}

// ---- IRC PRIVMSG パース ----

void TwitchPlatform::parseLine(const std::string &line)
{
	// PRIVMSG コマンドを含む行のみ処理（呼び出し元フィルターの二重確認）
	if (line.find(" PRIVMSG ") == std::string::npos) {
		obs_log(LOG_WARNING, "[%s] parseLine called for non-PRIVMSG line, skipping: %s",
			TAG, line.c_str());
		return;
	}

	std::string displayName;
	std::string userId;
	std::string messageText;

	if (!line.empty() && line[0] == '@') {
		// IRCv3 タグあり: @tag1=val1;tag2=val2 :nick!... PRIVMSG #ch :message
		const size_t spacePos = line.find(' ');
		if (spacePos == std::string::npos)
			return;

		const std::string tags = line.substr(1, spacePos - 1);
		const std::string rest = line.substr(spacePos + 1);

		// display-name と user-id タグを一括抽出
		size_t start = 0;
		while (start < tags.size()) {
			const size_t end = tags.find(';', start);
			const std::string tag =
				tags.substr(start, end == std::string::npos ? end : end - start);
			const size_t eq = tag.find('=');
			if (eq != std::string::npos) {
				const std::string key = tag.substr(0, eq);
				const std::string val = tag.substr(eq + 1);
				if (key == "display-name")
					displayName = val;
				else if (key == "user-id")
					userId = val;
			}
			if (end == std::string::npos)
				break;
			start = end + 1;
		}

		const size_t privmsgPos = rest.find("PRIVMSG");
		if (privmsgPos == std::string::npos)
			return;
		const size_t colonPos = rest.find(" :", privmsgPos);
		if (colonPos == std::string::npos)
			return;
		messageText = rest.substr(colonPos + 2);

	} else if (!line.empty() && line[0] == ':') {
		// タグなし: :nick!nick@nick.tmi.twitch.tv PRIVMSG #ch :message
		const size_t bangPos = line.find('!');
		if (bangPos != std::string::npos && bangPos > 1)
			displayName = line.substr(1, bangPos - 1);

		const size_t colonPos = line.rfind(" :");
		if (colonPos == std::string::npos)
			return;
		messageText = line.substr(colonPos + 2);
	}

	if (messageText.empty())
		return;

	// user-id があれば Helix API でアバター URL を取得（キャッシュ済みは即返す）
	const std::string avatarUrl = userId.empty() ? "" : fetchAvatarUrl(userId);

	CommentEvent ev;
	ev.platformName = "Twitch";
	ev.authorName = displayName;
	ev.message = messageText;
	ev.avatarUrl = avatarUrl;
	bus_.publish(ev);
}

// ---- アバター URL キャッシュ ----

std::string TwitchPlatform::fetchAvatarUrl(const std::string &userId)
{
	{
		std::lock_guard<std::mutex> lock(avatarCacheMutex_);
		const auto it = avatarCache_.find(userId);
		if (it != avatarCache_.end())
			return it->second;
	}

	const std::string url = doFetchAvatarUrl(userId);

	// 空文字もキャッシュして同じユーザーの再リクエストを防ぐ
	{
		std::lock_guard<std::mutex> lock(avatarCacheMutex_);
		avatarCache_[userId] = url;
	}
	return url;
}

// ---- WinHTTP で Twitch Helix API を呼び出す ----

#ifdef _WIN32
std::string TwitchPlatform::doFetchAvatarUrl(const std::string &userId)
{
	if (clientId_.empty()) {
		obs_log(LOG_INFO, "[%s] avatar: Client ID 未設定のためスキップ", TAG);
		return {};
	}

	// "oauth:" プレフィックスを除いた Bearer トークン
	const std::string token = (oauthToken_.size() > 6 && oauthToken_.substr(0, 6) == "oauth:")
					  ? oauthToken_.substr(6)
					  : oauthToken_;
	if (token.empty())
		return {};

	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		obs_log(LOG_WARNING, "[%s] avatar: WinHttpOpen failed (%lu)", TAG, GetLastError());
		return {};
	}

	HINTERNET hConnect =
		WinHttpConnect(hSession, L"api.twitch.tv", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		obs_log(LOG_WARNING, "[%s] avatar: WinHttpConnect failed (%lu)", TAG, GetLastError());
		WinHttpCloseHandle(hSession);
		return {};
	}

	const std::wstring path =
		L"/helix/users?id=" + std::wstring(userId.begin(), userId.end());
	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
				   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		obs_log(LOG_WARNING, "[%s] avatar: WinHttpOpenRequest failed (%lu)", TAG,
			GetLastError());
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	const std::wstring wClientId(clientId_.begin(), clientId_.end());
	const std::wstring wToken(token.begin(), token.end());
	const std::wstring headers =
		L"Client-ID: " + wClientId + L"\r\nAuthorization: Bearer " + wToken;

	if (!WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(-1),
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		obs_log(LOG_WARNING, "[%s] avatar: WinHttpSendRequest failed (%lu)", TAG,
			GetLastError());
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	if (!WinHttpReceiveResponse(hRequest, nullptr)) {
		obs_log(LOG_WARNING, "[%s] avatar: WinHttpReceiveResponse failed (%lu)", TAG,
			GetLastError());
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	std::string body;
	DWORD avail = 0;
	while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
		std::string buf(static_cast<size_t>(avail), '\0');
		DWORD bytesRead = 0;
		if (WinHttpReadData(hRequest, &buf[0], avail, &bytesRead) && bytesRead > 0)
			body.append(buf.data(), bytesRead);
		else
			break;
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	// JSON から "profile_image_url":"https://..." を抽出
	const std::string key = "\"profile_image_url\"";
	const auto kpos = body.find(key);
	if (kpos == std::string::npos) {
		obs_log(LOG_WARNING, "[%s] avatar: profile_image_url not found for user %s", TAG,
			userId.c_str());
		return {};
	}
	const auto colon = body.find(':', kpos + key.size());
	if (colon == std::string::npos)
		return {};
	const auto open = body.find('"', colon + 1);
	if (open == std::string::npos)
		return {};
	const auto close = body.find('"', open + 1);
	if (close == std::string::npos)
		return {};

	const std::string url = body.substr(open + 1, close - open - 1);
	if (!url.empty())
		obs_log(LOG_INFO, "[%s] avatar fetched for user %s", TAG, userId.c_str());
	return url;
}
#else
std::string TwitchPlatform::doFetchAvatarUrl(const std::string &)
{
	return {};
}
#endif
