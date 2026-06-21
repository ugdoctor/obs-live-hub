#pragma once
#ifdef _WIN32

#include <atomic>
#include <functional>
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
	enum class ListenState {
		NotStarted,  // 未起動 or 正常停止
		Listening,   // bind/listen 成功・稼働中
		BindFailed,  // bind() 失敗（ゾンビソケット等によるポート占有）
		ListenFailed // listen() 失敗
	};

	explicit WsServer(uint16_t port);
	~WsServer();

	bool start();
	void stop();
	bool isRunning() const { return running_.load(); }
	ListenState listenState() const { return listenState_.load(); }
	uint16_t port() const { return port_; }

	// 接続確立済みクライアント数を返す (スレッドセーフ)
	int clientCount() const
	{
		std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(clientsMutex_));
		return static_cast<int>(clients_.size());
	}

	// JSON テキストを全 WebSocket クライアントにブロードキャスト (スレッドセーフ)
	void broadcast(const std::string &jsonText);

	// クライアントからのテキストフレームを受け取るコールバック（WsServerスレッドから呼ばれる）
	void setMessageCallback(std::function<void(const std::string &)> cb);

	// 新規クライアント接続時コールバック（WsServerスレッドから呼ばれる）
	void setConnectCallback(std::function<void()> cb);

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
	std::atomic<ListenState> listenState_{ListenState::NotStarted};
	std::atomic<int> activeClients_{0};
	std::thread acceptThread_;
	std::mutex clientsMutex_;
	std::vector<SOCKET> clients_;
	std::function<void(const std::string &)> messageCallback_;
	std::function<void()>                    connectCallback_;
	std::mutex callbackMutex_;
};

#endif // _WIN32
