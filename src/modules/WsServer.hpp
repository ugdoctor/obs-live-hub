#pragma once
#ifdef _WIN32

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// RFC 6455 WebSocket サーバー (WinSock2 ベース、TLS なし)
// 2026-08-24: マルチユーザーVRM通話のシグナリング用途でLAN内の他PCからも到達できる必要が
// あるため、INADDR_ANY (0.0.0.0) でバインドする（旧: 127.0.0.1のみのループバック専用）。
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

	// WAN公開対応: プラグイン起動（＝WsServerインスタンス生成）のたびにランダム生成される
	// Controllerシークレットトークン。OBSメニュー（plugin-main.cpp）がVRM Stageの
	// Controller起動URLへ埋め込み、data/vrm_stage.html側がWebSocket接続・POST /vrm/model
	// のクエリ文字列へ含めて送り返してくる。mode=controllerを名乗るだけの自己申告
	// （旧来のisControllerMode）ではURLさえ知っていれば誰でもなりすませてしまうため、
	// 本トークンでサーバー側が独立に検証することで実効的な権限境界にする。
	const std::string &controllerSecretToken() const { return controllerSecretToken_; }

	// WAN公開対応: VRMアップロード（POST /vrm/model・POST /vrm/peer_upload）の許容最大
	// バイト数。既定は50MB。値としてWsServer内で一元管理し、2箇所のハンドラで同じ値を
	// 参照する（従来は256MBのローカル定数が2箇所に個別定義されており、変更時に更新漏れの
	// リスクがあった）。setMaxVrmUploadBytes()で起動時に変更可能（現状呼び出し元は無いが、
	// 将来設定UIから調整可能にする余地を残す）。
	size_t maxVrmUploadBytes() const { return maxVrmUploadBytes_; }
	void setMaxVrmUploadBytes(size_t bytes) { maxVrmUploadBytes_ = bytes; }

	// 接続確立済みクライアント数を返す (スレッドセーフ)
	int clientCount() const
	{
		std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(clientsMutex_));
		return static_cast<int>(clients_.size());
	}

	// JSON テキストを全 WebSocket クライアントにブロードキャスト (スレッドセーフ)
	void broadcast(const std::string &jsonText);

	// クライアントからのテキストフレームを受け取るコールバック（WsServerスレッドから呼ばれる）。
	// 第2引数isAuthorizedControllerは、このメッセージを送ってきた接続がWebSocket
	// ハンドシェイク時にcontrollerSecretTokenの検証に成功した（mode=controllerかつ
	// token一致）接続かどうかを表す。set_max_vrm_size等、サーバー側の設定を変更する
	// アクションの権限チェックに使う（クライアント側のJS状態を信用せず、接続確立時に
	// サーバー自身が検証した結果のみを根拠にする）。
	void setMessageCallback(std::function<void(const std::string &, bool isAuthorizedController)> cb);

	// 新規クライアント接続時コールバック（WsServerスレッドから呼ばれる）
	void setConnectCallback(std::function<void()> cb);

private:
	void acceptLoop();
	void clientLoop(SOCKET sock);

	// WAN公開対応（緊急遮断）: 認証済みControllerからのregenerate_token要求を処理する。
	// controllerSecretToken_を新しいランダム値へ差し替え、新tokenを要求元の接続（sock）
	// へのみ返信した上で（broadcast()は使わない。全クライアントへ届くと新tokenが
	// 漏洩し緊急遮断の意味が無くなるため）、接続中の全クライアント（要求元自身を含む）を
	// 強制切断する。clientLoop()のメッセージ処理内から、isAuthorizedController検証済みの
	// 場合のみ呼ばれる。
	void handleRegenerateTokenRequest(SOCKET sock);
	// 接続中の全WebSocketクライアントを強制切断する（サーバー自体は停止しない）。
	// stop()の全クライアントshutdown処理と同じ手法をサーバー稼働中に単独で使えるようにした版。
	void disconnectAllClients();

	// リクエスト行＋ヘッダ（\r\n\r\nまで）を読み込む。WebSocketハンドシェイクと
	// 簡易HTTP画像配信（GET /emotes/...）で共用する。
	static bool readHttpRequest(SOCKET sock, std::string &outRequest);
	static bool completeWsHandshake(SOCKET sock, const std::string &request);
	static std::string parseWsKey(const std::string &req);
	static std::string computeAcceptKey(const std::string &clientKey);
	static std::string base64Encode(const uint8_t *data, size_t len);
	static void sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
	static std::vector<uint8_t> encodeTextFrame(const std::string &text);

	// 簡易HTTP画像配信（YouTubeカスタムエモート辞書用）。
	// GET /emotes/<filename> なら true を返し outFileName にURLデコード済み・
	// パストラバーサル検証済みのファイル名を格納する。
	static bool parseEmoteGetPath(const std::string &request, std::string &outFileName);
	static void serveEmoteImage(SOCKET sock, const std::string &fileName);

	// VRM Stage連携（vrm_stage.htmlのHTTP配信 + Controllerが読み込んだVRMバイナリの
	// プラグイン側キャッシュ・Display側への配信）。
	// GET  /vrm_stage.html : vrm_stage.html を text/html として配信する
	//                        （OBSブラウザソースを http://127.0.0.1:<port>/... で統一するため）
	// GET  /vrm/model       : 最後にアップロードされたVRMバイナリを配信する（未アップロード時は404）
	// POST /vrm/model       : VRMバイナリ本体をリクエストボディで受け取りメモリキャッシュし、
	//                        全WebSocketクライアントへ "vrm_model_sync" をブロードキャストする
	void serveVrmStagePage(SOCKET sock);
	void serveVrmModel(SOCKET sock);
	void handleVrmModelUpload(SOCKET sock, const std::string &request);
	// GET /vrm/user_settings.json: TURNサーバー設定等（user_settings.json）を配信する。
	// マルチユーザーVRM通話機能（Git管理外の個人設定）用。
	void serveUserSettings(SOCKET sock);

	// マルチユーザーVRM通話（フェーズ2、2026-08-24よりHTTP一括転送方式）: 各参加者が自分の
	// VRMバイナリをHTTP経由でこのプラグインへ一時的に保持させ、他の参加者はHTTP GETで
	// 一括取得する。以前のWebSocketメッセージによるBase64チャンク分割送信
	// （vrm_peer_model_start/chunk/end）は、大きなテキストフレームの送受信で稀にチャンクが
	// 脱落する実機報告を受けて廃止した（詳細はdata/vrm_stage.html側のコメント・ai_logs参照）。
	// POST /vrm/peer_upload?peerId=<id> : ボディのVRMバイナリをpeerModels_[id]へ保持し、
	//                                     全WebSocketクライアントへ"vrm_peer_model_ready"を
	//                                     ブロードキャストする
	// GET  /vrm/peer_model?peerId=<id>  : peerModels_[id]をapplication/octet-streamで返す
	//                                     （未保持時は404）
	// POST /vrm/peer_remove?peerId=<id> : peerModels_[id]を即座に破棄する（退出・キック時）
	void handlePeerModelUpload(SOCKET sock, const std::string &request, const std::string &peerId);
	void servePeerModel(SOCKET sock, const std::string &peerId);
	void handlePeerModelRemove(SOCKET sock, const std::string &peerId);
	// リクエスト行のクエリ文字列（?key=value&...）からkeyに対応する値をURLデコードして返す
	// （無ければ空文字列）。
	static std::string parseQueryParam(const std::string &request, const std::string &paramName);

	uint16_t port_;
	// WAN公開対応: 起動のたびに新しくランダム生成される（コンストラクタで初期化）。
	// controllerSecretToken()参照。
	std::string controllerSecretToken_;
	// WAN公開対応: maxVrmUploadBytes()参照。既定50MB（52,428,800 bytes）。
	std::atomic<size_t> maxVrmUploadBytes_{50 * 1024 * 1024};
	SOCKET listenSock_ = INVALID_SOCKET;
	std::atomic<bool> running_{false};
	std::atomic<ListenState> listenState_{ListenState::NotStarted};
	std::atomic<int> activeClients_{0};
	std::thread acceptThread_;
	std::mutex clientsMutex_;
	std::vector<SOCKET> clients_;
	std::function<void(const std::string &, bool isAuthorizedController)> messageCallback_;
	std::function<void()>                    connectCallback_;
	std::mutex callbackMutex_;

	// VRM Stage: Controllerがアップロードした最新のVRMバイナリ（メモリキャッシュのみ、
	// ディスク永続化はしない。OBS/プラグイン再起動後はControllerの再読み込み時に再アップロードされる）
	std::mutex modelMutex_;
	std::vector<uint8_t> vrmModelData_;
	std::string vrmModelName_;

	// マルチユーザーVRM通話: 参加者ごとのVRMバイナリ（メモリキャッシュのみ、ディスク永続化なし。
	// 要件3「IndexedDB/localStorageへは一切保存しない」のサーバー側での相当方針）。
	// peerIdは接続確立ごとにJS側でランダム生成される使い捨てID（generatePeerId()参照）のため、
	// 無制限に増え続けることはない（ルーム退出・キック時にhandlePeerModelRemove()で破棄される）。
	std::mutex peerModelsMutex_;
	std::map<std::string, std::vector<uint8_t>> peerModels_;
};

#endif // _WIN32
