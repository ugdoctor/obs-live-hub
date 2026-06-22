#pragma once
#include <QString>

// AssistantSeika HTTP API クライアント（VOICEROID/CeVIO/A.I.VOICE 等の連携）
// POST /PLAY2/<cid> に JSON ボディを送り、AssistantSeika 経由で音声再生させる
// 通信はバックグラウンドスレッドで行い、AssistantSeika 側が再生を管理する
class VoiceroidClient {
public:
	// host, port: AssistantSeika の接続先（デフォルト: localhost:7180）
	// cid: 話者番号（AssistantSeika の「設定2」タブで確認可能）
	// text: 読み上げテキスト
	// username, password: AssistantSeika の BASIC 認証情報
	static void talk(const QString &host, int port, int cid,
	                 const QString &text,
	                 const QString &username, const QString &password);
};
