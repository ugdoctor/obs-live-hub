#pragma once
#include <QString>

// 棒読みちゃん HTTP連携クライアント
// GET /Talk?text=...&voice=...&volume=...&speed=...&tone=... を非同期で送信する
// 通信はバックグラウンドスレッドで行い、レスポンスは破棄する（棒読みちゃんが自前で再生）
class BouyomiChanClient {
public:
	// -1 は「棒読みちゃんの現在設定をそのまま使う」を意味する
	static void talk(const QString &host, int port, const QString &text,
	                 int voice = 0, int volume = -1, int speed = -1, int tone = -1);
};
