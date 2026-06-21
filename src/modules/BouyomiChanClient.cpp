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

#include "BouyomiChanClient.hpp"

#include <QUrl>

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

void BouyomiChanClient::talk(const QString &host, int port, const QString &text,
                              int voice, int volume, int speed, int tone)
{
#ifdef _WIN32
	// テキストをUTF-8 URLエンコード（%XX形式）にする
	const QByteArray encoded  = QUrl::toPercentEncoding(text);
	const std::string encStr  = encoded.toStdString();
	const std::string hostStr = host.toStdString();

	std::thread([hostStr, port, encStr, voice, volume, speed, tone]() {
		// パスは全て ASCII 文字のみ（%XX 形式）なので wstring 変換は安全
		const std::string path = "/Talk?text=" + encStr +
		                         "&voice=" + std::to_string(voice) +
		                         "&volume=" + std::to_string(volume) +
		                         "&speed=" + std::to_string(speed) +
		                         "&tone=" + std::to_string(tone);

		obs_log(LOG_INFO, "[BouyomiChanClient] GET http://%s:%d%s",
		        hostStr.c_str(), port, path.c_str());

		const std::wstring whost(hostStr.begin(), hostStr.end());
		const std::wstring wpath(path.begin(), path.end());

		HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0",
		                                 WINHTTP_ACCESS_TYPE_NO_PROXY,
		                                 WINHTTP_NO_PROXY_NAME,
		                                 WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession) {
			obs_log(LOG_WARNING, "[BouyomiChanClient] WinHttpOpen failed: err=%lu",
			        GetLastError());
			return;
		}

		HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
		                                    static_cast<INTERNET_PORT>(port), 0);
		if (!hConnect) {
			obs_log(LOG_WARNING, "[BouyomiChanClient] WinHttpConnect failed: err=%lu",
			        GetLastError());
			WinHttpCloseHandle(hSession);
			return;
		}

		HINTERNET hRequest =
			WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
			                   nullptr, WINHTTP_NO_REFERER,
			                   WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!hRequest) {
			obs_log(LOG_WARNING, "[BouyomiChanClient] WinHttpOpenRequest failed: err=%lu",
			        GetLastError());
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return;
		}

		// レスポンスは確認のみ（棒読みちゃんが自前で音声再生するため中身は不要）
		if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
			WinHttpReceiveResponse(hRequest, nullptr);
			obs_log(LOG_INFO, "[BouyomiChanClient] speak OK");
		} else {
			obs_log(LOG_WARNING, "[BouyomiChanClient] WinHttpSendRequest failed: err=%lu",
			        GetLastError());
		}

		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
	}).detach();
#else
	(void)host;
	(void)port;
	(void)text;
	(void)voice;
	(void)volume;
	(void)speed;
	(void)tone;
#endif
}
