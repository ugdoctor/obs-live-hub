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

#include "EngineManager.hpp"
#include "AivisStyleCache.hpp"
#include "../core/PluginConfig.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QVector>

#include <obs-module.h>
#include <plugin-support.h>

// ── static storage ─────────────────────────────────────────────────────────

static std::map<std::string, QProcess *>                  s_processes;
static std::map<std::string, EngineManager::EngineStatus> s_statuses;

// ── internal helpers ───────────────────────────────────────────────────────

namespace {

static void applyStatus(const std::string &name, EngineManager::EngineStatus st)
{
	s_statuses[name] = st;

	switch (st.state) {
	case EngineManager::EngineState::Starting:
		obs_log(LOG_INFO, "[EngineManager] %s: Starting", name.c_str());
		break;
	case EngineManager::EngineState::Connected:
		if (st.speakerCount > 0)
			obs_log(LOG_INFO, "[EngineManager] %s: Connected (%d speakers)",
			        name.c_str(), st.speakerCount);
		else
			obs_log(LOG_INFO, "[EngineManager] %s: Connected", name.c_str());
		break;
	case EngineManager::EngineState::Error:
		obs_log(LOG_WARNING, "[EngineManager] %s: Error - %s",
		        name.c_str(), st.errorMsg.c_str());
		break;
	default:
		break;
	}
}

#ifdef _WIN32

struct UrlParts {
	std::wstring  host;
	INTERNET_PORT port    = 80;
	bool          isHttps = false;
};

static UrlParts parseUrl(const std::string &baseUrl)
{
	UrlParts    parts;
	std::string url = baseUrl;

	if (url.size() > 8 && url.substr(0, 8) == "https://") {
		parts.isHttps = true;
		url           = url.substr(8);
		parts.port    = 443;
	} else if (url.size() > 7 && url.substr(0, 7) == "http://") {
		url        = url.substr(7);
		parts.port = 80;
	}

	const auto slash = url.find('/');
	if (slash != std::string::npos)
		url = url.substr(0, slash);

	const auto colon = url.rfind(':');
	if (colon != std::string::npos) {
		parts.host = std::wstring(url.begin(), url.begin() + colon);
		try {
			parts.port = static_cast<INTERNET_PORT>(std::stoi(url.substr(colon + 1)));
		} catch (...) {}
	} else {
		parts.host = std::wstring(url.begin(), url.end());
	}
	return parts;
}

struct FetchResult {
	bool                      ok           = false;
	int                       speakerCount = 0;
	QVector<AivisCachedStyle> styles;
};

static FetchResult fetchSpeakers(const std::string &baseUrl)
{
	FetchResult   res;
	const UrlParts p = parseUrl(baseUrl);
	if (p.host.empty())
		return res;

	HINTERNET hSession =
		WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
		            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return res;

	WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);

	HINTERNET hConnect = WinHttpConnect(hSession, p.host.c_str(), p.port, 0);
	HINTERNET hRequest =
		hConnect ? WinHttpOpenRequest(hConnect, L"GET", L"/speakers", nullptr,
		                              WINHTTP_NO_REFERER,
		                              WINHTTP_DEFAULT_ACCEPT_TYPES,
		                              p.isHttps ? WINHTTP_FLAG_SECURE : 0)
		         : nullptr;

	QByteArray body;
	const bool ok =
		hRequest &&
		WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	if (ok) {
		char  buf[4096];
		DWORD n = 0;
		while (WinHttpReadData(hRequest, buf, sizeof(buf), &n) && n > 0)
			body.append(buf, static_cast<int>(n));
	}

	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (!ok)
		return res;

	const auto doc = QJsonDocument::fromJson(body);
	if (!doc.isArray())
		return res;

	for (const auto &v : doc.array()) {
		const auto    spObj  = v.toObject();
		const QString spName = spObj["name"].toString();
		for (const auto &sv : spObj["styles"].toArray()) {
			const auto sobj = sv.toObject();
			AivisCachedStyle e;
			e.speakerName = spName;
			e.styleName   = sobj["name"].toString();
			e.styleId     = sobj["id"].toInteger();
			res.styles.append(e);
		}
	}

	res.ok           = true;
	res.speakerCount = res.styles.size();
	return res;
}

#endif // _WIN32

// バックグラウンドスレッドで /speakers を取得してステータスを更新する
// isActiveEngine = true のとき AivisStyleCache も更新する
static void launchSpeakerCheck(const std::string &engineName, const std::string &url,
                                bool isActiveEngine, bool withRetry, int maxSeconds = 30)
{
#ifdef _WIN32
	std::thread([engineName, url, isActiveEngine, withRetry, maxSeconds]() {
		const int tries = withRetry ? maxSeconds : 1;

		for (int i = 0; i < tries; ++i) {
			if (i > 0)
				Sleep(1000);

			const auto r = fetchSpeakers(url);
			if (r.ok) {
				const auto styles       = r.styles;
				const int  speakerCount = r.speakerCount;
				QMetaObject::invokeMethod(qApp,
				                          [engineName, isActiveEngine, styles,
				                           speakerCount]() {
					                          if (isActiveEngine && !styles.isEmpty())
						                          AivisStyleCache::set(styles);

					                          EngineManager::EngineStatus st;
					                          st.state        = EngineManager::EngineState::Connected;
					                          st.speakerCount = speakerCount;
					                          applyStatus(engineName, st);
				                          },
				                          Qt::QueuedConnection);
				return;
			}
		}

		QMetaObject::invokeMethod(qApp, [engineName]() {
			EngineManager::EngineStatus st;
			st.state    = EngineManager::EngineState::Error;
			st.errorMsg = "エンジンに接続できませんでした";
			applyStatus(engineName, st);
		}, Qt::QueuedConnection);
	}).detach();
#else
	Q_UNUSED(engineName)
	Q_UNUSED(url)
	Q_UNUSED(isActiveEngine)
	Q_UNUSED(withRetry)
	Q_UNUSED(maxSeconds)
#endif
}

// 棒読みちゃん HTTP サーバーへの疎通確認（GET / を送り、レスポンスが来ればOK）
#ifdef _WIN32
static bool pingBouyomi(const std::string &host, int port)
{
	const std::wstring whost(host.begin(), host.end());

	HINTERNET hSession =
		WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
		            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);

	HINTERNET hConnect =
		WinHttpConnect(hSession, whost.c_str(),
		               static_cast<INTERNET_PORT>(port), 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"GET", L"/", nullptr,
		                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const bool ok =
		WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok;
}
#endif

// AssistantSeika HTTP サーバーへの疎通確認（GET /VERSION を送り、レスポンスが来ればOK）
#ifdef _WIN32
static bool pingVoiceroid(const std::string &host, int port)
{
	const std::wstring whost(host.begin(), host.end());

	HINTERNET hSession =
		WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
		            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);

	HINTERNET hConnect =
		WinHttpConnect(hSession, whost.c_str(),
		               static_cast<INTERNET_PORT>(port), 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"GET", L"/VERSION", nullptr,
		                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const bool ok =
		WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok;
}
#endif

// AssistantSeika 接続確認をバックグラウンドで実行してステータスを更新する
static void launchVoiceroidCheck(const std::string &host, int port,
                                  bool withRetry, int maxSeconds = 30)
{
#ifdef _WIN32
	std::thread([host, port, withRetry, maxSeconds]() {
		const int tries = withRetry ? maxSeconds : 1;

		for (int i = 0; i < tries; ++i) {
			if (i > 0)
				Sleep(1000);

			if (pingVoiceroid(host, port)) {
				QMetaObject::invokeMethod(qApp, []() {
					EngineManager::EngineStatus st;
					st.state = EngineManager::EngineState::Connected;
					applyStatus("voiceroid", st);
				}, Qt::QueuedConnection);
				return;
			}
		}

		QMetaObject::invokeMethod(qApp, []() {
			EngineManager::EngineStatus st;
			st.state    = EngineManager::EngineState::Error;
			st.errorMsg = "AssistantSeika に接続できませんでした";
			applyStatus("voiceroid", st);
		}, Qt::QueuedConnection);
	}).detach();
#else
	Q_UNUSED(host)
	Q_UNUSED(port)
	Q_UNUSED(withRetry)
	Q_UNUSED(maxSeconds)
#endif
}

// 棒読みちゃん接続確認をバックグラウンドで実行してステータスを更新する
static void launchBouyomiCheck(const std::string &host, int port,
                                bool withRetry, int maxSeconds = 30)
{
#ifdef _WIN32
	std::thread([host, port, withRetry, maxSeconds]() {
		const int tries = withRetry ? maxSeconds : 1;

		for (int i = 0; i < tries; ++i) {
			if (i > 0)
				Sleep(1000);

			if (pingBouyomi(host, port)) {
				QMetaObject::invokeMethod(qApp, []() {
					EngineManager::EngineStatus st;
					st.state = EngineManager::EngineState::Connected;
					applyStatus("bouyomi", st);
				}, Qt::QueuedConnection);
				return;
			}
		}

		QMetaObject::invokeMethod(qApp, []() {
			EngineManager::EngineStatus st;
			st.state    = EngineManager::EngineState::Error;
			st.errorMsg = "棒読みちゃんに接続できませんでした";
			applyStatus("bouyomi", st);
		}, Qt::QueuedConnection);
	}).detach();
#else
	Q_UNUSED(host)
	Q_UNUSED(port)
	Q_UNUSED(withRetry)
	Q_UNUSED(maxSeconds)
#endif
}

} // namespace

// ── public API ─────────────────────────────────────────────────────────────

void EngineManager::startAll()
{
	const auto &cfg = PluginConfig::instance();

	struct VoicevoxEntry {
		std::string name;
		bool        enabled;
		std::string url;
		std::string exePath;
		bool        autoStart;
	};

	const std::vector<VoicevoxEntry> engines = {
		{"aivisspeech", cfg.aivisspeechEnabled, cfg.aivisUrl,    cfg.aivisEnginePath,    cfg.aivisAutoStart},
		{"sharevox",    cfg.sharevoxEnabled,    cfg.sharevoxUrl, cfg.sharevoxEnginePath, cfg.sharevoxAutoStart},
		{"lmroid",      cfg.lmroidEnabled,      cfg.lmroidUrl,   cfg.lmroidEnginePath,   cfg.lmroidAutoStart},
		{"itvoice",     cfg.itvoiceEnabled,     cfg.itvoiceUrl,  cfg.itvoiceEnginePath,  cfg.itvoiceAutoStart},
	};

	const std::string activeEngine = cfg.ttsEngine;
	bool anyStarted = false;

	for (const auto &e : engines) {
		if (!e.enabled)
			continue;

		anyStarted = true;

		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus(e.name, st);
		}

		const bool doAutoStart = e.autoStart && !e.exePath.empty();
		if (doAutoStart) {
			auto *proc = new QProcess();
			proc->start(QString::fromStdString(e.exePath), {"--allow_origin", "*"});
			s_processes[e.name] = proc;
		}

		launchSpeakerCheck(e.name, e.url,
		                   /*isActiveEngine=*/(e.name == activeEngine),
		                   /*withRetry=*/doAutoStart);
	}

	// 棒読みちゃん: 自動起動対応・HTTP ping で接続確認
	if (cfg.bouyomiEnabled) {
		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus("bouyomi", st);
		}
		anyStarted = true;

		const bool alreadyLaunched =
			s_processes.count("bouyomi") > 0 &&
			s_processes.at("bouyomi") != nullptr &&
			s_processes.at("bouyomi")->state() != QProcess::NotRunning;

		bool doAutoStart = false;
		if (cfg.bouyomiAutoStart) {
			if (cfg.bouyomiExePath.empty()) {
				obs_log(LOG_WARNING,
				        "[EngineManager] bouyomi auto-start: path not set, skipping");
			} else if (alreadyLaunched) {
				obs_log(LOG_INFO,
				        "[EngineManager] bouyomi auto-start: already running, skipping launch");
			} else {
				obs_log(LOG_INFO, "[EngineManager] bouyomi auto-start: launching %s",
				        cfg.bouyomiExePath.c_str());
				auto *proc = new QProcess();
				proc->start(QString::fromStdString(cfg.bouyomiExePath), {});
				s_processes["bouyomi"] = proc;
				doAutoStart = true;
			}
		}

		launchBouyomiCheck(cfg.bouyomiHost, cfg.bouyomiPort, doAutoStart);
	}

	// VOICEROID（AssistantSeika）: HTTP ping で接続確認
	if (cfg.voiceroidEnabled) {
		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus("voiceroid", st);
		}
		anyStarted = true;
		launchVoiceroidCheck(cfg.voiceroidHost, cfg.voiceroidPort, /*withRetry=*/false);
	}

	if (!anyStarted)
		obs_log(LOG_INFO, "[EngineManager] No engines enabled — skipping startup");
}

void EngineManager::stopAll()
{
	for (auto &[name, proc] : s_processes) {
		if (!proc)
			continue;
		proc->terminate();
		if (!proc->waitForFinished(3000))
			proc->kill();
		delete proc;
	}
	s_processes.clear();
	s_statuses.clear();
}

std::map<std::string, EngineManager::EngineStatus> EngineManager::getAllStatuses()
{
	return s_statuses;
}

void EngineManager::refreshAll()
{
	const auto &cfg = PluginConfig::instance();

	struct Entry {
		std::string name;
		bool        enabled;
		std::string url;
	};
	const std::vector<Entry> engines = {
		{"aivisspeech", cfg.aivisspeechEnabled, cfg.aivisUrl},
		{"sharevox",    cfg.sharevoxEnabled,    cfg.sharevoxUrl},
		{"lmroid",      cfg.lmroidEnabled,      cfg.lmroidUrl},
		{"itvoice",     cfg.itvoiceEnabled,     cfg.itvoiceUrl},
	};

	const std::string activeEngine = cfg.ttsEngine;

	for (const auto &e : engines) {
		if (!e.enabled)
			continue;

		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus(e.name, st);
		}
		launchSpeakerCheck(e.name, e.url, (e.name == activeEngine), /*withRetry=*/false);
	}

	if (cfg.bouyomiEnabled) {
		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus("bouyomi", st);
		}
		launchBouyomiCheck(cfg.bouyomiHost, cfg.bouyomiPort, /*withRetry=*/false);
	}

	if (cfg.voiceroidEnabled) {
		{
			EngineStatus st;
			st.state = EngineState::Starting;
			applyStatus("voiceroid", st);
		}
		launchVoiceroidCheck(cfg.voiceroidHost, cfg.voiceroidPort, /*withRetry=*/false);
	}
}
