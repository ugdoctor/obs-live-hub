#pragma once
#ifdef _WIN32

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// RFC 6455 WebSocket サーバー (WinSock2 ベース、TLS なし)
// ブラウザソースからのローカル接続専用 (127.0.0.1)
class WsServer {
public:
	explicit WsServer(uint16_t port);
	~WsServer();

	bool start();
	void stop();
	bool isRunning() const { return running_.load(); }
	uint16_t port() const { return port_; }

	// JSON テキストを全 WebSocket クライアントにブロードキャスト (スレッドセーフ)
	void broadcast(const std::string &jsonText);

private:
	void acceptLoop();
	void clientLoop(SOCKET sock);

	static bool doHandshake(SOCKET sock);
	static std::string parseWsKey(const std::string &req);
	static std::string computeAcceptKey(const std::string &clientKey);
	static std::string base64Encode(const uint8_t *data, size_t len);
	static void sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
	static std::vector<uint8_t> encodeTextFrame(const std::string &text);

	uint16_t port_;
	SOCKET listenSock_ = INVALID_SOCKET;
	std::atomic<bool> running_{false};
	std::atomic<int> activeClients_{0};
	std::thread acceptThread_;
	std::mutex clientsMutex_;
	std::vector<SOCKET> clients_;
};

#endif // _WIN32
