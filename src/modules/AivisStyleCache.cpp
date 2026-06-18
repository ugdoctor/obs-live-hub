/*
obs-live-hub
Copyright (C) 2026 ugdoctor
*/

#include "AivisStyleCache.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <thread>

#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

#include <obs-module.h>
#include <plugin-support.h>

static QVector<AivisCachedStyle> s_cache;

void AivisStyleCache::set(const QVector<AivisCachedStyle> &styles)
{
	s_cache = styles;
}

const AivisCachedStyle *AivisStyleCache::findByName(const QString &name)
{
	const QString lower = name.toLower();
	for (const auto &s : s_cache) {
		if (s.speakerName.toLower().contains(lower) ||
		    s.styleName.toLower().contains(lower))
			return &s;
	}
	return nullptr;
}

bool AivisStyleCache::isEmpty()
{
	return s_cache.isEmpty();
}

// ── HTTP ヘルパー（Windows 専用） ────────────────────────────────────────
#ifdef _WIN32
namespace {

struct UrlParts {
	std::wstring  host;
	INTERNET_PORT port    = 80;
	bool          isHttps = false;
};

static UrlParts parseUrl(const QString &baseUrl)
{
	UrlParts parts;
	QString  url = baseUrl.trimmed();
	if (url.startsWith("https://", Qt::CaseInsensitive)) {
		parts.isHttps = true;
		url           = url.mid(8);
		parts.port    = 443;
	} else if (url.startsWith("http://", Qt::CaseInsensitive)) {
		url        = url.mid(7);
		parts.port = 80;
	}
	const int slash = url.indexOf('/');
	if (slash >= 0)
		url = url.left(slash);
	const int colon = url.indexOf(':');
	if (colon >= 0) {
		parts.host = url.left(colon).toStdWString();
		parts.port = static_cast<INTERNET_PORT>(url.mid(colon + 1).toInt());
	} else {
		parts.host = url.toStdWString();
	}
	return parts;
}

// タイムアウトを 3 秒に設定した GET リクエスト
static bool httpGetJson(const QString &baseUrl, const wchar_t *path, QByteArray &outBody)
{
	const UrlParts p = parseUrl(baseUrl);
	if (p.host.empty())
		return false;

	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
	                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	// 名前解決・接続・送受信すべて 3 秒でタイムアウト（デフォルトは数分）
	WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);

	HINTERNET hConnect = WinHttpConnect(hSession, p.host.c_str(), p.port, 0);
	HINTERNET hRequest =
		hConnect
			? WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
		                             WINHTTP_DEFAULT_ACCEPT_TYPES,
		                             p.isHttps ? WINHTTP_FLAG_SECURE : 0)
			: nullptr;

	const bool ok =
		hRequest &&
		WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
	                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	if (ok) {
		char  buf[4096];
		DWORD n = 0;
		while (WinHttpReadData(hRequest, buf, sizeof(buf), &n) && n > 0)
			outBody.append(buf, static_cast<int>(n));
	}

	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok;
}

// JSON をパースして AivisCachedStyle のリストを返す（失敗時は空）
static QVector<AivisCachedStyle> parseSpeakers(const QByteArray &body)
{
	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(body, &err);
	if (!doc.isArray()) {
		obs_log(LOG_WARNING, "[AivisStyleCache] /speakers parse error: %s",
			err.errorString().toUtf8().constData());
		return {};
	}

	QVector<AivisCachedStyle> entries;
	for (const auto &v : doc.array()) {
		const auto    obj    = v.toObject();
		const QString spName = obj["name"].toString();
		for (const auto &sv : obj["styles"].toArray()) {
			const auto sobj = sv.toObject();
			AivisCachedStyle e;
			e.speakerName = spName;
			e.styleName   = sobj["name"].toString();
			e.styleId     = sobj["id"].toInteger();
			entries.append(e);
		}
	}
	return entries;
}

// メインスレッドでキャッシュを更新してログを出す
static void applyToCache(const QVector<AivisCachedStyle> &entries)
{
	const int count = entries.size();
	QMetaObject::invokeMethod(
		qApp,
		[entries, count]() {
			AivisStyleCache::set(entries);
			obs_log(LOG_INFO, "[AivisEngine] speakers loaded: %d 件", count);
		},
		Qt::QueuedConnection);
}

} // namespace
#endif // _WIN32

// ── 公開 API ─────────────────────────────────────────────────────────────

// 1 回だけ /speakers を取得してキャッシュ更新（バックグラウンド）
void AivisStyleCache::fetchAndCacheAsync(const QString &aivisUrl)
{
#ifdef _WIN32
	const QString url =
		aivisUrl.trimmed().isEmpty() ? QStringLiteral("http://localhost:10101") : aivisUrl;

	std::thread([url]() {
		QByteArray body;
		if (!httpGetJson(url, L"/speakers", body)) {
			obs_log(LOG_INFO,
				"[AivisEngine] speakers fetch failed (engine not running?)");
			return;
		}
		const auto entries = parseSpeakers(body);
		if (!entries.isEmpty())
			applyToCache(entries);
	}).detach();
#else
	Q_UNUSED(aivisUrl)
#endif
}

// /speakers 取得+キャッシュ更新、先頭話者をメインスレッドの cb に通知
void AivisStyleCache::fetchAndCacheAsyncWithResult(
	const QString &url,
	std::function<void(bool, int64_t, const QString &, const QString &)> cb)
{
#ifdef _WIN32
	const QString resolvedUrl =
		url.trimmed().isEmpty() ? QStringLiteral("http://localhost:10101") : url;

	std::thread([resolvedUrl, cb]() {
		QByteArray body;
		if (!httpGetJson(resolvedUrl, L"/speakers", body)) {
			QMetaObject::invokeMethod(qApp, [cb]() { cb(false, 0, {}, {}); },
			                          Qt::QueuedConnection);
			return;
		}
		const auto entries = parseSpeakers(body);
		if (entries.isEmpty()) {
			QMetaObject::invokeMethod(qApp, [cb]() { cb(false, 0, {}, {}); },
			                          Qt::QueuedConnection);
			return;
		}
		applyToCache(entries);
		const int64_t firstId = entries[0].styleId;
		const QString firstSp = entries[0].speakerName;
		const QString firstSt = entries[0].styleName;
		QMetaObject::invokeMethod(qApp, [cb, firstId, firstSp, firstSt]() {
			cb(true, firstId, firstSp, firstSt);
		}, Qt::QueuedConnection);
	}).detach();
#else
	Q_UNUSED(url)
	Q_UNUSED(cb)
#endif
}

// エンジン起動後用：1 秒ごとにリトライ、最大 maxSeconds 秒
void AivisStyleCache::fetchAndCacheWithRetry(const QString &aivisUrl, int maxSeconds)
{
#ifdef _WIN32
	const QString url =
		aivisUrl.trimmed().isEmpty() ? QStringLiteral("http://localhost:10101") : aivisUrl;

	std::thread([url, maxSeconds]() {
		for (int i = 0; i <= maxSeconds; ++i) {
			if (i > 0)
				Sleep(1000);

			QByteArray body;
			if (!httpGetJson(url, L"/speakers", body))
				continue; // 失敗 → リトライ

			const auto entries = parseSpeakers(body);
			if (!entries.isEmpty()) {
				applyToCache(entries);
				return; // 成功
			}
		}
		obs_log(LOG_WARNING, "[AivisEngine] speakers fetch failed after %d attempts",
			maxSeconds + 1);
	}).detach();
#else
	Q_UNUSED(aivisUrl)
	Q_UNUSED(maxSeconds)
#endif
}
