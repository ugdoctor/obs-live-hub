#pragma once
#ifdef _WIN32

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// winsock2 は windows.h より前にインクルードしなければならない
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>

// WinSock2 + SChannel による TLS ソケット（Windows 専用）。
// send() と close() は別スレッドから呼ばれる可能性があるため内部で mutex を保持する。
class TlsSocket {
public:
	TlsSocket();
	~TlsSocket();

	// host:port へ TCP 接続し TLS ハンドシェイクを実行する。成功で true。
	bool connectTo(const std::string &host, uint16_t port);

	// 暗号化して送信する。別スレッドから close() が呼ばれても安全。
	bool send(const std::string &data);

	// 暗号化データを受信して復号し outData に追記する。
	// timeoutMs 経過またはソケットが閉じられた場合は false。
	bool receive(std::string &outData, int timeoutMs = 5000);

	// ソケットを閉じる。receive() 中の別スレッドはエラーで抜ける。
	void close();

	bool isConnected() const { return connected_; }

private:
	bool doHandshake(const std::string &host);
	bool sendRaw(const BYTE *buf, DWORD len);
	int recvRaw(BYTE *buf, int maxLen, int timeoutMs);

	SOCKET sock_ = INVALID_SOCKET;
	CredHandle hCred_{};
	CtxtHandle hCtx_{};
	SecPkgContext_StreamSizes streamSizes_{};
	bool hasCredHandle_ = false;
	bool hasCtxHandle_ = false;
	bool connected_ = false;

	std::vector<BYTE> extraBuf_; // 前回の receive() で余ったデータ
	std::mutex sendMutex_;       // send() と close() の競合を防ぐ
};

#endif // _WIN32
