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

#include "XClient.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <ctime>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

// ─── 内部ヘルパー ─────────────────────────────────────────────────────────────

static std::string percentEncode(const std::string &s)
{
	std::string out;
	out.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", c);
			out += buf;
		}
	}
	return out;
}

static std::string base64Encode(const std::vector<uint8_t> &data)
{
	DWORD needed = 0;
	CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
	                     CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
	                     nullptr, &needed);
	std::string result(needed, '\0');
	CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
	                     CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
	                     &result[0], &needed);
	while (!result.empty() && result.back() == '\0')
		result.pop_back();
	return result;
}

static std::vector<uint8_t> hmacSha1(const std::string &key, const std::string &data)
{
	std::vector<uint8_t> digest(20, 0);

	BCRYPT_ALG_HANDLE hAlg = nullptr;
	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr,
	                                BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
		return digest;

	BCRYPT_HASH_HANDLE hHash = nullptr;
	const bool created = BCryptCreateHash(
		hAlg, &hHash, nullptr, 0,
		reinterpret_cast<PUCHAR>(const_cast<char *>(key.data())),
		static_cast<ULONG>(key.size()), 0) == 0;
	if (!created) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return digest;
	}

	BCryptHashData(hHash,
	               reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())),
	               static_cast<ULONG>(data.size()), 0);
	BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);

	BCryptDestroyHash(hHash);
	BCryptCloseAlgorithmProvider(hAlg, 0);
	return digest;
}

static std::string generateNonce()
{
	uint8_t bytes[16] = {};
	BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	std::string result;
	result.reserve(32);
	for (uint8_t b : bytes) {
		char buf[3];
		snprintf(buf, sizeof(buf), "%02x", b);
		result += buf;
	}
	return result;
}

static std::string escapeJson(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + 16);
	for (unsigned char c : s) {
		if      (c == '"')  out += "\\\"";
		else if (c == '\\') out += "\\\\";
		else if (c == '\n') out += "\\n";
		else if (c == '\r') out += "\\r";
		else if (c >= 0x20) out += static_cast<char>(c);
	}
	return out;
}

// JSON から "key":"value" 形式の文字列値を抽出する簡易パーサー
static std::string jsonExtractStr(const std::string &json, const std::string &key)
{
	const std::string search = "\"" + key + "\":\"";
	const size_t pos = json.find(search);
	if (pos == std::string::npos)
		return {};
	const size_t start = pos + search.size();
	const size_t end   = json.find('"', start);
	if (end == std::string::npos)
		return {};
	return json.substr(start, end - start);
}

// OAuth 1.0a 署名を生成する
// method: "GET" / "POST"
// baseUrl: クエリ文字列を含まない URL
// extraParams: クエリパラメータ（署名の対象に含める場合）
static std::string buildOAuthSignature(
	const std::string &method,
	const std::string &baseUrl,
	const std::string &apiKey,
	const std::string &apiSecret,
	const std::string &accessToken,
	const std::string &accessTokenSecret,
	const std::string &nonce,
	const std::string &timestamp,
	const std::map<std::string, std::string> &extraParams = {})
{
	std::map<std::string, std::string> params = {
		{"oauth_consumer_key",     apiKey},
		{"oauth_nonce",            nonce},
		{"oauth_signature_method", "HMAC-SHA1"},
		{"oauth_timestamp",        timestamp},
		{"oauth_token",            accessToken},
		{"oauth_version",          "1.0"},
	};
	for (const auto &kv : extraParams)
		params[kv.first] = kv.second;

	std::string paramStr;
	for (const auto &kv : params) {
		if (!paramStr.empty()) paramStr += '&';
		paramStr += percentEncode(kv.first) + '=' + percentEncode(kv.second);
	}

	const std::string sigBase =
		method + '&' + percentEncode(baseUrl) + '&' + percentEncode(paramStr);
	const std::string sigKey =
		percentEncode(apiSecret) + '&' + percentEncode(accessTokenSecret);

	return base64Encode(hmacSha1(sigKey, sigBase));
}

// Authorization ヘッダー値を構築する（署名のみ percent-encode）
static std::string buildAuthHeaderVal(
	const std::string &apiKey,
	const std::string &accessToken,
	const std::string &nonce,
	const std::string &timestamp,
	const std::string &signature)
{
	return std::string("OAuth ") +
	       "oauth_consumer_key=\"" + apiKey + "\"," +
	       "oauth_nonce=\"" + nonce + "\"," +
	       "oauth_signature=\"" + percentEncode(signature) + "\"," +
	       "oauth_signature_method=\"HMAC-SHA1\"," +
	       "oauth_timestamp=\"" + timestamp + "\"," +
	       "oauth_token=\"" + accessToken + "\"," +
	       "oauth_version=\"1.0\"";
}

// レスポンスボディを全て読み込む
static std::string readBody(HINTERNET hRequest)
{
	std::string body;
	DWORD avail = 0;
	while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
		std::vector<char> buf(avail + 1, '\0');
		DWORD read = 0;
		if (WinHttpReadData(hRequest, buf.data(), avail, &read))
			body.append(buf.data(), read);
	}
	return body;
}

// HTTP ステータスコードを取得する
static int getStatusCode(HINTERNET hRequest)
{
	DWORD code = 0;
	DWORD size = sizeof(code);
	WinHttpQueryHeaders(hRequest,
	                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                    WINHTTP_HEADER_NAME_BY_INDEX,
	                    &code, &size, WINHTTP_NO_HEADER_INDEX);
	return static_cast<int>(code);
}

// WinHTTP セッション／接続ハンドルを開く（HTTPS 専用）
struct WinHttpHandles {
	HINTERNET hSession = nullptr;
	HINTERNET hConnect = nullptr;
	bool ok() const { return hSession && hConnect; }
};

static WinHttpHandles openHttps(const wchar_t *host)
{
	WinHttpHandles h;
	h.hSession = WinHttpOpen(L"obs-live-hub/1.0",
	                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                         WINHTTP_NO_PROXY_NAME,
	                         WINHTTP_NO_PROXY_BYPASS, 0);
	if (!h.hSession)
		return h;
	WinHttpSetTimeouts(h.hSession, 10000, 10000, 10000, 10000);
	h.hConnect = WinHttpConnect(h.hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!h.hConnect) {
		WinHttpCloseHandle(h.hSession);
		h.hSession = nullptr;
	}
	return h;
}

static void closeHttps(WinHttpHandles &h)
{
	if (h.hConnect) { WinHttpCloseHandle(h.hConnect); h.hConnect = nullptr; }
	if (h.hSession) { WinHttpCloseHandle(h.hSession); h.hSession = nullptr; }
}

// ─── 公開 API ─────────────────────────────────────────────────────────────────

XClient::PostResult XClient::postTweet(
	const std::string &apiKey,
	const std::string &apiSecret,
	const std::string &accessToken,
	const std::string &accessTokenSecret,
	const std::string &text)
{
	PostResult result;

	const std::string nonce     = generateNonce();
	const std::string timestamp = std::to_string(static_cast<long long>(std::time(nullptr)));
	const std::string url       = "https://api.twitter.com/2/tweets";

	const std::string signature = buildOAuthSignature(
		"POST", url, apiKey, apiSecret, accessToken, accessTokenSecret, nonce, timestamp);

	const std::string allHeaders =
		"Authorization: " +
		buildAuthHeaderVal(apiKey, accessToken, nonce, timestamp, signature) +
		"\r\nContent-Type: application/json";

	const std::string body = "{\"text\":\"" + escapeJson(text) + "\"}";

	obs_log(LOG_INFO, "[XClient] POST /2/tweets text=\"%s\"", text.c_str());

	auto h = openHttps(L"api.twitter.com");
	if (!h.ok()) {
		obs_log(LOG_WARNING, "[XClient] WinHttpConnect failed: err=%lu", GetLastError());
		return result;
	}

	const std::wstring wHeaders(allHeaders.begin(), allHeaders.end());
	HINTERNET hRequest = WinHttpOpenRequest(
		h.hConnect, L"POST", L"/2/tweets", nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		obs_log(LOG_WARNING, "[XClient] WinHttpOpenRequest failed: err=%lu", GetLastError());
		closeHttps(h);
		return result;
	}

	const DWORD bodyLen = static_cast<DWORD>(body.size());
	const bool sent =
		WinHttpSendRequest(hRequest, wHeaders.c_str(), static_cast<DWORD>(-1L),
		                   const_cast<char *>(body.c_str()),
		                   bodyLen, bodyLen, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	if (!sent) {
		obs_log(LOG_WARNING, "[XClient] POST /2/tweets send failed: err=%lu", GetLastError());
		WinHttpCloseHandle(hRequest);
		closeHttps(h);
		return result;
	}

	result.httpStatus   = getStatusCode(hRequest);
	result.responseBody = readBody(hRequest);
	result.ok           = (result.httpStatus >= 200 && result.httpStatus < 300);

	if (result.ok) {
		result.tweetId = jsonExtractStr(result.responseBody, "id");
		obs_log(LOG_INFO, "[XClient] POST OK: HTTP %d id=%s",
		        result.httpStatus, result.tweetId.c_str());
	} else {
		obs_log(LOG_WARNING, "[XClient] POST FAILED: HTTP %d body=%s",
		        result.httpStatus, result.responseBody.c_str());
	}

	WinHttpCloseHandle(hRequest);
	closeHttps(h);
	return result;
}

XClient::VerifyResult XClient::verifyCredentials(
	const std::string &apiKey,
	const std::string &apiSecret,
	const std::string &accessToken,
	const std::string &accessTokenSecret)
{
	VerifyResult result;

	const std::string nonce     = generateNonce();
	const std::string timestamp = std::to_string(static_cast<long long>(std::time(nullptr)));
	const std::string url       = "https://api.twitter.com/2/users/me";

	const std::string signature = buildOAuthSignature(
		"GET", url, apiKey, apiSecret, accessToken, accessTokenSecret, nonce, timestamp);

	const std::string allHeaders =
		"Authorization: " +
		buildAuthHeaderVal(apiKey, accessToken, nonce, timestamp, signature);

	obs_log(LOG_INFO, "[XClient] GET /2/users/me (credential verify)");

	auto h = openHttps(L"api.twitter.com");
	if (!h.ok()) {
		obs_log(LOG_WARNING, "[XClient] verifyCredentials: WinHttpConnect failed: err=%lu",
		        GetLastError());
		result.errorDesc = "WinHTTP 接続失敗";
		return result;
	}

	const std::wstring wHeaders(allHeaders.begin(), allHeaders.end());
	HINTERNET hRequest = WinHttpOpenRequest(
		h.hConnect, L"GET", L"/2/users/me", nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		obs_log(LOG_WARNING, "[XClient] verifyCredentials: WinHttpOpenRequest failed: err=%lu",
		        GetLastError());
		result.errorDesc = "WinHTTP リクエスト作成失敗";
		closeHttps(h);
		return result;
	}

	const bool sent =
		WinHttpSendRequest(hRequest, wHeaders.c_str(), static_cast<DWORD>(-1L),
		                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	if (!sent) {
		obs_log(LOG_WARNING, "[XClient] verifyCredentials: request failed: err=%lu",
		        GetLastError());
		result.errorDesc = "リクエスト送信失敗（ネットワークエラー）";
		WinHttpCloseHandle(hRequest);
		closeHttps(h);
		return result;
	}

	result.httpStatus        = getStatusCode(hRequest);
	const std::string body   = readBody(hRequest);
	result.ok                = (result.httpStatus == 200);

	if (result.ok) {
		result.username    = jsonExtractStr(body, "username");
		result.displayName = jsonExtractStr(body, "name");
		obs_log(LOG_INFO, "[XClient] verifyCredentials OK: HTTP 200 username=%s",
		        result.username.c_str());
	} else {
		// title フィールド（X API エラー）または先頭の説明部分のみ取り出す
		const std::string title  = jsonExtractStr(body, "title");
		const std::string detail = jsonExtractStr(body, "detail");
		result.errorDesc = title.empty() ? "HTTP " + std::to_string(result.httpStatus)
		                                 : title + (detail.empty() ? "" : ": " + detail);
		obs_log(LOG_WARNING, "[XClient] verifyCredentials FAILED: HTTP %d desc=%s",
		        result.httpStatus, result.errorDesc.c_str());
	}

	WinHttpCloseHandle(hRequest);
	closeHttps(h);
	return result;
}

#else // non-Windows stubs
XClient::PostResult XClient::postTweet(
	const std::string &, const std::string &,
	const std::string &, const std::string &, const std::string &)
{
	return {};
}

XClient::VerifyResult XClient::verifyCredentials(
	const std::string &, const std::string &,
	const std::string &, const std::string &)
{
	return {};
}
#endif
