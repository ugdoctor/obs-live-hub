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
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 外部ネットワークには公開しない
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
	obs_log(LOG_INFO, "[%s] Listening for VMC/OSC on udp://127.0.0.1:%u", VMCTAG, port_);
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

void VmcReceiver::setUpdateCallback(std::function<void(const std::string &)> cb)
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
		try {
			handlePacket(buf.data(), static_cast<size_t>(n));
		} catch (const std::exception &e) {
			obs_log(LOG_WARNING, "[%s] packet parse threw: %s — 無視します", VMCTAG, e.what());
		} catch (...) {
			obs_log(LOG_WARNING, "[%s] packet parse threw unknown exception — 無視します", VMCTAG);
		}

		maybeFlush();
	}

	obs_log(LOG_INFO, "[%s] recvLoop() exiting", VMCTAG);
}

void VmcReceiver::handlePacket(const uint8_t *data, size_t len)
{
	parseOscPacket(data, len);
}

void VmcReceiver::parseOscPacket(const uint8_t *data, size_t len)
{
	if (len < 4)
		return;
	if (len >= 16 && std::memcmp(data, "#bundle", 7) == 0) {
		parseOscBundle(data, len);
	} else if (data[0] == '/') {
		parseOscMessage(data, len);
	}
	// それ以外（不正な形式・未対応形式）は黙って無視する
}

void VmcReceiver::parseOscBundle(const uint8_t *data, size_t len)
{
	// "#bundle\0"（8バイト）+ タイムスタンプ（8バイト）の後、
	// [4バイト長][要素データ] の繰り返し。要素は個別メッセージまたは入れ子のバンドル。
	size_t offset = 16;
	while (offset + 4 <= len) {
		int32_t elemLen = 0;
		if (!oscReadInt32(data, len, offset, elemLen))
			break;
		if (elemLen < 0 || static_cast<size_t>(elemLen) > len - offset)
			break; // 壊れた長さフィールド。これ以上パースを続けない
		parseOscPacket(data + offset, static_cast<size_t>(elemLen));
		offset += static_cast<size_t>(elemLen);
	}
}

void VmcReceiver::parseOscMessage(const uint8_t *data, size_t len)
{
	size_t offset = 0;
	std::string address;
	if (!oscReadString(data, len, offset, address))
		return;

	std::string typeTags;
	if (!oscReadString(data, len, offset, typeTags))
		return;
	if (typeTags.empty() || typeTags[0] != ',')
		return;

	std::vector<OscArg> args;
	args.reserve(typeTags.size() - 1);
	for (size_t i = 1; i < typeTags.size(); ++i) {
		const char t = typeTags[i];
		if (t == 'f') {
			float v = 0;
			if (!oscReadFloat32(data, len, offset, v))
				return;
			args.emplace_back(v);
		} else if (t == 'i') {
			int32_t v = 0;
			if (!oscReadInt32(data, len, offset, v))
				return;
			args.emplace_back(v);
		} else if (t == 's') {
			std::string v;
			if (!oscReadString(data, len, offset, v))
				return;
			args.emplace_back(std::move(v));
		} else if (t == 'T' || t == 'F') {
			args.emplace_back(t == 'T');
		} else {
			// blob等、VMCで使われない型タグ。安全に諦める（クラッシュしない）
			return;
		}
	}

	dispatchOscMessage(address, args);
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
		// (string boneName, float px,py,pz, float qx,qy,qz,qw) の8引数を期待する
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

void VmcReceiver::maybeFlush()
{
	std::string json;
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (!dirty_)
			return;
		const uint64_t now = GetTickCount64();
		if (lastFlushMs_ != 0 && now - lastFlushMs_ < static_cast<uint64_t>(kFlushIntervalMs))
			return;
		lastFlushMs_ = now;
		dirty_ = false;
		json = buildSnapshotJsonLocked();
	}

	std::function<void(const std::string &)> cb;
	{
		std::lock_guard<std::mutex> lock(callbackMutex_);
		cb = updateCallback_;
	}
	if (cb)
		cb(json);
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
