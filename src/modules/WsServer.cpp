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

#ifdef _WIN32

// rand_s()（CryptoAPIのRtlGenRandomベース、CSPRNG相当）を使うために必要。
// <stdlib.h>系ヘッダを他のインクルードより先にrand_s宣言込みで読み込ませるため、
// ファイル冒頭・他のインクルードより前に置く。
#define _CRT_RAND_S
#include <cstdlib>

#include "WsServer.hpp"

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <obs-module.h>
#include <plugin-support.h>

static const char *WSTAG = "WsServer";

// WAN公開対応: Controllerシークレットトークン（WsServer::controllerSecretToken_）用の
// 安全な乱数英数字文字列を生成する。rand_s()はWindows CSPRNG（RtlGenRandom）を使うため
// std::rand()より推測されにくい。極めて稀にrand_s()自体が失敗する環境（対応するCSPが
// 無い等の異常系）への保険としてのみstd::random_deviceへフォールバックする。
static std::string generateSecureRandomToken(size_t len)
{
	static const char kAlphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::string out;
	out.reserve(len);
	std::random_device fallbackRd;
	for (size_t i = 0; i < len; ++i) {
		unsigned int r = 0;
		if (rand_s(&r) != 0)
			r = fallbackRd();
		out += kAlphabet[r % (sizeof(kAlphabet) - 1)];
	}
	return out;
}

// ─────────────────────────────────────────
// 簡易HTTP画像配信（YouTubeカスタムエモート辞書用）ヘルパー
// ─────────────────────────────────────────

// YouTubeエモート画像の自動格納先フォルダ。
// YouTubeEmoteSettingsDialog::imagesDir() と同一パスを指す（独立して算出する設計。
// TtsDictionaryDialog::csvPath() 等、本プロジェクトの既存の各所と同様に
// %APPDATA% 相対パスを都度組み立てる方式に合わせている）。
static std::wstring emotesImagesDirW()
{
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return std::wstring(appdata) + L"\\obs-studio\\plugins\\obs-live-hub\\youtube_emotes";
}

static std::wstring utf8ToWide(const std::string &s)
{
	if (s.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
	                                   nullptr, 0);
	if (n <= 0)
		return {};
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

static std::string urlDecode(const std::string &s)
{
	auto hexVal = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			const int hi = hexVal(s[i + 1]);
			const int lo = hexVal(s[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out += static_cast<char>((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out += s[i];
	}
	return out;
}

// ディレクトリトラバーサル対策: ".." や区切り文字を含むファイル名は拒否する
// （自動格納先フォルダはフラット構造のみを想定）。
static bool isSafeFileName(const std::string &name)
{
	if (name.empty()) return false;
	if (name.find("..") != std::string::npos) return false;
	if (name.find('/') != std::string::npos) return false;
	if (name.find('\\') != std::string::npos) return false;
	return true;
}

static std::string contentTypeForFile(const std::string &fileName)
{
	const size_t dot = fileName.rfind('.');
	std::string ext = (dot != std::string::npos) ? fileName.substr(dot + 1) : "";
	std::transform(ext.begin(), ext.end(), ext.begin(),
	                [](unsigned char c) { return static_cast<char>(::tolower(c)); });
	if (ext == "png")                return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif")                return "image/gif";
	if (ext == "webp")               return "image/webp";
	if (ext == "bmp")                return "image/bmp";
	return "application/octet-stream";
}

// send()は一度の呼び出しで全バイトを送信する保証がない（部分送信=short write）。
// 特にVRMバイナリ（/vrm/model、数MB）やvrm_stage.html本体（数十〜百KB超）、WebRTCの
// SDP（vrm_call_signal、数百〜数KB）等サイズの大きいペイロードで顕在化しやすく、これを
// 考慮せず戻り値をエラー判定にしか使わないと、クライアント側は「Content-Lengthより短い
// ボディ」（fetch()がERR_CONTENT_LENGTH_MISMATCH等で失敗）や「途中で切れたJSON/HTML」
// （JSON.parse()の例外やスクリプトの構文エラー）を受け取ることになる。いずれも症状としては
// 「何も起きていないように見える」失敗になり、切り分けが難しい。送信できたバイト数ぶんだけ
// ポインタを進めて全部送り切るまでループする。
static bool sendAll(SOCKET sock, const char *data, size_t len)
{
	size_t sent = 0;
	while (sent < len) {
		const int n = ::send(sock, data + sent, static_cast<int>(len - sent), 0);
		if (n == SOCKET_ERROR || n == 0)
			return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

// ─────────────────────────────────────────
// SHA-1 (RFC 3174)
// ─────────────────────────────────────────

static uint32_t rotl32(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

void WsServer::sha1(const uint8_t *data, size_t len, uint8_t digest[20])
{
	uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

	const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
	const size_t padded = ((len + 9 + 63) / 64) * 64;
	std::vector<uint8_t> msg(padded, 0);
	std::copy(data, data + len, msg.begin());
	msg[len] = 0x80;
	for (int i = 0; i < 8; ++i)
		msg[padded - 8 + i] = static_cast<uint8_t>((bitLen >> ((7 - i) * 8)) & 0xFF);

	for (size_t base = 0; base < padded; base += 64) {
		uint32_t w[80];
		for (int j = 0; j < 16; ++j)
			w[j] = (static_cast<uint32_t>(msg[base + j * 4]) << 24) |
			       (static_cast<uint32_t>(msg[base + j * 4 + 1]) << 16) |
			       (static_cast<uint32_t>(msg[base + j * 4 + 2]) << 8) |
			       static_cast<uint32_t>(msg[base + j * 4 + 3]);
		for (int j = 16; j < 80; ++j)
			w[j] = rotl32(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);

		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int j = 0; j < 80; ++j) {
			uint32_t f, k;
			if (j < 20) {
				f = (b & c) | (~b & d);
				k = 0x5A827999u;
			} else if (j < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1u;
			} else if (j < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDCu;
			} else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6u;
			}
			const uint32_t tmp = rotl32(a, 5) + f + e + k + w[j];
			e = d;
			d = c;
			c = rotl32(b, 30);
			b = a;
			a = tmp;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}

	for (int i = 0; i < 5; ++i) {
		digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
		digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
		digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
		digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
	}
}

// ─────────────────────────────────────────
// Base64 エンコード
// ─────────────────────────────────────────

std::string WsServer::base64Encode(const uint8_t *data, size_t len)
{
	static const char B64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	for (size_t i = 0; i < len; i += 3) {
		const uint32_t n =
			(static_cast<uint32_t>(data[i]) << 16) |
			(i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0u) |
			(i + 2 < len ? static_cast<uint32_t>(data[i + 2]) : 0u);
		out += B64[(n >> 18) & 0x3F];
		out += B64[(n >> 12) & 0x3F];
		out += (i + 1 < len) ? B64[(n >> 6) & 0x3F] : '=';
		out += (i + 2 < len) ? B64[n & 0x3F] : '=';
	}
	return out;
}

// ─────────────────────────────────────────
// WebSocket ハンドシェイクヘルパー
// ─────────────────────────────────────────

std::string WsServer::parseWsKey(const std::string &req)
{
	// ヘッダ名を小文字化して検索
	std::string lower = req;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	const std::string hdr = "sec-websocket-key:";
	const size_t pos = lower.find(hdr);
	if (pos == std::string::npos)
		return {};
	size_t start = pos + hdr.size();
	while (start < req.size() && req[start] == ' ')
		++start;
	const size_t end = req.find('\r', start);
	return req.substr(start, (end != std::string::npos ? end : req.size()) - start);
}

std::string WsServer::computeAcceptKey(const std::string &clientKey)
{
	const std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	uint8_t digest[20];
	sha1(reinterpret_cast<const uint8_t *>(combined.data()), combined.size(), digest);
	return base64Encode(digest, 20);
}

bool WsServer::readHttpRequest(SOCKET sock, std::string &outRequest)
{
	char buf[4096] = {};
	int total = 0;

	// \r\n\r\n が来るまで読み続ける
	while (total < static_cast<int>(sizeof(buf)) - 1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{5, 0};
		if (select(0, &fds, nullptr, nullptr, &tv) <= 0)
			return false;
		const int n = recv(sock, buf + total, sizeof(buf) - total - 1, 0);
		if (n <= 0)
			return false;
		total += n;
		buf[total] = '\0';
		if (strstr(buf, "\r\n\r\n"))
			break;
	}

	outRequest.assign(buf, total);
	return true;
}

bool WsServer::completeWsHandshake(SOCKET sock, const std::string &req)
{
	const std::string key = parseWsKey(req);
	if (key.empty()) {
		obs_log(LOG_WARNING, "[%s] Handshake: Sec-WebSocket-Key not found", WSTAG);
		return false;
	}

	const std::string resp =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: " +
		computeAcceptKey(key) + "\r\n\r\n";
	return sendAll(sock, resp.c_str(), resp.size());
}

bool WsServer::parseEmoteGetPath(const std::string &req, std::string &outFileName)
{
	if (req.compare(0, 4, "GET ") != 0)
		return false;
	const size_t pathEnd = req.find(' ', 4);
	if (pathEnd == std::string::npos)
		return false;
	const std::string rawPath = req.substr(4, pathEnd - 4);

	const std::string prefix = "/emotes/";
	if (rawPath.compare(0, prefix.size(), prefix) != 0)
		return false;

	const std::string decoded = urlDecode(rawPath.substr(prefix.size()));
	if (!isSafeFileName(decoded))
		return false;

	outFileName = decoded;
	return true;
}

// WAN公開対応（要件2、パストラバーサル防止の最終防御）: GetFullPathNameW()で".."等を
// 素朴に正規化した上で、結果がrootDir配下から外れていないかを確認する。呼び出し元
// （isSafeFileName()等）で既にファイル名を検証済みの経路もあるが、将来別の呼び出し元が
// 検証を怠っても、sendFileResponse()を経由する限り配信ルート外のファイルは読み出せない
// ようにする独立した最終防御として置く。大文字小文字を区別しない比較にする（NTFSの
// 既定動作に合わせる）。
static bool isPathWithinRoot(const std::wstring &filePath, const std::wstring &rootDir)
{
	wchar_t fullPath[MAX_PATH] = {};
	wchar_t fullRoot[MAX_PATH] = {};
	if (GetFullPathNameW(filePath.c_str(), MAX_PATH, fullPath, nullptr) == 0)
		return false;
	if (GetFullPathNameW(rootDir.c_str(), MAX_PATH, fullRoot, nullptr) == 0)
		return false;

	std::wstring normPath(fullPath);
	std::wstring normRoot(fullRoot);
	std::transform(normPath.begin(), normPath.end(), normPath.begin(),
	                [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
	std::transform(normRoot.begin(), normRoot.end(), normRoot.begin(),
	                [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
	if (normRoot.empty() || normRoot.back() != L'\\')
		normRoot += L'\\';
	return normPath.compare(0, normRoot.size(), normRoot) == 0;
}

// ディスク上のファイルを読み込みHTTPレスポンスとして返す共通ヘルパー
// （/emotes/<file> と /vrm_stage.html の配信で共用する）。rootDirは配信を許可する
// ルートディレクトリ（isPathWithinRoot()参照、パストラバーサル対策）。
static void sendFileResponse(SOCKET sock, const std::wstring &filePath, const std::wstring &rootDir,
                              const std::string &contentType, size_t maxSize,
                              const char *notFoundLogTag = nullptr)
{
	static const char *resp403 =
		"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp404 =
		"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp413 =
		"HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp500 =
		"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

	if (!isPathWithinRoot(filePath, rootDir)) {
		obs_log(LOG_WARNING, "[%s] sendFileResponse: 配信ルート外へのアクセスを拒否しました%s%s",
			WSTAG, notFoundLogTag ? ": " : "", notFoundLogTag ? notFoundLogTag : "");
		sendAll(sock, resp403, strlen(resp403));
		return;
	}

	HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		if (notFoundLogTag)
			obs_log(LOG_INFO, "[%s] file not found: %s", WSTAG, notFoundLogTag);
		sendAll(sock, resp404, strlen(resp404));
		return;
	}

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(hFile, &size) || size.QuadPart < 0 ||
	    static_cast<size_t>(size.QuadPart) > maxSize) {
		CloseHandle(hFile);
		sendAll(sock, resp413, strlen(resp413));
		return;
	}

	std::vector<char> data(static_cast<size_t>(size.QuadPart));
	DWORD readBytes = 0;
	const bool ok = data.empty() ||
	                (ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &readBytes,
	                          nullptr) && readBytes == data.size());
	CloseHandle(hFile);

	if (!ok) {
		sendAll(sock, resp500, strlen(resp500));
		return;
	}

	const std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + contentType + "\r\n"
		"Content-Length: " + std::to_string(data.size()) + "\r\n"
		"Cache-Control: no-cache, no-store, must-revalidate\r\n"
		"Pragma: no-cache\r\n"
		"Expires: 0\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n";
	if (sendAll(sock, header.c_str(), header.size()) && !data.empty())
		sendAll(sock, data.data(), data.size());
}

// メモリ上のバイナリをHTTPレスポンスとして返す共通ヘルパー（/vrm/model の配信で使用）。
static void sendBufferResponse(SOCKET sock, const std::vector<uint8_t> &data,
                                const std::string &contentType)
{
	const std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + contentType + "\r\n"
		"Content-Length: " + std::to_string(data.size()) + "\r\n"
		"Cache-Control: no-cache, no-store, must-revalidate\r\n"
		"Pragma: no-cache\r\n"
		"Expires: 0\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n";
	// VRMバイナリは数MB〜十数MBに達することがあり、単発のsend()では確実に部分送信になる
	// （Displayが受け取るボディがContent-Lengthより短くなり、fetch()が失敗する／glTFの
	// パースが壊れる原因になっていた。sendAll()参照）。
	if (sendAll(sock, header.c_str(), header.size()) && !data.empty()) {
		if (!sendAll(sock, reinterpret_cast<const char *>(data.data()), data.size())) {
			obs_log(LOG_WARNING,
				"[%s] sendBufferResponse: VRMボディ送信が完了しませんでした（%zu bytes）",
				WSTAG, data.size());
		}
	}
}

void WsServer::serveEmoteImage(SOCKET sock, const std::string &fileName)
{
	const std::wstring filePath = emotesImagesDirW() + L"\\" + utf8ToWide(fileName);
	sendFileResponse(sock, filePath, emotesImagesDirW(), contentTypeForFile(fileName),
	                  20 * 1024 * 1024, fileName.c_str());
}

// ─────────────────────────────────────────
// VRM Stage 連携ヘルパー
// ─────────────────────────────────────────

// プラグインのデータ配置先ルート（vrm_stage.html・user_settings.json共通の親ディレクトリ）。
// sendFileResponse()のパストラバーサル対策（isPathWithinRoot()）用のルートとしても使う。
static std::wstring pluginDataDirW()
{
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return std::wstring(appdata) + L"\\obs-studio\\plugins\\obs-live-hub";
}

// vrm_stage.html の配置先。ensureHtmlFileInAppData() がコピーする先と同一パスを指す
// （独立して算出する設計。emotesImagesDirW() 等、本ファイルの既存の各所と同様）。
static std::wstring vrmStageHtmlPathW()
{
	return pluginDataDirW() + L"\\vrm_stage.html";
}

// user_settings.json（TURNサーバー認証情報等、Git管理外の個人設定）の配置先。
// ensureUserSettingsFileInAppData()（plugin-main.cpp）が
// 存在しない場合のみuser_settings.example.jsonから複製する（既存ファイルは上書きしない）。
static std::wstring vrmUserSettingsPathW()
{
	return pluginDataDirW() + L"\\user_settings.json";
}

// リクエスト行が "<method> <path>"（クエリ文字列があれば無視）と厳密一致するか判定する。
static bool isRequestForPath(const std::string &req, const char *method, const char *path)
{
	const std::string methodPrefix = std::string(method) + " ";
	if (req.compare(0, methodPrefix.size(), methodPrefix) != 0)
		return false;
	const size_t pathStart = methodPrefix.size();
	const size_t pathEnd = req.find_first_of(" ?", pathStart);
	if (pathEnd == std::string::npos)
		return false;
	return req.compare(pathStart, pathEnd - pathStart, path) == 0;
}

static long parseContentLength(const std::string &req)
{
	std::string lower = req;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	const std::string hdr = "content-length:";
	const size_t pos = lower.find(hdr);
	if (pos == std::string::npos)
		return -1;
	size_t start = pos + hdr.size();
	while (start < req.size() && req[start] == ' ')
		++start;
	size_t end = req.find('\r', start);
	if (end == std::string::npos)
		end = req.size();
	try {
		return std::stol(req.substr(start, end - start));
	} catch (...) {
		return -1;
	}
}

// ヘッダ名（小文字）を指定して値を取り出す汎用ヘルパー（Content-Length以外の任意ヘッダ用）。
static std::string parseHeaderValue(const std::string &req, const std::string &headerNameLower)
{
	std::string lower = req;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	const std::string hdr = headerNameLower + ":";
	const size_t pos = lower.find(hdr);
	if (pos == std::string::npos)
		return {};
	size_t start = pos + hdr.size();
	while (start < req.size() && req[start] == ' ')
		++start;
	size_t end = req.find('\r', start);
	if (end == std::string::npos)
		end = req.size();
	return req.substr(start, end - start);
}

static std::string jsonEscape(const std::string &s)
{
	std::string r;
	r.reserve(s.size() + 16);
	for (unsigned char c : s) {
		if (c == '"')       r += "\\\"";
		else if (c == '\\') r += "\\\\";
		else if (c == '\n') r += "\\n";
		else if (c == '\r') r += "\\r";
		else if (c >= 0x20) r += static_cast<char>(c);
	}
	return r;
}

// 簡易JSON文字列フィールド検出（"key":"value"という定型の並びがtext中に含まれるかだけを
// 見る）。WsServer.cppはQt非依存のため本格的なJSONパーサを持たず、既存のparseQueryParam()
// 等と同様の手軽な文字列検索の方針を踏襲する。クライアント（JSON.stringify()）が生成する
// 出力はキーと文字列値の間に空白を挟まないため、この単純な部分一致で十分に検出できる。
// WebSocketメッセージ全般をここで本格的にパースする用途ではなく、緊急遮断
// （regenerate_token）のようにWsServer自身が直接介入する必要がある特定アクションの
// 検出のみに使う（他の大半のvrm_*系メッセージは内容を解釈せずそのまま中継する方針）。
static bool jsonHasStringField(const std::string &text, const std::string &key, const std::string &value)
{
	const std::string needle = "\"" + key + "\":\"" + value + "\"";
	return text.find(needle) != std::string::npos;
}

void WsServer::serveVrmStagePage(SOCKET sock)
{
	sendFileResponse(sock, vrmStageHtmlPathW(), pluginDataDirW(), "text/html; charset=UTF-8",
	                  5 * 1024 * 1024, "vrm_stage.html");
}

void WsServer::serveUserSettings(SOCKET sock)
{
	// TURNサーバー認証情報を含みうるファイルのため、存在しない場合は404を返すのみで
	// 自動生成はしない（自動生成はensureUserSettingsFileInAppData()がexampleから複製する形で
	// プラグイン起動時に一度だけ行う。ここでは配信のみを担当する）。
	sendFileResponse(sock, vrmUserSettingsPathW(), pluginDataDirW(), "application/json; charset=UTF-8",
	                  64 * 1024, "user_settings.json");
}

void WsServer::setTunnelInfo(bool active, const std::string &url)
{
	std::lock_guard<std::mutex> lock(tunnelInfoMutex_);
	tunnelActive_ = active;
	tunnelUrl_ = url;
}

void WsServer::serveTunnelInfo(SOCKET sock)
{
	bool active;
	std::string url;
	{
		std::lock_guard<std::mutex> lock(tunnelInfoMutex_);
		active = tunnelActive_;
		url = tunnelUrl_;
	}

	const std::string body = std::string("{\"active\":") + (active ? "true" : "false") +
	                          ",\"url\":\"" + jsonEscape(url) + "\"}";
	const std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json; charset=UTF-8\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"Cache-Control: no-cache, no-store, must-revalidate\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n";
	if (sendAll(sock, header.c_str(), header.size()))
		sendAll(sock, body.c_str(), body.size());
}

void WsServer::serveVrmModel(SOCKET sock)
{
	std::vector<uint8_t> data;
	{
		std::lock_guard<std::mutex> lock(modelMutex_);
		data = vrmModelData_; // ロック区間を短くするためコピーする
	}
	if (data.empty()) {
		static const char *resp404 =
			"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		sendAll(sock, resp404, strlen(resp404));
		return;
	}
	sendBufferResponse(sock, data, "application/octet-stream");
}

// Content-Lengthヘッダの値ぶんだけボディを読み切る共通ヘルパー（POST /vrm/model と
// POST /vrm/peer_upload で共用）。readHttpRequest()が既に読んでいたヘッダ後方のスピルオーバー
// 分を起点に、残りをrecv()で読み進める。失敗時は500応答を送信済みでfalseを返す。
//
// 実機バグ対策: Cloudflare Tunnel等のリバースプロキシ経由でWAN（例: iPhoneのモバイル回線）
// から約23MBのVRMをアップロードすると、45秒ほどでクライアント側に`Load failed`が発生し
// ホストへ届かない不具合が報告された。受信ループ自体はcontentLengthに達するまでrecv()を
// 回し続ける構造で問題なかったが、1回のrecv()が空振り（新規データが来ない）した場合の
// stallタイムアウトが10秒しかなく、モバイル回線+トンネル越しでは転送途中に10秒を超える
// 無通信区間が普通に発生しうるため、途中で500を返して打ち切ってしまっていた
// （23MB全体の転送時間ではなく、この「1回あたりの無通信許容時間」が短すぎたのが原因）。
// 大容量ファイル・低速回線を考慮し60〜120秒の余裕を持たせる。
static bool readHttpBodyByContentLength(SOCKET sock, const std::string &request, long contentLength,
                                         std::vector<uint8_t> &outBody)
{
	static const char *resp500 =
		"HTTP/1.1 500 Internal Server Error\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: 0\r\nConnection: close\r\n\r\n";
	static const int kBodyRecvStallTimeoutSec = 90;

	outBody.clear();
	outBody.reserve(static_cast<size_t>(contentLength));
	const size_t headerEnd = request.find("\r\n\r\n");
	if (headerEnd != std::string::npos) {
		const size_t spillStart = headerEnd + 4;
		if (spillStart < request.size())
			outBody.assign(request.begin() + spillStart, request.end());
	}
	if (outBody.size() > static_cast<size_t>(contentLength))
		outBody.resize(static_cast<size_t>(contentLength));

	while (outBody.size() < static_cast<size_t>(contentLength)) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{kBodyRecvStallTimeoutSec, 0};
		if (select(0, &fds, nullptr, nullptr, &tv) <= 0) {
			obs_log(LOG_WARNING,
				"[%s] HTTPボディ読み込み: recv timeout/error（%zu/%ld bytes受信済み、"
				"stallタイムアウト%d秒）",
				WSTAG, outBody.size(), contentLength, kBodyRecvStallTimeoutSec);
			sendAll(sock, resp500, strlen(resp500));
			return false;
		}
		char tmp[65536];
		const int n = recv(sock, tmp, sizeof(tmp), 0);
		if (n <= 0) {
			obs_log(LOG_WARNING,
				"[%s] HTTPボディ読み込み: recv()が%d（接続切断/エラー）、%zu/%ld bytes受信済み",
				WSTAG, n, outBody.size(), contentLength);
			sendAll(sock, resp500, strlen(resp500));
			return false;
		}
		outBody.insert(outBody.end(), tmp, tmp + n);
	}
	if (outBody.size() > static_cast<size_t>(contentLength))
		outBody.resize(static_cast<size_t>(contentLength));
	return true;
}

void WsServer::handleVrmModelUpload(SOCKET sock, const std::string &request)
{
	// 実機バグ対策: 従来はエラー応答（400/500）にAccess-Control-Allow-Originが
	// 含まれておらず、失敗時にブラウザがレスポンス本文/ステータスをJSへ渡せず
	// （CORSブロック）、実際のエラー理由（400/500/timeout）に関わらず一律
	// "Load failed"のような汎用ネットワークエラーとしてしか観測できなかった。
	// 200 OKと同様、全レスポンスにCORSヘッダーを付与し切り分けを容易にする。
	static const char *resp400 =
		"HTTP/1.1 400 Bad Request\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp403 =
		"HTTP/1.1 403 Forbidden\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp413 =
		"HTTP/1.1 413 Payload Too Large\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp200 =
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";

	// WAN公開対応（真の権限境界）: メインモデル(/vrm/model)を上書きできるのは
	// controllerSecretToken_（起動のたびランダム生成、OBSメニュー経由のURLにのみ埋め込まれる）
	// を提示できたリクエストのみに限定する。data/vrm_stage.html側のisControllerModeチェックは
	// あくまでJS側の自己防衛であり、WAN公開時は第三者が直接この/vrm/modelへPOSTしてくる
	// 可能性も考慮し、ここが実効的な権限チェックになる。ボディを読む前（＝大容量の受信に
	// 時間を使う前）に弾く。
	const std::string token = parseQueryParam(request, "token");
	if (controllerSecretToken_.empty() || token != controllerSecretToken_) {
		obs_log(LOG_WARNING,
			"[%s] POST /vrm/model: controller token不一致のため拒否しました (token_len=%zu)",
			WSTAG, token.size());
		sendAll(sock, resp403, strlen(resp403));
		return;
	}

	const long contentLength = parseContentLength(request);
	if (contentLength < 0) {
		sendAll(sock, resp400, strlen(resp400));
		return;
	}
	// WAN公開対応（要件1）: Content-Lengthの時点でサイズ超過が判明している場合は、
	// ソケットからのボディ受信（＝低速回線では長時間かかりうる）を一切行わず即座に
	// 413で拒否する。maxVrmUploadBytes_は2つのアップロードハンドラで共有する
	// 一元管理された上限値（既定50MB）。
	if (static_cast<size_t>(contentLength) > maxVrmUploadBytes_.load()) {
		obs_log(LOG_WARNING,
			"[%s] POST /vrm/model: Content-Length(%ld bytes)が上限(%zu bytes)を超過したため拒否しました",
			WSTAG, contentLength, maxVrmUploadBytes_.load());
		sendAll(sock, resp413, strlen(resp413));
		return;
	}

	std::vector<uint8_t> body;
	if (!readHttpBodyByContentLength(sock, request, contentLength, body))
		return; // 失敗時はreadHttpBodyByContentLength内で500応答済み

	std::string name = urlDecode(parseHeaderValue(request, "x-vrm-name"));
	if (name.empty())
		name = "model.vrm";

	{
		std::lock_guard<std::mutex> lock(modelMutex_);
		vrmModelData_ = std::move(body);
		vrmModelName_ = name;
	}

	obs_log(LOG_INFO, "[%s] VRMモデルをキャッシュしました: %s (%ld bytes)", WSTAG, name.c_str(),
	        contentLength);

	sendAll(sock, resp200, strlen(resp200));

	// Displayクライアント（OBSブラウザソース等）へモデル更新を通知する
	const std::string syncJson =
		"{\"type\":\"vrm_model_sync\",\"name\":\"" + jsonEscape(name) + "\"}";
	broadcast(syncJson);
}

std::string WsServer::parseQueryParam(const std::string &request, const std::string &paramName)
{
	const size_t lineEnd = request.find("\r\n");
	const std::string requestLine = (lineEnd != std::string::npos) ? request.substr(0, lineEnd) : request;
	const size_t qPos = requestLine.find('?');
	if (qPos == std::string::npos)
		return {};
	const size_t spacePos = requestLine.find(' ', qPos);
	const std::string query = requestLine.substr(
		qPos + 1, spacePos == std::string::npos ? std::string::npos : spacePos - qPos - 1);

	const std::string key = paramName + "=";
	size_t pos = 0;
	while (pos < query.size()) {
		const size_t amp = query.find('&', pos);
		const std::string pair =
			query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
		if (pair.compare(0, key.size(), key) == 0)
			return urlDecode(pair.substr(key.size()));
		if (amp == std::string::npos)
			break;
		pos = amp + 1;
	}
	return {};
}

void WsServer::servePeerModel(SOCKET sock, const std::string &peerId)
{
	std::vector<uint8_t> data;
	if (!peerId.empty()) {
		std::lock_guard<std::mutex> lock(peerModelsMutex_);
		auto it = peerModels_.find(peerId);
		if (it != peerModels_.end())
			data = it->second; // ロック区間を短くするためコピーする
	}
	if (data.empty()) {
		static const char *resp404 =
			"HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		sendAll(sock, resp404, strlen(resp404));
		return;
	}
	sendBufferResponse(sock, data, "application/octet-stream");
}

void WsServer::handlePeerModelUpload(SOCKET sock, const std::string &request, const std::string &peerId)
{
	static const char *resp400 =
		"HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp413 =
		"HTTP/1.1 413 Payload Too Large\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp200 =
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";

	if (peerId.empty()) {
		sendAll(sock, resp400, strlen(resp400));
		return;
	}

	const long contentLength = parseContentLength(request);
	if (contentLength < 0) {
		sendAll(sock, resp400, strlen(resp400));
		return;
	}
	// WAN公開対応（要件1）: handleVrmModelUpload()と同じ一元管理された上限値・即時413応答。
	if (static_cast<size_t>(contentLength) > maxVrmUploadBytes_.load()) {
		obs_log(LOG_WARNING,
			"[%s] POST /vrm/peer_upload: Content-Length(%ld bytes)が上限(%zu bytes)を超過したため拒否しました",
			WSTAG, contentLength, maxVrmUploadBytes_.load());
		sendAll(sock, resp413, strlen(resp413));
		return;
	}

	std::vector<uint8_t> body;
	if (!readHttpBodyByContentLength(sock, request, contentLength, body))
		return; // 失敗時はreadHttpBodyByContentLength内で500応答済み

	{
		std::lock_guard<std::mutex> lock(peerModelsMutex_);
		peerModels_[peerId] = std::move(body);
	}

	obs_log(LOG_INFO, "[%s] ピアVRMをキャッシュしました: peerId=%s (%ld bytes)", WSTAG, peerId.c_str(),
	        contentLength);

	sendAll(sock, resp200, strlen(resp200));

	// ルーム内の他ピアへこのpeerIdのモデルが取得可能になったことを通知する。サーバー側では
	// ルーム/宛先の概念を持たないため、他のvrm_*系メッセージと同じく全WebSocketクライアントへ
	// ブロードキャストし、宛先フィルタ（roomId一致・ロースターに実在するpeerIdか）は
	// vrm_stage.html側のJSが行う。
	const std::string readyJson =
		"{\"type\":\"vrm_peer_model_ready\",\"peerId\":\"" + jsonEscape(peerId) + "\"}";
	broadcast(readyJson);
}

void WsServer::handlePeerModelRemove(SOCKET sock, const std::string &peerId)
{
	static const char *resp200 =
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
	if (!peerId.empty()) {
		std::lock_guard<std::mutex> lock(peerModelsMutex_);
		const size_t erased = peerModels_.erase(peerId);
		if (erased > 0)
			obs_log(LOG_INFO, "[%s] ピアVRMを破棄しました: peerId=%s", WSTAG, peerId.c_str());
	}
	sendAll(sock, resp200, strlen(resp200));
}

// ─────────────────────────────────────────
// WebSocket フレームエンコード (サーバー→クライアント、マスクなし)
// ─────────────────────────────────────────

std::vector<uint8_t> WsServer::encodeTextFrame(const std::string &text)
{
	std::vector<uint8_t> frame;
	frame.push_back(0x81); // FIN=1, opcode=0x1 (テキスト)
	const size_t len = text.size();
	if (len <= 125) {
		frame.push_back(static_cast<uint8_t>(len));
	} else if (len <= 65535) {
		frame.push_back(126);
		frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
		frame.push_back(static_cast<uint8_t>(len & 0xFF));
	} else {
		frame.push_back(127);
		for (int i = 7; i >= 0; --i)
			frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
	}
	frame.insert(frame.end(), text.begin(), text.end());
	return frame;
}

// ─────────────────────────────────────────
// WsServer 本体
// ─────────────────────────────────────────

WsServer::WsServer(uint16_t port)
	: port_(port), controllerSecretToken_(generateSecureRandomToken(32))
{
}

WsServer::~WsServer()
{
	stop();
}

bool WsServer::start()
{
	if (running_.load())
		return true;

	listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock_ == INVALID_SOCKET) {
		obs_log(LOG_WARNING, "[%s] socket() failed, WSA=%d", WSTAG, WSAGetLastError());
		return false;
	}

	// SO_REUSEADDR は Windows では複数ソケットが同一ポートへ同時 bind できてしまう。
	// ゾンビソケットと新サーバーが共存して接続を奪い合う問題の根本原因となるため使わない。
	// SO_EXCLUSIVEADDRUSE: ゾンビソケットがポートを保持している場合に bind() を即失敗させ
	// 問題を可視化する。また自分が LISTEN 中は他プロセスからの同ポート bind を拒否する。
	int opt = 1;
	if (setsockopt(listenSock_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		       reinterpret_cast<const char *>(&opt), sizeof(opt)) == SOCKET_ERROR) {
		obs_log(LOG_WARNING, "[%s] setsockopt(SO_EXCLUSIVEADDRUSE) failed WSA=%d — 続行します",
			WSTAG, WSAGetLastError());
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	// 2026-08-24: マルチユーザーVRM通話機能（vrm_stage.html）のシグナリングサーバーとして
	// LAN内の他PCからも到達できる必要があるため、INADDR_LOOPBACKからINADDR_ANYへ変更した。
	// これによりWsServerの全機能（コメント表示・TTS・VMC受信結果配信等を含む）が同一LAN上の
	// 他ホストから到達可能になる（ユーザー承認済みの意図的な変更。詳細はai_logs参照）。
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port_);

	// クラッシュ直後の再起動等、直前のOBSプロセスがまだ終了処理中でポートを
	// 解放し切っていないタイミングでbind()するとWSAEADDRINUSE(10048)になり得る。
	// SO_REUSEADDRでbind()自体を成功させる方式はWindowsではLISTEN中の別ソケットへの
	// 同時bind()まで許してしまう「ポート乗っ取り」のリスクがあるため採用せず
	// （直前セッションでSO_EXCLUSIVEADDRUSEを採用した経緯とも矛盾するため）、
	// 実際にOSがポートを解放するまで短い間隔で数回だけリトライする。
	constexpr int  kBindMaxRetries   = 5;
	constexpr DWORD kBindRetryDelayMs = 300;
	int  bindErr = 0;
	bool bound   = false;
	for (int attempt = 0; attempt < kBindMaxRetries; ++attempt) {
		if (bind(listenSock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != SOCKET_ERROR) {
			bound = true;
			break;
		}
		bindErr = WSAGetLastError();
		if (bindErr != WSAEADDRINUSE)
			break;
		obs_log(LOG_WARNING,
			"[%s] bind() WSAEADDRINUSE (試行 %d/%d) — 直前のプロセスがport %u を解放するのを待って"
			"再試行します",
			WSTAG, attempt + 1, kBindMaxRetries, port_);
		Sleep(kBindRetryDelayMs);
	}
	if (!bound) {
		obs_log(LOG_ERROR,
			"[%s] bind() FAILED: port %u が別プロセスに占有されています (WSA=%d)。"
			"コマンドプロンプトで「netstat -ano | findstr :%u」を実行してポートを"
			"保持しているプロセスを確認してください。PCを再起動すると解消する場合があります。",
			WSTAG, port_, bindErr, port_);
		closesocket(listenSock_);
		listenSock_ = INVALID_SOCKET;
		listenState_.store(ListenState::BindFailed);
		return false;
	}
	if (listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) {
		const int wsaErr = WSAGetLastError();
		obs_log(LOG_WARNING, "[%s] listen() failed WSA=%d", WSTAG, wsaErr);
		closesocket(listenSock_);
		listenSock_ = INVALID_SOCKET;
		listenState_.store(ListenState::ListenFailed);
		return false;
	}

	listenState_.store(ListenState::Listening);
	running_.store(true);
	acceptThread_ = std::thread(&WsServer::acceptLoop, this);
	obs_log(LOG_INFO,
		"[%s] Listening on ws://0.0.0.0:%u (INADDR_ANY — LAN内の他PCからも到達可能です)",
		WSTAG, port_);
	return true;
}

void WsServer::stop()
{
	if (!running_.exchange(false))
		return;

	listenState_.store(ListenState::NotStarted);

	// listenSock_ を閉じて acceptLoop の select() を抜ける
	if (listenSock_ != INVALID_SOCKET) {
		closesocket(listenSock_);
		listenSock_ = INVALID_SOCKET;
	}

	// クライアントソケットを shutdown して recv() を即座に抜けさせる
	{
		std::lock_guard<std::mutex> lock(clientsMutex_);
		for (SOCKET s : clients_)
			shutdown(s, SD_BOTH);
	}

	if (acceptThread_.joinable())
		acceptThread_.join();

	// クライアントスレッドが全員 activeClients_ をデクリメントするまで待つ
	int waitMs = 0;
	while (activeClients_.load() > 0 && waitMs < 2000) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		waitMs += 10;
	}

	obs_log(LOG_INFO, "[%s] Server stopped.", WSTAG);
}

// WAN公開対応（緊急遮断）: stop()の「クライアントソケットをshutdownしてrecv()を即座に
// 抜けさせる」処理と同じ手法を、サーバー自体は稼働させたまま単独で使えるようにした版。
// 各clientLoop()スレッドは shutdown() 後、recv()<=0を検知して自分自身で
// clients_からの除去・closesocket()・activeClients_のデクリメントを行う（stop()同様、
// ここでは待ち合わせしない）。
void WsServer::disconnectAllClients()
{
	std::lock_guard<std::mutex> lock(clientsMutex_);
	for (SOCKET s : clients_)
		shutdown(s, SD_BOTH);
}

// WAN公開対応（緊急遮断）: clientLoop()から、isAuthorizedController検証済みの
// regenerate_token要求に対してのみ呼ばれる。
void WsServer::handleRegenerateTokenRequest(SOCKET sock)
{
	const std::string newToken = generateSecureRandomToken(32);
	controllerSecretToken_ = newToken;
	obs_log(LOG_WARNING,
		"[%s] 緊急遮断: controllerSecretTokenを再発行し、全クライアントを強制切断します",
		WSTAG);

	// 新tokenは要求元の接続（sock）へのみ直接返す。broadcast()は使わない
	// ——全クライアントへ届いてしまうと新tokenが他者に漏洩し、緊急遮断（招待・接続の
	// リセット）の意味が無くなるため。
	const std::string tokenJson = "{\"type\":\"vrm_token_regenerated\",\"token\":\"" + newToken + "\"}";
	const auto frame = encodeTextFrame(tokenJson);
	sendAll(sock, reinterpret_cast<const char *>(frame.data()), frame.size());

	// 要求元自身を含む全クライアントを強制切断する。旧tokenを保持していた全ての接続
	// （ブラウザの別タブ・キャッシュされたURL等）を確実に無効化するのが目的のため、
	// 要求元だけを除外することはしない（要求元はdata/vrm_stage.html側が上で受け取った
	// 新tokenを使い、connectWsWatchdog()経由で自動的に再接続する）。
	disconnectAllClients();
}

// 実機バグ対策（ログ肥大化）: VMC受信・Webカメラトラッキング稼働時、これらのtypeは
// 毎秒10〜数十回broadcast()される（vrm_pose_update/vrm_peer_motionは約30Hz、
// vrm_vmc_updateはVmcReceiverが受信したフレームレート依存、vrm_transform_updateも
// スライダードラッグ中は最短100ms間隔）。broadcast()側の通常ログ（JSON全文＋送信結果の
// 2行/回）を毎回出していると、これだけでOBSログが瞬時に埋め尽くされ、他の重要な
// イベント（エラー・接続/切断・ルーム管理等）が見えなくなっていた。これら高頻度な
// モーション/位置同期メッセージのみログ出力をスキップする（低頻度な制御メッセージ
// ——vrm_room_control・vrm_auth_result・vrm_peer_model_ready・コメント表示・TTS等——は
// 引き続き通常通りログに残す）。
static bool isHighFrequencyBroadcastType(const std::string &jsonText)
{
	static const char *kHighFreqTypes[] = {
		"vrm_vmc_update", "vrm_pose_update", "vrm_peer_motion", "vrm_transform_update", nullptr
	};
	for (int i = 0; kHighFreqTypes[i]; ++i) {
		if (jsonHasStringField(jsonText, "type", kHighFreqTypes[i]))
			return true;
	}
	return false;
}

void WsServer::broadcast(const std::string &jsonText)
{
	if (!running_.load()) {
		obs_log(LOG_WARNING, "[%s] broadcast: server not running, dropped: %s", WSTAG,
			jsonText.c_str());
		return;
	}

	const auto frame = encodeTextFrame(jsonText);
	std::lock_guard<std::mutex> lock(clientsMutex_);
	const size_t total = clients_.size();
	const bool skipLog = isHighFrequencyBroadcastType(jsonText);
	if (!skipLog)
		obs_log(LOG_INFO, "[%s] broadcast: %zu client(s) — %s", WSTAG, total, jsonText.c_str());

	int sent = 0;
	auto it = clients_.begin();
	while (it != clients_.end()) {
		// sendAll(): 1回のsend()で全バイト送れる保証がない（部分送信）。特に
		// vrm_room_control/vrm_call_signal（SDPを含み数百〜数KBになりうる）で
		// これを怠ると、受信側は「途中で切れたJSON」を受け取ってJSON.parse()に失敗し、
		// 承認・SDP/ICE交換が理由不明のまま止まって見える不具合の原因になっていた。
		if (!sendAll(*it, reinterpret_cast<const char *>(frame.data()), frame.size())) {
			// 送信失敗（クライアント切断等）は高頻度メッセージであっても常にログを残す
			// （エラー診断のための情報のため、ログ抑制の対象外とする）。
			obs_log(LOG_WARNING, "[%s] broadcast: send failed, removing client (WSA=%d)",
				WSTAG, WSAGetLastError());
			closesocket(*it);
			it = clients_.erase(it);
		} else {
			++sent;
			++it;
		}
	}

	if (!skipLog)
		obs_log(LOG_INFO, "[%s] broadcast: sent to %d/%zu client(s)", WSTAG, sent,
			static_cast<size_t>(total));
}

void WsServer::acceptLoop()
{
	obs_log(LOG_INFO, "[%s] acceptLoop() started, listenSock_=%d",
	        WSTAG, static_cast<int>(listenSock_));

	int loopCount = 0;
	while (running_.load()) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listenSock_, &fds);
		timeval tv{1, 0}; // select タイムアウト 1 秒
		const int sel = select(0, &fds, nullptr, nullptr, &tv);

		++loopCount;

		// 60秒ごとにハートビートログ（ループが生きているか・接続数の確認）
		if (loopCount % 60 == 0) {
			obs_log(LOG_INFO,
			        "[%s] heartbeat #%d: activeClients=%d listenSock=%d WSA=%d",
			        WSTAG, loopCount / 60,
			        activeClients_.load(),
			        static_cast<int>(listenSock_),
			        WSAGetLastError());
		}

		// select() エラー時は毎回ログ（ポート競合・ソケット異常を検出するため）
		if (sel < 0) {
			obs_log(LOG_WARNING,
			        "[%s] select() error: WSA=%d sock=%d tick=%d",
			        WSTAG, WSAGetLastError(),
			        static_cast<int>(listenSock_),
			        loopCount);
		}

		if (sel <= 0)
			continue;

		obs_log(LOG_INFO, "[%s] select()=%d (readable), calling accept()", WSTAG, sel);

		SOCKET client = accept(listenSock_, nullptr, nullptr);
		if (client == INVALID_SOCKET) {
			obs_log(LOG_WARNING,
			        "[%s] accept() returned INVALID_SOCKET, WSA=%d",
			        WSTAG, WSAGetLastError());
			continue;
		}

		obs_log(LOG_INFO, "[%s] Incoming connection, starting handshake", WSTAG);
		std::thread([this, client]() { clientLoop(client); }).detach();
	}

	obs_log(LOG_INFO, "[%s] acceptLoop() exiting", WSTAG);
}

void WsServer::setMessageCallback(std::function<void(const std::string &, bool)> cb)
{
	std::lock_guard<std::mutex> lock(callbackMutex_);
	messageCallback_ = std::move(cb);
}

void WsServer::setConnectCallback(std::function<void()> cb)
{
	std::lock_guard<std::mutex> lock(callbackMutex_);
	connectCallback_ = std::move(cb);
}

void WsServer::clientLoop(SOCKET sock)
{
	++activeClients_;

	std::string request;
	if (!readHttpRequest(sock, request)) {
		obs_log(LOG_WARNING, "[%s] Failed to read HTTP request", WSTAG);
		closesocket(sock);
		--activeClients_;
		return;
	}

	// GET /emotes/<filename> は簡易HTTPレスポンスとして画像を配信する
	// （WebSocketへのアップグレードは行わずここで完結させる）
	std::string emoteFileName;
	if (parseEmoteGetPath(request, emoteFileName)) {
		serveEmoteImage(sock, emoteFileName);
		closesocket(sock);
		--activeClients_;
		return;
	}

	// VRM Stage連携: vrm_stage.html本体・VRMモデルのHTTP配信/アップロードも
	// WebSocketへのアップグレードを行わずここで完結させる
	if (isRequestForPath(request, "GET", "/vrm_stage.html")) {
		serveVrmStagePage(sock);
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "GET", "/vrm/user_settings.json")) {
		serveUserSettings(sock);
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "GET", "/vrm/tunnel_info")) {
		serveTunnelInfo(sock);
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "GET", "/vrm/model")) {
		serveVrmModel(sock);
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "POST", "/vrm/model")) {
		handleVrmModelUpload(sock, request);
		closesocket(sock);
		--activeClients_;
		return;
	}
	// Controller（file://等の別オリジン）からの POST /vrm/model はカスタムヘッダー
	// （X-Vrm-Name）とContent-Type:application/octet-streamの組み合わせによりCORS
	// プリフライト（OPTIONS）が飛ぶため、これに応答してからでないと本POSTが送られない。
	if (isRequestForPath(request, "OPTIONS", "/vrm/model")) {
		static const char *respOptions =
			"HTTP/1.1 204 No Content\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type, X-Vrm-Name\r\n"
			"Access-Control-Max-Age: 86400\r\n"
			"Content-Length: 0\r\n"
			"Connection: close\r\n\r\n";
		sendAll(sock, respOptions, strlen(respOptions));
		closesocket(sock);
		--activeClients_;
		return;
	}

	// マルチユーザーVRM通話: 参加者ごとのVRMバイナリのHTTP一括転送（peerId指定）。
	if (isRequestForPath(request, "GET", "/vrm/peer_model")) {
		servePeerModel(sock, parseQueryParam(request, "peerId"));
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "POST", "/vrm/peer_upload")) {
		handlePeerModelUpload(sock, request, parseQueryParam(request, "peerId"));
		closesocket(sock);
		--activeClients_;
		return;
	}
	if (isRequestForPath(request, "POST", "/vrm/peer_remove")) {
		handlePeerModelRemove(sock, parseQueryParam(request, "peerId"));
		closesocket(sock);
		--activeClients_;
		return;
	}
	// POST /vrm/peer_upload はContent-Type:application/octet-streamのPOSTのため、
	// クロスオリジンから呼ばれた場合はCORSプリフライト（OPTIONS）が飛ぶ（/vrm/modelと同様）。
	if (isRequestForPath(request, "OPTIONS", "/vrm/peer_upload")
	    || isRequestForPath(request, "OPTIONS", "/vrm/peer_remove")) {
		static const char *respOptions =
			"HTTP/1.1 204 No Content\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
			"Access-Control-Allow-Headers: Content-Type\r\n"
			"Access-Control-Max-Age: 86400\r\n"
			"Content-Length: 0\r\n"
			"Connection: close\r\n\r\n";
		sendAll(sock, respOptions, strlen(respOptions));
		closesocket(sock);
		--activeClients_;
		return;
	}

	if (!completeWsHandshake(sock, request)) {
		obs_log(LOG_WARNING, "[%s] Handshake failed", WSTAG);
		closesocket(sock);
		--activeClients_;
		return;
	}

	{
		std::lock_guard<std::mutex> lock(clientsMutex_);
		clients_.push_back(sock);
	}
	obs_log(LOG_INFO, "[%s] Client connected (total: %zu)", WSTAG, clients_.size());

	// WAN公開対応: このWebSocket接続がmode=controllerを名乗っている場合のみ、
	// controllerSecretToken_の検証結果を"vrm_auth_result"として一度だけ返す。
	// data/vrm_stage.html側はこれを見てisControllerModeを実際の検証結果に合わせる
	// （不一致ならDisplayモード相当・閲覧専用へ自動的に格下げする）。
	// isAuthorizedControllerはこの接続の生存期間中（clientLoop()関数スコープ）保持し、
	// 以降このソケットから届く全メッセージをmessageCallback_へ渡す際の第2引数として使う
	// （set_max_vrm_size等、サーバー設定を変更するアクションの権限チェック用。
	// クライアントのJS状態ではなく、ハンドシェイク時にサーバー自身が検証した結果のみを
	// 根拠にする——真の権限境界はここ）。
	bool isAuthorizedController = false;
	if (parseQueryParam(request, "mode") == "controller") {
		const std::string token = parseQueryParam(request, "token");
		isAuthorizedController = !controllerSecretToken_.empty() && token == controllerSecretToken_;
		if (!isAuthorizedController) {
			obs_log(LOG_WARNING,
				"[%s] WebSocket handshake: mode=controllerのtoken不一致 (token_len=%zu)",
				WSTAG, token.size());
		}
		const std::string authJson = std::string("{\"type\":\"vrm_auth_result\",\"controllerAuthorized\":")
			+ (isAuthorizedController ? "true" : "false") + "}";
		const auto authFrame = encodeTextFrame(authJson);
		sendAll(sock, reinterpret_cast<const char *>(authFrame.data()), authFrame.size());
	}

	{
		std::lock_guard<std::mutex> lock(callbackMutex_);
		if (connectCallback_)
			connectCallback_();
	}

	std::vector<uint8_t> rxBuf;
	rxBuf.reserve(4096);
	bool shouldClose = false;

	while (running_.load() && !shouldClose) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{1, 0};
		const int sel = select(0, &fds, nullptr, nullptr, &tv);
		if (sel < 0)
			break;
		if (sel == 0)
			continue;

		char tmp[4096];
		const int n = recv(sock, tmp, sizeof(tmp), 0);
		if (n <= 0)
			break;

		rxBuf.insert(rxBuf.end(), tmp, tmp + n);

		// 完全なフレームをパース
		while (!shouldClose && rxBuf.size() >= 2) {
			const uint8_t b0      = rxBuf[0];
			const uint8_t b1      = rxBuf[1];
			const uint8_t opcode  = b0 & 0x0F;
			const bool    masked  = (b1 & 0x80) != 0;
			uint64_t      payloadLen = b1 & 0x7F;

			size_t headerSize = 2;
			if (payloadLen == 126) {
				if (rxBuf.size() < 4) break;
				payloadLen = (static_cast<uint64_t>(rxBuf[2]) << 8) | rxBuf[3];
				headerSize = 4;
			} else if (payloadLen == 127) {
				if (rxBuf.size() < 10) break;
				payloadLen = 0;
				for (int i = 0; i < 8; ++i)
					payloadLen = (payloadLen << 8) | rxBuf[2 + i];
				headerSize = 10;
			}
			if (masked)
				headerSize += 4;

			if (rxBuf.size() < headerSize + payloadLen)
				break; // フレーム未完成 — 次のrecv待ち

			if (opcode == 0x08) { // Close
				shouldClose = true;
			} else if (opcode == 0x01 && masked) { // Text フレーム（クライアント→サーバーは常にマスク）
				const uint8_t *maskKey = rxBuf.data() + (headerSize - 4);
				std::string text(payloadLen, '\0');
				for (uint64_t i = 0; i < payloadLen; ++i)
					text[i] = static_cast<char>(rxBuf[headerSize + i] ^ maskKey[i % 4]);

				// WAN公開対応（緊急遮断）: regenerate_tokenだけはmessageCallback_
				// （plugin-main.cpp）経由の中継に流さず、ここで直接処理する。理由:
				// 新しいtokenを安全に返送できるのは「この接続（sock）へ直接」のみであり
				// （broadcast()は全クライアントへ届くため新tokenが漏洩する）、socketとの
				// 紐付けを持たないmessageCallback_のシグネチャではそれができないため。
				if (jsonHasStringField(text, "type", "vrm_room_control") &&
				    jsonHasStringField(text, "action", "regenerate_token")) {
					if (isAuthorizedController) {
						handleRegenerateTokenRequest(sock);
					} else {
						obs_log(LOG_WARNING,
							"[%s] regenerate_token: 未認証の接続からのリクエストを拒否しました",
							WSTAG);
					}
				} else {
					std::function<void(const std::string &, bool)> cb;
					{
						std::lock_guard<std::mutex> lock(callbackMutex_);
						cb = messageCallback_;
					}
					if (cb)
						cb(text, isAuthorizedController);
				}
			}
			rxBuf.erase(rxBuf.begin(),
			            rxBuf.begin() + headerSize + static_cast<size_t>(payloadLen));
		}
	}

	{
		std::lock_guard<std::mutex> lock(clientsMutex_);
		clients_.erase(std::remove(clients_.begin(), clients_.end(), sock), clients_.end());
	}
	obs_log(LOG_INFO, "[%s] Client disconnected (total: %zu)", WSTAG, clients_.size());
	closesocket(sock);
	--activeClients_;
}

#endif // _WIN32
