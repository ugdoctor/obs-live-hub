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

#include "GoogleOAuth.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <cstdio>
#include <string>
#include <thread>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "GoogleOAuth";

// ============================================================
// 内部ユーティリティ
// ============================================================

// RFC 3986 準拠の URL エンコード（Qt 不使用）
static std::string urlEncode(const std::string &s)
{
	std::string out;
	out.reserve(s.size() * 3);
	for (const unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			char hex[4];
			std::snprintf(hex, sizeof(hex), "%%%02X", c);
			out += hex;
		}
	}
	return out;
}

// JSON ホワイトスペーススキップヘルパー
static size_t skipWs(const std::string &s, size_t pos)
{
	while (pos < s.size() &&
	       (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
		++pos;
	return pos;
}

// フラット JSON から文字列値を取り出す。
// "key" : "value" 形式でコロン前後のホワイトスペースを許容する。
static std::string jsonStr(const std::string &json, const std::string &key)
{
	const std::string needle = "\"" + key + "\"";
	auto pos = json.find(needle);
	if (pos == std::string::npos)
		return {};

	auto cur = skipWs(json, pos + needle.size());
	if (cur >= json.size() || json[cur] != ':')
		return {};
	cur = skipWs(json, cur + 1);
	if (cur >= json.size() || json[cur] != '"')
		return {};
	++cur; // 開き " をスキップ

	const auto end = json.find('"', cur);
	if (end == std::string::npos)
		return {};
	return json.substr(cur, end - cur);
}

// フラット JSON から整数値を取り出す。
// "key" : NNN 形式でコロン前後のホワイトスペースを許容する。
static int64_t jsonInt(const std::string &json, const std::string &key)
{
	const std::string needle = "\"" + key + "\"";
	auto pos = json.find(needle);
	if (pos == std::string::npos)
		return 0;

	auto cur = skipWs(json, pos + needle.size());
	if (cur >= json.size() || json[cur] != ':')
		return 0;
	cur = skipWs(json, cur + 1);
	if (cur >= json.size() || json[cur] < '0' || json[cur] > '9')
		return 0;

	try {
		return static_cast<int64_t>(std::stoll(json.substr(cur)));
	} catch (...) {
		return 0;
	}
}

// ============================================================
// HTTP GET ラインから code=XXXX を抽出する
// ============================================================

static std::string extractCode(const std::string &request)
{
	const std::string key = "code=";
	const auto pos = request.find(key);
	if (pos == std::string::npos)
		return {};
	const auto start = pos + key.size();
	const auto end = request.find_first_of(" &\r\n\t#", start);
	return (end == std::string::npos) ? request.substr(start)
					  : request.substr(start, end - start);
}

// ============================================================
// WinHTTP POST to https://oauth2.googleapis.com/token
// Qt TLS / QNetworkAccessManager を一切使わない。
// Windows SChannel が TLS を処理するため obs-deps の OpenSSL 不要。
// ============================================================

#ifdef _WIN32
static GoogleTokenResult doWinHttpPost(const std::string &body)
{
	GoogleTokenResult result;

	obs_log(LOG_INFO, "[%s] WinHTTP POST to oauth2.googleapis.com/token (body len=%zu)", TAG,
		body.size());

	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		result.error = "WinHttpOpen failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		return result;
	}

	HINTERNET hConnect = WinHttpConnect(hSession, L"oauth2.googleapis.com",
					    INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		result.error = "WinHttpConnect failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hSession);
		return result;
	}

	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"POST", L"/token", nullptr, WINHTTP_NO_REFERER,
				   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		result.error =
			"WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return result;
	}

	// Content-Type ヘッダーを追加
	WinHttpAddRequestHeaders(
		hRequest, L"Content-Type: application/x-www-form-urlencoded\r\n",
		static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);

	// POST ボディを送信
	const auto bodyLen = static_cast<DWORD>(body.size());
	const bool sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					     const_cast<char *>(body.c_str()), bodyLen,
					     bodyLen, 0);
	if (!sent) {
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
	WinHttpQueryHeaders(hRequest,
			    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
			    WINHTTP_NO_HEADER_INDEX);
	obs_log(LOG_INFO, "[%s] HTTP status: %lu", TAG, statusCode);

	// レスポンスを読み取る（チャンク分割に対応）
	std::string responseBody;
	DWORD bytesAvailable = 0;
	int readLoops = 0;
	while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
		std::string chunk(bytesAvailable, '\0');
		DWORD bytesRead = 0;
		if (!WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead)) {
			obs_log(LOG_WARNING, "[%s] WinHttpReadData failed (err=%lu) at loop %d",
				TAG, GetLastError(), readLoops);
			break;
		}
		responseBody.append(chunk, 0, bytesRead);
		++readLoops;
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	// レスポンス本文をログ（全バイト数 + 先頭 500 文字）
	obs_log(LOG_INFO, "[%s] Response body (%zu bytes, %d read loops): %.*s", TAG,
		responseBody.size(), readLoops, static_cast<int>(responseBody.size()),
		responseBody.c_str());

	// JSON パース
	result.error = jsonStr(responseBody, "error");
	if (!result.error.empty()) {
		const std::string desc = jsonStr(responseBody, "error_description");
		if (!desc.empty())
			result.error += ": " + desc;
		obs_log(LOG_WARNING, "[%s] Token error from Google: %s", TAG, result.error.c_str());
		return result;
	}

	result.accessToken = jsonStr(responseBody, "access_token");
	result.refreshToken = jsonStr(responseBody, "refresh_token");
	result.expiresIn = jsonInt(responseBody, "expires_in");
	if (result.expiresIn <= 0)
		result.expiresIn = 3600;
	result.ok = !result.accessToken.empty();
	if (!result.ok) {
		result.error = "access_token が応答に含まれていません";
		obs_log(LOG_WARNING, "[%s] %s", TAG, result.error.c_str());
	} else {
		obs_log(LOG_INFO, "[%s] Token exchange OK: access_token(len=%zu) refresh_token(len=%zu) expires_in=%lld",
			TAG, result.accessToken.size(), result.refreshToken.size(),
			static_cast<long long>(result.expiresIn));
	}

	return result;
}
#else
static GoogleTokenResult doWinHttpPost(const std::string &)
{
	GoogleTokenResult r;
	r.error = "Windows only";
	return r;
}
#endif

// ============================================================
// GoogleOAuthCallbackServer
// ============================================================

void GoogleOAuthCallbackServer::startAsync(std::function<void(const std::string &)> onCode,
					   int port, int timeoutSec)
{
	std::thread([onCode, port, timeoutSec]() {
#ifdef _WIN32
		// WSAStartup は参照カウント方式なので重複呼び出し可。
		// WsServer / TlsSocket がすでに初期化しているが念のため呼ぶ。
		// WSACleanup は呼ばない — 他のソケットを破壊する可能性があるため。
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);

		SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSock == INVALID_SOCKET) {
			onCode({});
			return;
		}

		int reuse = 1;
		setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
			   reinterpret_cast<const char *>(&reuse), sizeof(reuse));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = htons(static_cast<u_short>(port));

		if (bind(listenSock, reinterpret_cast<SOCKADDR *>(&addr), sizeof(addr)) != 0 ||
		    listen(listenSock, 1) != 0) {
			closesocket(listenSock);
			onCode({});
			return;
		}

		// select() でタイムアウト付き accept（SO_RCVTIMEO は accept に効かない）
		struct timeval tv;
		tv.tv_sec = timeoutSec;
		tv.tv_usec = 0;
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listenSock, &fds);
		obs_log(LOG_INFO, "[%s] CallbackServer: listening on port %d (timeout=%d s)", TAG,
			port, timeoutSec);

		if (select(0, &fds, nullptr, nullptr, &tv) <= 0) {
			obs_log(LOG_WARNING, "[%s] CallbackServer: timeout waiting for browser redirect",
				TAG);
			closesocket(listenSock);
			onCode({});
			return;
		}

		SOCKET client = accept(listenSock, nullptr, nullptr);
		closesocket(listenSock);
		if (client == INVALID_SOCKET) {
			obs_log(LOG_WARNING, "[%s] CallbackServer: accept() failed (err=%d)", TAG,
				WSAGetLastError());
			onCode({});
			return;
		}

		std::string request;
		char buf[4096];
		const int n = recv(client, buf, static_cast<int>(sizeof(buf) - 1), 0);
		if (n > 0) {
			buf[n] = '\0';
			request = buf;
		}

		obs_log(LOG_INFO, "[%s] CallbackServer: received %d bytes", TAG, n);

		const std::string code = extractCode(request);
		if (code.empty()) {
			obs_log(LOG_WARNING,
				"[%s] CallbackServer: no 'code' param in request. First 200 chars: %.200s",
				TAG, request.c_str());
		} else {
			obs_log(LOG_INFO, "[%s] CallbackServer: got authorization code (len=%zu)", TAG,
				code.size());
		}

		const char *htmlBody =
			code.empty()
				? "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
				  "<h2 style='color:red'>エラー</h2>"
				  "<p>認証コードが取得できませんでした。</p>"
				  "</body></html>"
				: "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
				  "<h2 style='color:green'>認証完了</h2>"
				  "<p>このタブを閉じて OBS に戻ってください。</p>"
				  "</body></html>";
		const std::string response =
			std::string("HTTP/1.1 200 OK\r\n"
				    "Content-Type: text/html; charset=utf-8\r\n"
				    "Connection: close\r\n\r\n") +
			htmlBody;
		send(client, response.c_str(), static_cast<int>(response.size()), 0);
		closesocket(client);

		onCode(code);
#else
		(void)port;
		(void)timeoutSec;
		onCode({});
#endif
	}).detach();
}

// ============================================================
// GoogleOAuthTokenClient
// ============================================================

void GoogleOAuthTokenClient::exchangeCodeAsync(const std::string &clientId,
					       const std::string &clientSecret,
					       const std::string &code,
					       const std::string &redirectUri,
					       std::function<void(GoogleTokenResult)> onResult)
{
	obs_log(LOG_INFO,
		"[%s] exchangeCodeAsync: clientId(len=%zu) code(len=%zu) redirectUri=%s", TAG,
		clientId.size(), code.size(), redirectUri.c_str());

	// client_id の先頭がスペースや改行の場合を検出してログに出す
	if (!clientId.empty() && (clientId[0] == '\n' || clientId[0] == '\r' || clientId[0] == ' ')) {
		obs_log(LOG_WARNING,
			"[%s] WARNING: clientId starts with whitespace/newline (0x%02X)! "
			"Please check the Client ID in settings.",
			TAG, static_cast<unsigned char>(clientId[0]));
	}

	std::thread([=]() {
		std::string body = "client_id=" + urlEncode(clientId);
		body += "&client_secret=" + urlEncode(clientSecret);
		body += "&code=" + urlEncode(code);
		body += "&redirect_uri=" + urlEncode(redirectUri);
		body += "&grant_type=authorization_code";
		onResult(doWinHttpPost(body));
	}).detach();
}

void GoogleOAuthTokenClient::refreshAccessTokenAsync(
	const std::string &clientId, const std::string &clientSecret,
	const std::string &refreshToken, std::function<void(GoogleTokenResult)> onResult)
{
	obs_log(LOG_INFO, "[%s] refreshAccessTokenAsync: clientId(len=%zu) refreshToken(len=%zu)",
		TAG, clientId.size(), refreshToken.size());

	std::thread([=]() {
		std::string body = "client_id=" + urlEncode(clientId);
		body += "&client_secret=" + urlEncode(clientSecret);
		body += "&refresh_token=" + urlEncode(refreshToken);
		body += "&grant_type=refresh_token";
		onResult(doWinHttpPost(body));
	}).detach();
}
