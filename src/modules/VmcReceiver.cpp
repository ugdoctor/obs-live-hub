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

#include "VmcReceiver.hpp"

#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <obs-module.h>
#include <plugin-support.h>

static const char *VMCTAG = "VmcReceiver";

// 集約スナップショットの送出間隔。要件の「約30〜60Hz」の中間を採用。
static constexpr int kFlushIntervalMs = 22; // 約45Hz
// 診断ログ（RAW受信・パースエラー）のレート制限間隔。
static constexpr uint64_t kDiagLogIntervalMs = 1000; // 1秒に1回まで

// ─────────────────────────────────────────
// OSC パケット読み取りヘルパー（境界チェック付き、失敗時は false を返すのみで例外は投げない）
// ─────────────────────────────────────────

static bool oscReadString(const uint8_t *data, size_t len, size_t &offset, std::string &out)
{
	if (offset >= len)
		return false;
	size_t i = offset;
	while (i < len && data[i] != 0)
		++i;
	if (i >= len)
		return false; // 終端(NUL)が見つからない = 壊れたパケット
	out.assign(reinterpret_cast<const char *>(data + offset), i - offset);
	const size_t strLenWithNul = (i - offset) + 1;
	const size_t padded = (strLenWithNul + 3) & ~static_cast<size_t>(3);
	if (offset + padded > len)
		return false;
	offset += padded;
	return true;
}

static bool oscReadFloat32(const uint8_t *data, size_t len, size_t &offset, float &out)
{
	if (offset + 4 > len)
		return false;
	const uint32_t bits = (static_cast<uint32_t>(data[offset]) << 24) |
	                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
	                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
	                       static_cast<uint32_t>(data[offset + 3]);
	std::memcpy(&out, &bits, sizeof(out));
	offset += 4;
	return true;
}

static bool oscReadInt32(const uint8_t *data, size_t len, size_t &offset, int32_t &out)
{
	if (offset + 4 > len)
		return false;
	const uint32_t bits = (static_cast<uint32_t>(data[offset]) << 24) |
	                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
	                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
	                       static_cast<uint32_t>(data[offset + 3]);
	out = static_cast<int32_t>(bits);
	offset += 4;
	return true;
}

// ─────────────────────────────────────────
// JSON構築ヘルパー
// ─────────────────────────────────────────

static std::string jsonEscape(const std::string &s)
{
	std::string r;
	r.reserve(s.size() + 16);
	for (unsigned char c : s) {
		if (c == '"')
			r += "\\\"";
		else if (c == '\\')
			r += "\\\\";
		else if (c == '\n')
			r += "\\n";
		else if (c == '\r')
			r += "\\r";
		else if (c >= 0x20)
			r += static_cast<char>(c);
	}
	return r;
}

static std::string fixedFloat(float v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(v));
	return buf;
}

// ─────────────────────────────────────────
// VmcReceiver 本体
// ─────────────────────────────────────────

VmcReceiver::VmcReceiver(uint16_t port) : port_(port) {}

VmcReceiver::~VmcReceiver()
{
	stop();
}

bool VmcReceiver::start()
{
	if (running_.load())
		return true;

	sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock_ == INVALID_SOCKET) {
		obs_log(LOG_WARNING, "[%s] socket() failed, WSA=%d", VMCTAG, WSAGetLastError());
		return false;
	}

	// WsServerと同様、ゾンビソケットとのポート奪い合いを避けるためSO_EXCLUSIVEADDRUSEを使う。
	// 失敗しても致命的ではないため続行する。
	int opt = 1;
	if (setsockopt(sock_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		       reinterpret_cast<const char *>(&opt), sizeof(opt)) == SOCKET_ERROR) {
		obs_log(LOG_WARNING, "[%s] setsockopt(SO_EXCLUSIVEADDRUSE) failed WSA=%d — 続行します",
			VMCTAG, WSAGetLastError());
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	// INADDR_ANY(0.0.0.0)でバインドする。送信元がループバック(127.0.0.1)経由でも
	// ローカルNIC経由でも受信できるようにするための仕様（2026-08-24変更、詳細はai_logs参照）。
	// UDPは受信専用（応答を返さない）ため、外部から本ポートへパケットを送りつけられる
	// リスクはあるが、データを外部へ漏らす経路にはならない。
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port_);

	if (bind(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		const int wsaErr = WSAGetLastError();
		obs_log(LOG_ERROR,
			"[%s] bind() FAILED: UDP port %u が使用できません (WSA=%d)。"
			"VMC受信は無効のまま起動を続行します。",
			VMCTAG, port_, wsaErr);
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
		return false;
	}

	running_.store(true);
	thread_ = std::thread(&VmcReceiver::recvLoop, this);
	obs_log(LOG_INFO, "[%s] Listening on UDP port %u (INADDR_ANY)", VMCTAG, port_);
	return true;
}

void VmcReceiver::stop()
{
	if (!running_.exchange(false))
		return;

	if (sock_ != INVALID_SOCKET) {
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
	}
	if (thread_.joinable())
		thread_.join();

	obs_log(LOG_INFO, "[%s] Stopped.", VMCTAG);
}

void VmcReceiver::setUpdateCallback(UpdateCallback cb)
{
	std::lock_guard<std::mutex> lock(callbackMutex_);
	updateCallback_ = std::move(cb);
}

void VmcReceiver::recvLoop()
{
	obs_log(LOG_INFO, "[%s] recvLoop() started", VMCTAG);

	// UDPデータグラムの理論上限（65507バイト）に余裕を持たせて確保
	static const size_t kBufSize = 65536;
	std::vector<uint8_t> buf(kBufSize);

	while (running_.load()) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock_, &fds);
		timeval tv{1, 0}; // 1秒タイムアウト（stop()での終了検知用）
		const int sel = select(0, &fds, nullptr, nullptr, &tv);
		if (sel <= 0)
			continue;

		sockaddr_in from{};
		int fromLen = sizeof(from);
		const int n = recvfrom(sock_, reinterpret_cast<char *>(buf.data()),
					static_cast<int>(buf.size()), 0,
					reinterpret_cast<sockaddr *>(&from), &fromLen);
		if (n <= 0)
			continue;

		// 要件: パケット破棄やポート競合でOBS全体をクラッシュさせない（例外安全）
		bool parseOk = false;
		try {
			parseOk = handlePacket(buf.data(), static_cast<size_t>(n));
		} catch (const std::exception &e) {
			parseOk = false;
			obs_log(LOG_WARNING, "[%s] packet parse threw: %s — 無視します", VMCTAG, e.what());
		} catch (...) {
			parseOk = false;
			obs_log(LOG_WARNING, "[%s] packet parse threw unknown exception — 無視します", VMCTAG);
		}
		if (!parseOk)
			logParseErrorRateLimited("OSCパースに失敗したパケットを受信しました（形式不正または未対応）");

		maybeFlush(static_cast<size_t>(n));
	}

	obs_log(LOG_INFO, "[%s] recvLoop() exiting", VMCTAG);
}

bool VmcReceiver::handlePacket(const uint8_t *data, size_t len)
{
	return parseOscPacket(data, len);
}

void VmcReceiver::logParseErrorRateLimited(const char *reason)
{
	// recvLoop()のスレッドからのみ呼ばれるため排他制御は不要
	const uint64_t now = GetTickCount64();
	if (lastErrorLogMs_ != 0 && now - lastErrorLogMs_ < kDiagLogIntervalMs)
		return;
	lastErrorLogMs_ = now;
	obs_log(LOG_WARNING, "[%s] OSC parse error: %s", VMCTAG, reason);
}

bool VmcReceiver::parseOscPacket(const uint8_t *data, size_t len)
{
	if (len < 4)
		return false;
	if (len >= 16 && std::memcmp(data, "#bundle", 7) == 0)
		return parseOscBundle(data, len);
	if (data[0] == '/')
		return parseOscMessage(data, len);
	// アドレスが'/'で始まらず#bundleでもない = OSCとして不正な形式
	return false;
}

bool VmcReceiver::parseOscBundle(const uint8_t *data, size_t len)
{
	// "#bundle\0"（8バイト）+ タイムスタンプ（8バイト）の後、
	// [4バイト長][要素データ] の繰り返し。要素は個別メッセージまたは入れ子のバンドル。
	// WMC（Webcam Motion Capture）等はこの形式で複数ボーン更新を1パケットにまとめて送ってくる。
	size_t offset = 16;
	bool allOk = true;
	while (offset + 4 <= len) {
		int32_t elemLen = 0;
		if (!oscReadInt32(data, len, offset, elemLen)) {
			allOk = false;
			break;
		}
		if (elemLen < 0 || static_cast<size_t>(elemLen) > len - offset) {
			allOk = false; // 壊れた長さフィールド。これ以上パースを続けない
			break;
		}
		if (!parseOscPacket(data + offset, static_cast<size_t>(elemLen)))
			allOk = false; // この要素は失敗したが、後続の要素は引き続き試す
		offset += static_cast<size_t>(elemLen);
	}
	return allOk;
}

bool VmcReceiver::parseOscMessage(const uint8_t *data, size_t len)
{
	size_t offset = 0;
	std::string address;
	if (!oscReadString(data, len, offset, address))
		return false;

	std::string typeTags;
	if (!oscReadString(data, len, offset, typeTags))
		return false;
	if (typeTags.empty() || typeTags[0] != ',')
		return false;

	std::vector<OscArg> args;
	args.reserve(typeTags.size() - 1);
	for (size_t i = 1; i < typeTags.size(); ++i) {
		const char t = typeTags[i];
		if (t == 'f') {
			float v = 0;
			if (!oscReadFloat32(data, len, offset, v))
				return false;
			args.emplace_back(v);
		} else if (t == 'i') {
			int32_t v = 0;
			if (!oscReadInt32(data, len, offset, v))
				return false;
			args.emplace_back(v);
		} else if (t == 's') {
			std::string v;
			if (!oscReadString(data, len, offset, v))
				return false;
			args.emplace_back(std::move(v));
		} else if (t == 'T' || t == 'F') {
			args.emplace_back(t == 'T');
		} else {
			// blob等、VMCで使われない型タグ。安全に諦める（クラッシュしない）
			return false;
		}
	}

	dispatchOscMessage(address, args);
	return true;
}

static bool getArgFloatImpl(const std::variant<float, int32_t, std::string, bool> &arg, float &out)
{
	if (auto f = std::get_if<float>(&arg)) {
		out = *f;
		return true;
	}
	if (auto i = std::get_if<int32_t>(&arg)) {
		out = static_cast<float>(*i);
		return true;
	}
	return false;
}

void VmcReceiver::dispatchOscMessage(const std::string &address, const std::vector<OscArg> &args)
{
	if (address == "/VMC/Ext/Bone/Pos" || address == "/VMC/Ext/Root/Pos") {
		// (string boneName, float px,py,pz, float qx,qy,qz,qw) の8引数（型タグ "sfffffff"）を期待する
		if (args.size() < 8)
			return;
		const std::string *name = std::get_if<std::string>(&args[0]);
		if (!name || name->empty())
			return;

		BoneSample sample;
		if (!getArgFloatImpl(args[1], sample.px) || !getArgFloatImpl(args[2], sample.py) ||
		    !getArgFloatImpl(args[3], sample.pz) || !getArgFloatImpl(args[4], sample.qx) ||
		    !getArgFloatImpl(args[5], sample.qy) || !getArgFloatImpl(args[6], sample.qz) ||
		    !getArgFloatImpl(args[7], sample.qw))
			return;

		std::lock_guard<std::mutex> lock(stateMutex_);
		if (address == "/VMC/Ext/Root/Pos") {
			root_ = sample;
			hasRoot_ = true;
		} else {
			bones_[*name] = sample;
		}
		dirty_ = true;
	} else if (address == "/VMC/Ext/Blend/Val") {
		// (string blendShapeName, float value)
		if (args.size() < 2)
			return;
		const std::string *name = std::get_if<std::string>(&args[0]);
		if (!name || name->empty())
			return;
		float value = 0;
		if (!getArgFloatImpl(args[1], value))
			return;

		std::lock_guard<std::mutex> lock(stateMutex_);
		blendShapes_[*name] = value;
		dirty_ = true;
	}
	// /VMC/Ext/Blend/Apply 等その他のアドレスはスナップショット方式のため無視してよい
	// （Blend/Valが届いた時点でblendShapes_へ反映済みであり、Applyの発火を待つ必要がない）
}

void VmcReceiver::maybeFlush(size_t lastPacketBytes)
{
	std::string json;
	size_t boneCount = 0, morphCount = 0;
	bool doFlush = false;
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		boneCount = bones_.size();
		morphCount = blendShapes_.size();
		if (dirty_) {
			const uint64_t now = GetTickCount64();
			if (lastFlushMs_ == 0 || now - lastFlushMs_ >= static_cast<uint64_t>(kFlushIntervalMs)) {
				lastFlushMs_ = now;
				dirty_ = false;
				json = buildSnapshotJsonLocked();
				doFlush = true;
			}
		}
	}

	// RAW受信ログ（1秒に1回まで）。フラッシュの有無に関わらず、パケットを受信した事実自体を記録する。
	const uint64_t nowMs = GetTickCount64();
	if (lastRawLogMs_ == 0 || nowMs - lastRawLogMs_ >= kDiagLogIntervalMs) {
		lastRawLogMs_ = nowMs;
		obs_log(LOG_INFO, "[%s] RAW UDP recv: %zu bytes | parsed: %zu bones, %zu morphs", VMCTAG,
			lastPacketBytes, boneCount, morphCount);
	}

	if (!doFlush)
		return;

	UpdateCallback cb;
	{
		std::lock_guard<std::mutex> lock(callbackMutex_);
		cb = updateCallback_;
	}
	if (cb)
		cb(json, lastPacketBytes, boneCount, morphCount);
}

std::string VmcReceiver::boneSampleJson(const BoneSample &b)
{
	return "{\"px\":" + fixedFloat(b.px) + ",\"py\":" + fixedFloat(b.py) + ",\"pz\":" +
	       fixedFloat(b.pz) + ",\"qx\":" + fixedFloat(b.qx) + ",\"qy\":" + fixedFloat(b.qy) +
	       ",\"qz\":" + fixedFloat(b.qz) + ",\"qw\":" + fixedFloat(b.qw) + "}";
}

std::string VmcReceiver::buildSnapshotJsonLocked() const
{
	std::string json = "{\"type\":\"vrm_vmc_update\",\"bones\":{";
	bool first = true;
	for (const auto &kv : bones_) {
		if (!first)
			json += ",";
		first = false;
		json += "\"" + jsonEscape(kv.first) + "\":" + boneSampleJson(kv.second);
	}
	json += "},\"blendShapes\":{";
	first = true;
	for (const auto &kv : blendShapes_) {
		if (!first)
			json += ",";
		first = false;
		json += "\"" + jsonEscape(kv.first) + "\":" + fixedFloat(kv.second);
	}
	json += "}";
	if (hasRoot_)
		json += ",\"root\":" + boneSampleJson(root_);
	json += "}";
	return json;
}

#endif // _WIN32
