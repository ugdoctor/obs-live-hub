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

#include <obs-module.h>
#include <plugin-support.h>

static const char *WSTAG = "WsServer";

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

bool WsServer::doHandshake(SOCKET sock)
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

	const std::string req(buf, total);
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

	int opt = 1;
	setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR,
		   reinterpret_cast<const char *>(&opt), sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port_);

	if (bind(listenSock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		obs_log(LOG_WARNING, "[%s] bind() failed on port %u, WSA=%d", WSTAG, port_,
			WSAGetLastError());
		closesocket(listenSock_);
		listenSock_ = INVALID_SOCKET;
		return false;
	}
	if (listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) {
		obs_log(LOG_WARNING, "[%s] listen() failed, WSA=%d", WSTAG, WSAGetLastError());
		closesocket(listenSock_);
		listenSock_ = INVALID_SOCKET;
		return false;
	}

	running_.store(true);
	acceptThread_ = std::thread(&WsServer::acceptLoop, this);
	obs_log(LOG_INFO, "[%s] Listening on ws://127.0.0.1:%u", WSTAG, port_);
	return true;
}

void WsServer::stop()
{
	if (!running_.exchange(false))
		return;

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
	while (running_.load()) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listenSock_, &fds);
		timeval tv{1, 0};
		if (select(0, &fds, nullptr, nullptr, &tv) <= 0)
			continue;

		SOCKET client = accept(listenSock_, nullptr, nullptr);
		if (client == INVALID_SOCKET)
			continue;

		obs_log(LOG_INFO, "[%s] Incoming connection, starting handshake", WSTAG);
		std::thread([this, client]() { clientLoop(client); }).detach();
	}
}

void WsServer::clientLoop(SOCKET sock)
{
	++activeClients_;

	if (!doHandshake(sock)) {
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

	char buf[256];
	while (running_.load()) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{1, 0};
		const int sel = select(0, &fds, nullptr, nullptr, &tv);
		if (sel < 0)
			break; // stop() でソケットが閉じられた
		if (sel == 0)
			continue; // タイムアウト — running_ を再チェック

		const int n = recv(sock, buf, sizeof(buf), 0);
		if (n <= 0)
			break; // 切断 or shutdown() による EOF

		// WebSocket Close フレーム (opcode 0x8)
		if (n >= 2 && (buf[0] & 0x0F) == 0x08)
			break;
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
