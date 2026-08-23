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

#include "WsServer.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <obs-module.h>
#include <plugin-support.h>

static const char *WSTAG = "WsServer";

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
	return ::send(sock, resp.c_str(), static_cast<int>(resp.size()), 0) ==
	       static_cast<int>(resp.size());
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

// ディスク上のファイルを読み込みHTTPレスポンスとして返す共通ヘルパー
// （/emotes/<file> と /vrm_stage.html の配信で共用する）。
static void sendFileResponse(SOCKET sock, const std::wstring &filePath,
                              const std::string &contentType, size_t maxSize,
                              const char *notFoundLogTag = nullptr)
{
	static const char *resp404 =
		"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp413 =
		"HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp500 =
		"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

	HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		if (notFoundLogTag)
			obs_log(LOG_INFO, "[%s] file not found: %s", WSTAG, notFoundLogTag);
		::send(sock, resp404, static_cast<int>(strlen(resp404)), 0);
		return;
	}

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(hFile, &size) || size.QuadPart < 0 ||
	    static_cast<size_t>(size.QuadPart) > maxSize) {
		CloseHandle(hFile);
		::send(sock, resp413, static_cast<int>(strlen(resp413)), 0);
		return;
	}

	std::vector<char> data(static_cast<size_t>(size.QuadPart));
	DWORD readBytes = 0;
	const bool ok = data.empty() ||
	                (ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &readBytes,
	                          nullptr) && readBytes == data.size());
	CloseHandle(hFile);

	if (!ok) {
		::send(sock, resp500, static_cast<int>(strlen(resp500)), 0);
		return;
	}

	const std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + contentType + "\r\n"
		"Content-Length: " + std::to_string(data.size()) + "\r\n"
		"Cache-Control: no-cache\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n";
	::send(sock, header.c_str(), static_cast<int>(header.size()), 0);
	if (!data.empty())
		::send(sock, data.data(), static_cast<int>(data.size()), 0);
}

// メモリ上のバイナリをHTTPレスポンスとして返す共通ヘルパー（/vrm/model の配信で使用）。
static void sendBufferResponse(SOCKET sock, const std::vector<uint8_t> &data,
                                const std::string &contentType)
{
	const std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + contentType + "\r\n"
		"Content-Length: " + std::to_string(data.size()) + "\r\n"
		"Cache-Control: no-cache\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Connection: close\r\n\r\n";
	::send(sock, header.c_str(), static_cast<int>(header.size()), 0);
	if (!data.empty())
		::send(sock, reinterpret_cast<const char *>(data.data()),
		       static_cast<int>(data.size()), 0);
}

void WsServer::serveEmoteImage(SOCKET sock, const std::string &fileName)
{
	const std::wstring filePath = emotesImagesDirW() + L"\\" + utf8ToWide(fileName);
	sendFileResponse(sock, filePath, contentTypeForFile(fileName), 20 * 1024 * 1024,
	                  fileName.c_str());
}

// ─────────────────────────────────────────
// VRM Stage 連携ヘルパー
// ─────────────────────────────────────────

// vrm_stage.html の配置先。ensureHtmlFileInAppData() がコピーする先と同一パスを指す
// （独立して算出する設計。emotesImagesDirW() 等、本ファイルの既存の各所と同様）。
static std::wstring vrmStageHtmlPathW()
{
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return std::wstring(appdata) + L"\\obs-studio\\plugins\\obs-live-hub\\vrm_stage.html";
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

void WsServer::serveVrmStagePage(SOCKET sock)
{
	sendFileResponse(sock, vrmStageHtmlPathW(), "text/html; charset=UTF-8", 5 * 1024 * 1024,
	                  "vrm_stage.html");
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
		::send(sock, resp404, static_cast<int>(strlen(resp404)), 0);
		return;
	}
	sendBufferResponse(sock, data, "application/octet-stream");
}

void WsServer::handleVrmModelUpload(SOCKET sock, const std::string &request)
{
	static const char *resp400 =
		"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp413 =
		"HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp500 =
		"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	static const char *resp200 =
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";

	const long contentLength = parseContentLength(request);
	const size_t kMaxVrmSize = 256 * 1024 * 1024; // 256MB上限（異常アップロードへの防御）
	if (contentLength < 0 || static_cast<size_t>(contentLength) > kMaxVrmSize) {
		::send(sock, resp400, static_cast<int>(strlen(resp400)), 0);
		return;
	}

	// readHttpRequest() が読んだバッファの中に、ヘッダに続くボディの先頭部分（スピルオーバー）が
	// 既に含まれている場合があるため、それを起点に残りをrecv()で読み進める。
	std::vector<uint8_t> body;
	body.reserve(static_cast<size_t>(contentLength));
	const size_t headerEnd = request.find("\r\n\r\n");
	if (headerEnd != std::string::npos) {
		const size_t spillStart = headerEnd + 4;
		if (spillStart < request.size())
			body.assign(request.begin() + spillStart, request.end());
	}
	if (body.size() > static_cast<size_t>(contentLength))
		body.resize(static_cast<size_t>(contentLength));

	while (body.size() < static_cast<size_t>(contentLength)) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{10, 0};
		if (select(0, &fds, nullptr, nullptr, &tv) <= 0) {
			obs_log(LOG_WARNING, "[%s] VRMアップロード: recv timeout/error", WSTAG);
			::send(sock, resp500, static_cast<int>(strlen(resp500)), 0);
			return;
		}
		char tmp[65536];
		const int n = recv(sock, tmp, sizeof(tmp), 0);
		if (n <= 0) {
			::send(sock, resp500, static_cast<int>(strlen(resp500)), 0);
			return;
		}
		body.insert(body.end(), tmp, tmp + n);
	}
	if (body.size() > static_cast<size_t>(contentLength))
		body.resize(static_cast<size_t>(contentLength));

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

	::send(sock, resp200, static_cast<int>(strlen(resp200)), 0);

	// Displayクライアント（OBSブラウザソース等）へモデル更新を通知する
	const std::string syncJson =
		"{\"type\":\"vrm_model_sync\",\"name\":\"" + jsonEscape(name) + "\"}";
	broadcast(syncJson);
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

WsServer::WsServer(uint16_t port) : port_(port) {}

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
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port_);

	if (bind(listenSock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		const int wsaErr = WSAGetLastError();
		obs_log(LOG_ERROR,
			"[%s] bind() FAILED: port %u が別プロセスに占有されています (WSA=%d)。"
			"コマンドプロンプトで「netstat -ano | findstr :%u」を実行してポートを"
			"保持しているプロセスを確認してください。PCを再起動すると解消する場合があります。",
			WSTAG, port_, wsaErr, port_);
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
	obs_log(LOG_INFO, "[%s] Listening on ws://127.0.0.1:%u", WSTAG, port_);
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
	obs_log(LOG_INFO, "[%s] broadcast: %zu client(s) — %s", WSTAG, total, jsonText.c_str());

	int sent = 0;
	auto it = clients_.begin();
	while (it != clients_.end()) {
		const int n = ::send(*it, reinterpret_cast<const char *>(frame.data()),
				     static_cast<int>(frame.size()), 0);
		if (n == SOCKET_ERROR) {
			obs_log(LOG_WARNING, "[%s] broadcast: send failed, removing client (WSA=%d)",
				WSTAG, WSAGetLastError());
			closesocket(*it);
			it = clients_.erase(it);
		} else {
			++sent;
			++it;
		}
	}

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

void WsServer::setMessageCallback(std::function<void(const std::string &)> cb)
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
		::send(sock, respOptions, static_cast<int>(strlen(respOptions)), 0);
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

				std::function<void(const std::string &)> cb;
				{
					std::lock_guard<std::mutex> lock(callbackMutex_);
					cb = messageCallback_;
				}
				if (cb)
					cb(text);
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
