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
// VSeeFace等の外部トラッキングソフトが送信するボーン姿勢・ブレンドシェイプ値を
// UDPで受信し、約30〜60Hzに間引いてJSON化したスナップショットをコールバックで通知する。
// 実際のWebSocketブロードキャストは呼び出し側（plugin-main.cpp）が担当し、
// 本クラス自体はWsServerに依存しない（単体で完結する）。
//
// セキュリティ: WsServerと同様にループバック（127.0.0.1）のみで待ち受け、
// 外部ネットワークには公開しない。VSeeFace等を別PCで動かし本機能へ送信したい場合は
// 対応していない（既知の制約）。
class VmcReceiver {
public:
	explicit VmcReceiver(uint16_t port);
	~VmcReceiver();

	bool start();
	void stop();
	bool isRunning() const { return running_.load(); }
	uint16_t port() const { return port_; }

	// 集約済みJSONスナップショット（type: "vrm_vmc_update"）の通知コールバック。
	// VmcReceiver自身の受信スレッドから呼ばれる（呼び出し側でスレッドセーフに処理すること）。
	void setUpdateCallback(std::function<void(const std::string &)> cb);

private:
	struct BoneSample {
		float px = 0, py = 0, pz = 0;
		float qx = 0, qy = 0, qz = 0, qw = 1;
	};
	using OscArg = std::variant<float, int32_t, std::string, bool>;

	void recvLoop();
	void handlePacket(const uint8_t *data, size_t len);
	void maybeFlush();

	// OSC パケット解析（例外を投げない。壊れたパケットは黙って無視する）
	void parseOscPacket(const uint8_t *data, size_t len);
	void parseOscBundle(const uint8_t *data, size_t len);
	void parseOscMessage(const uint8_t *data, size_t len);
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

	std::mutex callbackMutex_;
	std::function<void(const std::string &)> updateCallback_;
};

#endif // _WIN32
