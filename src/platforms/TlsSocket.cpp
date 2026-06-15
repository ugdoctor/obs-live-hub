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

#include "TlsSocket.hpp"

#include <string>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TLSTAG = "TlsSocket";

TlsSocket::TlsSocket() = default;

TlsSocket::~TlsSocket()
{
	close();
}

// ---- 公開インターフェース ----

bool TlsSocket::connectTo(const std::string &host, uint16_t port)
{
	// TCP 接続
	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo *res = nullptr;
	if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
		return false;

	sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock_ == INVALID_SOCKET) {
		freeaddrinfo(res);
		return false;
	}

	const bool ok = (::connect(sock_, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0);
	freeaddrinfo(res);
	if (!ok) {
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
		return false;
	}

	// TLS ハンドシェイク
	if (!doHandshake(host)) {
		close();
		return false;
	}

	connected_ = true;
	return true;
}

bool TlsSocket::send(const std::string &data)
{
	std::lock_guard<std::mutex> lock(sendMutex_);

	if (!connected_ || sock_ == INVALID_SOCKET)
		return false;

	// EncryptMessage: [ヘッダ][プレーンテキスト][トレーラ] の連続バッファで暗号化
	const DWORD msgLen = static_cast<DWORD>(data.size());
	std::vector<BYTE> buf(streamSizes_.cbHeader + msgLen + streamSizes_.cbTrailer);
	memcpy(buf.data() + streamSizes_.cbHeader, data.data(), msgLen);

	SecBuffer bufs[4];
	bufs[0] = {streamSizes_.cbHeader, SECBUFFER_STREAM_HEADER, buf.data()};
	bufs[1] = {msgLen, SECBUFFER_DATA, buf.data() + streamSizes_.cbHeader};
	bufs[2] = {streamSizes_.cbTrailer, SECBUFFER_STREAM_TRAILER,
		   buf.data() + streamSizes_.cbHeader + msgLen};
	bufs[3] = {0, SECBUFFER_EMPTY, nullptr};
	SecBufferDesc desc = {SECBUFFER_VERSION, 4, bufs};

	if (EncryptMessage(&hCtx_, 0, &desc, 0) != SEC_E_OK)
		return false;

	const DWORD total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
	return sendRaw(buf.data(), total);
}

bool TlsSocket::receive(std::string &outData, int timeoutMs)
{
	if (!connected_ || sock_ == INVALID_SOCKET) {
		obs_log(LOG_WARNING, "[%s] receive: called but not connected", TLSTAG);
		return false;
	}

	// 前回の余剰データを引き継ぐ
	std::vector<BYTE> encBuf = std::move(extraBuf_);
	extraBuf_.clear();

	if (!encBuf.empty()) {
		obs_log(LOG_INFO, "[%s] receive: starting with %zu extraBuf bytes", TLSTAG,
			encBuf.size());
	}

	while (true) {
		// 暗号化データが不足していれば受信
		if (encBuf.size() < 5) { // TLS レコードヘッダは最低 5 バイト
			BYTE tmp[16384];
			const int got = recvRaw(tmp, sizeof(tmp), timeoutMs);
			if (got == 0)
				return true; // タイムアウト — エラーではない。outData は空のまま
			if (got < 0)
				return false; // 接続切断またはエラー
			encBuf.insert(encBuf.end(), tmp, tmp + got);
		}

		SecBuffer bufs[4] = {{static_cast<ULONG>(encBuf.size()), SECBUFFER_DATA,
				      encBuf.data()},
				     {0, SECBUFFER_EMPTY, nullptr},
				     {0, SECBUFFER_EMPTY, nullptr},
				     {0, SECBUFFER_EMPTY, nullptr}};
		SecBufferDesc desc = {SECBUFFER_VERSION, 4, bufs};

		const SECURITY_STATUS ss = DecryptMessage(&hCtx_, &desc, 0, nullptr);

		if (ss == SEC_E_OK) {
			for (int i = 0; i < 4; i++) {
				if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0)
					outData.append(static_cast<char *>(bufs[i].pvBuffer),
						       bufs[i].cbBuffer);
				// 余剰データ（次の TLS レコード）を保存
				else if (bufs[i].BufferType == SECBUFFER_EXTRA &&
					 bufs[i].cbBuffer > 0) {
					auto *p = static_cast<BYTE *>(bufs[i].pvBuffer);
					extraBuf_.assign(p, p + bufs[i].cbBuffer);
					obs_log(LOG_INFO,
						"[%s] receive: saved %zu extra bytes for next call",
						TLSTAG, extraBuf_.size());
				}
			}
			return true;
		} else if (ss == SEC_E_INCOMPLETE_MESSAGE) {
			obs_log(LOG_INFO,
				"[%s] receive: SEC_E_INCOMPLETE_MESSAGE (%zu bytes so far), reading more...",
				TLSTAG, encBuf.size());
			BYTE tmp[16384];
			const int got = recvRaw(tmp, sizeof(tmp), timeoutMs);
			if (got <= 0)
				return false;
			encBuf.insert(encBuf.end(), tmp, tmp + got);
		} else if (ss == SEC_I_CONTEXT_EXPIRED) {
			obs_log(LOG_INFO, "[%s] receive: SEC_I_CONTEXT_EXPIRED (server sent close_notify)",
				TLSTAG);
			connected_ = false;
			return false;
		} else if (ss == SEC_I_RENEGOTIATE) {
			// TLS 再ネゴシエーション要求 — 無視して次のレコードへ
			obs_log(LOG_INFO, "[%s] receive: SEC_I_RENEGOTIATE (ignored)", TLSTAG);
			encBuf.clear();
		} else {
			obs_log(LOG_WARNING, "[%s] receive: DecryptMessage failed, status=0x%08X",
				TLSTAG, static_cast<unsigned>(ss));
			return false;
		}
	}
}

void TlsSocket::close()
{
	std::lock_guard<std::mutex> lock(sendMutex_);

	connected_ = false;

	if (sock_ != INVALID_SOCKET) {
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
	}
	if (hasCtxHandle_) {
		DeleteSecurityContext(&hCtx_);
		hasCtxHandle_ = false;
	}
	if (hasCredHandle_) {
		FreeCredentialsHandle(&hCred_);
		hasCredHandle_ = false;
	}
	extraBuf_.clear();
}

// ---- 内部ヘルパー ----

bool TlsSocket::doHandshake(const std::string &host)
{
	// SChannel クライアント資格情報を取得
	SCHANNEL_CRED cred{};
	cred.dwVersion = SCHANNEL_CRED_VERSION;
	// grbitEnabledProtocols = 0 → システムデフォルト (TLS 1.2+) を使用
	cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

	SECURITY_STATUS ss =
		AcquireCredentialsHandleA(nullptr, const_cast<LPSTR>(UNISP_NAME_A),
					  SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr,
					  nullptr, &hCred_, nullptr);
	if (ss != SEC_E_OK)
		return false;
	hasCredHandle_ = true;

	// InitializeSecurityContext はホスト名を wchar_t で要求する
	std::wstring whost(host.begin(), host.end());

	constexpr DWORD ctxReq = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
				  ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
				  ISC_REQ_STREAM;
	DWORD ctxAttr = 0;

	std::vector<BYTE> inData(32768);
	DWORD inLen = 0;
	bool firstCall = true;

	while (true) {
		SecBuffer outBuf = {0, SECBUFFER_TOKEN, nullptr};
		SecBufferDesc outDesc = {SECBUFFER_VERSION, 1, &outBuf};

		SecBuffer inBufs[2] = {{inLen, SECBUFFER_TOKEN, inData.data()},
				       {0, SECBUFFER_EMPTY, nullptr}};
		SecBufferDesc inDesc = {SECBUFFER_VERSION, 2, inBufs};

		ss = InitializeSecurityContextW(&hCred_, firstCall ? nullptr : &hCtx_,
						firstCall ? whost.data() : nullptr, ctxReq, 0,
						0, firstCall ? nullptr : &inDesc, 0, &hCtx_,
						&outDesc, &ctxAttr, nullptr);
		firstCall = false;
		hasCtxHandle_ = true;

		// ハンドシェイクデータをサーバーへ送信
		if (outBuf.pvBuffer && outBuf.cbBuffer > 0) {
			sendRaw(static_cast<BYTE *>(outBuf.pvBuffer), outBuf.cbBuffer);
			FreeContextBuffer(outBuf.pvBuffer);
		}

		if (ss == SEC_E_OK) {
			// ハンドシェイク完了 - 余剰データを保存
			if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0) {
				auto *p = static_cast<BYTE *>(inBufs[1].pvBuffer);
				extraBuf_.assign(p, p + inBufs[1].cbBuffer);
			}
			break;
		} else if (ss == SEC_I_CONTINUE_NEEDED) {
			// ISC が消費しなかったデータを先頭に詰める
			if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0) {
				const DWORD extra = inBufs[1].cbBuffer;
				memmove(inData.data(), inData.data() + inLen - extra, extra);
				inLen = extra;
			} else {
				inLen = 0;
			}
			// サーバーからの続きを受信
			const int got = recvRaw(inData.data() + inLen,
						static_cast<int>(inData.size() - inLen), 15000);
			if (got <= 0)
				return false;
			inLen += got;
		} else if (ss == SEC_E_INCOMPLETE_MESSAGE) {
			const int got = recvRaw(inData.data() + inLen,
						static_cast<int>(inData.size() - inLen), 15000);
			if (got <= 0)
				return false;
			inLen += got;
		} else {
			return false;
		}
	}

	QueryContextAttributes(&hCtx_, SECPKG_ATTR_STREAM_SIZES, &streamSizes_);
	return true;
}

bool TlsSocket::sendRaw(const BYTE *buf, DWORD len)
{
	DWORD sent = 0;
	while (sent < len) {
		const int n = ::send(sock_, reinterpret_cast<const char *>(buf + sent),
				     static_cast<int>(len - sent), 0);
		if (n == SOCKET_ERROR)
			return false;
		sent += n;
	}
	return true;
}

int TlsSocket::recvRaw(BYTE *buf, int maxLen, int timeoutMs)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(sock_, &fds);
	timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	const int sel = select(0, &fds, nullptr, nullptr, &tv);
	if (sel == 0) {
		obs_log(LOG_WARNING, "[%s] recvRaw: select() timeout (%d ms)", TLSTAG, timeoutMs);
		return 0;
	}
	if (sel < 0) {
		obs_log(LOG_WARNING, "[%s] recvRaw: select() error, WSA=%d", TLSTAG,
			WSAGetLastError());
		return -1;
	}
	const int got =
		::recv(sock_, reinterpret_cast<char *>(buf), maxLen, 0);
	if (got == 0) {
		// サーバーが接続を閉じた。タイムアウト(0)と区別するため -1 で返す
		obs_log(LOG_INFO, "[%s] recvRaw: server closed connection (recv=0)", TLSTAG);
		return -1;
	}
	if (got < 0) {
		obs_log(LOG_WARNING, "[%s] recvRaw: recv() error, WSA=%d", TLSTAG,
			WSAGetLastError());
		return -1;
	}
	obs_log(LOG_INFO, "[%s] recvRaw: got %d bytes", TLSTAG, got);
	return got;
}

#endif // _WIN32
