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

#pragma once
#ifdef _WIN32

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// VMCプロトコル（VirtualMotionCapture Protocol、OSC over UDP）受信機。
// VSeeFace / Webcam Motion Capture（WMC）等の外部トラッキングソフトが送信する
// ボーン姿勢・ブレンドシェイプ値をUDPで受信し、約30〜60Hzに間引いてJSON化した
// スナップショットをコールバックで通知する。実際のWebSocketブロードキャストは
// 呼び出し側（plugin-main.cpp）が担当し、本クラス自体はWsServerに依存しない。
//
// セキュリティ: INADDR_ANY（0.0.0.0）でバインドする。送信元がローカルPC上の
// 127.0.0.1からだけでなく、同一PCの別ネットワークインターフェース経由（例:
// 仮想アダプタ経由でのループバック相当の通信）で送られてくるツールにも対応する
// ための仕様変更（2026-08-24）。UDPは応答を返さない受信専用のため、悪用されても
// 外部からデータを取得される経路にはならないが、LAN内の他ホストからも本ポートへ
// パケットを送りつけられるようになる点は既知のトレードオフとして明記しておく。
class VmcReceiver {
public:
	// json: 集約済みスナップショット（type: "vrm_vmc_update"）
	// rawBytes: 直近に受信した生UDPパケットのバイト数
	// boneCount / morphCount: スナップショットに含まれるボーン数・モーフ数
	using UpdateCallback =
		std::function<void(const std::string &json, size_t rawBytes, size_t boneCount, size_t morphCount)>;

	explicit VmcReceiver(uint16_t port);
	~VmcReceiver();

	bool start();
	void stop();
	bool isRunning() const { return running_.load(); }
	uint16_t port() const { return port_; }

	// VmcReceiver自身の受信スレッドから呼ばれる（呼び出し側でスレッドセーフに処理すること）。
	void setUpdateCallback(UpdateCallback cb);

private:
	struct BoneSample {
		float px = 0, py = 0, pz = 0;
		float qx = 0, qy = 0, qz = 0, qw = 1;
	};
	using OscArg = std::variant<float, int32_t, std::string, bool>;

	void recvLoop();
	bool handlePacket(const uint8_t *data, size_t len); // 戻り値: パース成功したか
	void maybeFlush(size_t lastPacketBytes);
	void logParseErrorRateLimited(const char *reason);

	// OSC パケット解析（例外を投げない。壊れたパケットは黙って無視する。戻り値は成功可否）
	bool parseOscPacket(const uint8_t *data, size_t len);
	bool parseOscBundle(const uint8_t *data, size_t len);
	bool parseOscMessage(const uint8_t *data, size_t len);
	void dispatchOscMessage(const std::string &address, const std::vector<OscArg> &args);

	// stateMutex_ を保持した状態で呼ぶこと（内部専用）
	std::string buildSnapshotJsonLocked() const;
	static std::string boneSampleJson(const BoneSample &b);

	uint16_t port_;
	SOCKET sock_ = INVALID_SOCKET;
	std::atomic<bool> running_{false};
	std::thread thread_;

	std::mutex stateMutex_;
	std::unordered_map<std::string, BoneSample> bones_;
	std::unordered_map<std::string, float> blendShapes_;
	BoneSample root_;
	bool hasRoot_ = false;
	bool dirty_ = false;
	uint64_t lastFlushMs_ = 0;

	// 受信スレッド（recvLoop）だけが読み書きするため排他制御は不要
	uint64_t lastRawLogMs_ = 0;
	uint64_t lastErrorLogMs_ = 0;

	std::mutex callbackMutex_;
	UpdateCallback updateCallback_;
};

#endif // _WIN32
