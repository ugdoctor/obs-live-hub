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

#include "TwitchOAuth.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <chrono>
#include <string>
#include <thread>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "TwitchOAuth";

// ============================================================
// 内部ユーティリティ
// ============================================================

// HTTP リクエスト文字列からクエリパラメータ値を抽出する
static std::string extractParam(const std::string &request, const std::string &key)
{
	const std::string needle = key + "=";
	const auto pos = request.find(needle);
	if (pos == std::string::npos)
		return {};
	const auto start = pos + needle.size();
	const auto end = request.find_first_of(" &\r\n\t#", start);
	return (end == std::string::npos) ? request.substr(start)
					  : request.substr(start, end - start);
}

// ============================================================
// TwitchOAuthCallbackServer
// ============================================================

void TwitchOAuthCallbackServer::startAsync(std::function<void(const std::string &)> onToken,
					   std::function<void(const std::string &)> onError,
					   int timeoutSec)
{
	std::thread([onToken, onError, timeoutSec]() {
#ifdef _WIN32
		// WSAStartup は参照カウント方式なので重複呼び出し可。
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);

		// ポート選択: 80 → 8767 の順で試みる。
		// redirect_uri=http://localhost （ポートなし）に対応するにはポート 80 が必要。
		const int tryPorts[] = {80, 8767};
		SOCKET listenSock = INVALID_SOCKET;
		int boundPort = 0;

		for (int tryPort : tryPorts) {
			SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == INVALID_SOCKET)
				continue;

			int reuse = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				   reinterpret_cast<const char *>(&reuse), sizeof(reuse));

			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addr.sin_port = htons(static_cast<u_short>(tryPort));

			if (bind(sock, reinterpret_cast<SOCKADDR *>(&addr), sizeof(addr)) == 0 &&
			    listen(sock, 2) == 0) {
				listenSock = sock;
				boundPort = tryPort;
				break;
			}

			obs_log(LOG_INFO, "[%s] CallbackServer: port %d unavailable", TAG, tryPort);
			closesocket(sock);
		}

		if (listenSock == INVALID_SOCKET) {
			obs_log(LOG_WARNING,
				"[%s] CallbackServer: ポート 80/8767 ともに使用不可", TAG);
			if (onError)
				onError("ポート80が使用できません。\n"
					"管理者権限で実行するか、"
					"他のアプリがポート80を使用していないか確認してください。");
			return; // onToken は呼ばない
		}

		obs_log(LOG_INFO, "[%s] CallbackServer: listening on port %d (timeout=%d s)", TAG,
			boundPort, timeoutSec);

		using Clock = std::chrono::steady_clock;
		const auto deadline = Clock::now() + std::chrono::seconds(timeoutSec);
		std::string token;

		// 最大 2 リクエスト処理するループ:
		//   Step1: Twitch からのリダイレクト → JS リダイレクトページを返す
		//   Step2: JS がフラグメントをクエリ化した /callback2 → トークンを取得
		while (token.empty()) {
			const auto now = Clock::now();
			if (now >= deadline) {
				obs_log(LOG_WARNING, "[%s] CallbackServer: timeout", TAG);
				break;
			}
			const long remaining = static_cast<long>(
				std::chrono::duration_cast<std::chrono::seconds>(deadline - now)
					.count());

			struct timeval tv {};
			tv.tv_sec = remaining;
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(listenSock, &fds);
			if (select(0, &fds, nullptr, nullptr, &tv) <= 0) {
				obs_log(LOG_WARNING, "[%s] CallbackServer: select timeout", TAG);
				break;
			}

			SOCKET client = accept(listenSock, nullptr, nullptr);
			if (client == INVALID_SOCKET)
				break;

			char buf[8192];
			const int n = recv(client, buf, static_cast<int>(sizeof(buf) - 1), 0);
			std::string request;
			if (n > 0) {
				buf[n] = '\0';
				request = buf;
			}
			obs_log(LOG_INFO, "[%s] CallbackServer: recv %d bytes: %.120s", TAG, n,
				request.c_str());

			std::string htmlBody;
			if (request.find("GET /callback2") != std::string::npos) {
				// Step2: JS リダイレクトで access_token がクエリに乗ってくる
				token = extractParam(request, "access_token");
				if (token.empty()) {
					obs_log(LOG_WARNING,
						"[%s] CallbackServer: /callback2 に access_token なし",
						TAG);
					htmlBody =
						"<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
						"<body><h2 style='color:red'>エラー</h2>"
						"<p>アクセストークンが取得できませんでした。</p>"
						"</body></html>";
				} else {
					obs_log(LOG_INFO,
						"[%s] CallbackServer: access_token 取得完了 (len=%zu)",
						TAG, token.size());
					htmlBody =
						"<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
						"<body><h2 style='color:green'>Twitch 連携完了</h2>"
						"<p>このタブを閉じて OBS に戻ってください。</p>"
						"</body></html>";
				}
			} else {
				// Step1: Twitch からの初回リダイレクト。
				// フラグメント (#access_token=...) はサーバーに届かないため
				// JS で /callback2?<フラグメント> にリダイレクトさせる。
				obs_log(LOG_INFO,
					"[%s] CallbackServer: step1 — JS リダイレクトページを送信",
					TAG);
				htmlBody =
					"<!DOCTYPE html><html>"
					"<head><meta charset='utf-8'><title>Twitch OAuth</title></head>"
					"<body><p>認証処理中...</p>"
					"<script>"
					"var h=window.location.hash;"
					"if(h.length>1){"
					"window.location.replace('/callback2?'+h.substring(1));"
					"}else{"
					"document.body.innerHTML='<p>エラー: トークンが見つかりません</p>';"
					"}"
					"</script>"
					"</body></html>";
			}

			const std::string response =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/html; charset=utf-8\r\n"
				"Connection: close\r\n\r\n" +
				htmlBody;
			send(client, response.c_str(), static_cast<int>(response.size()), 0);
			closesocket(client);
		}

		closesocket(listenSock);
		onToken(token);
#else
		(void)timeoutSec;
		(void)onError;
		onToken({});
#endif
	}).detach();
}
