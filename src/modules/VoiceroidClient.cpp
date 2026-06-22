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

#include "VoiceroidClient.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>
#include <thread>

#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

void VoiceroidClient::talk(const QString &host, int port, int cid,
                            const QString &text,
                            const QString &username, const QString &password)
{
#ifdef _WIN32
	if (cid <= 0) {
		obs_log(LOG_WARNING,
		        "[VoiceroidClient] cid が未設定（%d）です。読み上げ設定で cid を設定してください。",
		        cid);
		return;
	}

	const std::string hostStr = host.toStdString();

	// BASIC認証: base64(username:password)（純 ASCII なので wstring 変換は安全）
	const std::string credStr =
		(username + ":" + password).toUtf8().toBase64().toStdString();

	// JSONボディ: {"talktext": "..."}（QJsonDocument でエスケープ処理）
	const QJsonObject bodyObj{{"talktext", text}};
	const std::string bodyStr =
		QJsonDocument(bodyObj).toJson(QJsonDocument::Compact).toStdString();

	obs_log(LOG_INFO,
	        "[VoiceroidClient] speak request: host=%s port=%d cid=%d text=\"%s\"",
	        hostStr.c_str(), port, cid, text.toUtf8().constData());

	std::thread([hostStr, port, cid, bodyStr, credStr]() {
		const std::string path = "/PLAY2/" + std::to_string(cid);

		obs_log(LOG_INFO, "[VoiceroidClient] POST http://%s:%d%s",
		        hostStr.c_str(), port, path.c_str());

		const std::wstring whost(hostStr.begin(), hostStr.end());
		const std::wstring wpath(path.begin(), path.end());

		// ヘッダー文字列（base64 は ASCII のみ、ヘッダー名も ASCII）
		const std::string  hdrs =
			"Content-Type: application/json\r\n"
			"Authorization: Basic " + credStr;
		const std::wstring wHeaders(hdrs.begin(), hdrs.end());

		HINTERNET hSession =
			WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
			            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession) {
			obs_log(LOG_WARNING,
			        "[VoiceroidClient] WinHttpOpen failed: err=%lu",
			        GetLastError());
			return;
		}

		WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

		HINTERNET hConnect =
			WinHttpConnect(hSession, whost.c_str(),
			               static_cast<INTERNET_PORT>(port), 0);
		if (!hConnect) {
			obs_log(LOG_WARNING,
			        "[VoiceroidClient] WinHttpConnect failed: err=%lu",
			        GetLastError());
			WinHttpCloseHandle(hSession);
			return;
		}

		HINTERNET hRequest =
			WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
			                   nullptr, WINHTTP_NO_REFERER,
			                   WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!hRequest) {
			obs_log(LOG_WARNING,
			        "[VoiceroidClient] WinHttpOpenRequest failed: err=%lu",
			        GetLastError());
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return;
		}

		const DWORD bodyLen = static_cast<DWORD>(bodyStr.size());
		const bool ok =
			WinHttpSendRequest(hRequest,
			                   wHeaders.c_str(), static_cast<DWORD>(-1L),
			                   const_cast<char *>(bodyStr.c_str()),
			                   bodyLen, bodyLen, 0) != FALSE &&
			WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

		if (ok) {
			obs_log(LOG_INFO, "[VoiceroidClient] speak OK");
		} else {
			obs_log(LOG_WARNING,
			        "[VoiceroidClient] WinHttpSendRequest failed: err=%lu",
			        GetLastError());
		}

		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
	}).detach();
#else
	(void)host;
	(void)port;
	(void)cid;
	(void)text;
	(void)username;
	(void)password;
#endif
}
