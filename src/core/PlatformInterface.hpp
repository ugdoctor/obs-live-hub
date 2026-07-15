#pragma once
#include <string>
#include <vector>

// コメント内に出現するスタンプ（エモート）1個分の情報。
// start/end は Twitch IRC の emotes タグ仕様に準拠した UTF-16 コード単位インデックス
// （end は inclusive）。message 文字列（UTF-8 の std::string）へのバイトオフセットでは
// ない点に注意。C++側ではこの整数をそのまま右から左へ運ぶだけで、実際のテキスト分割は
// JavaScript（ネイティブにUTF-16コード単位でstring.slice()が動く）側で行う。
struct CommentEmote {
	std::string id;
	int start = 0;
	int end   = 0;
};

// コメント受信イベント。全プラットフォーム共通のデータ型。
struct CommentEvent {
	std::string platformName;
	std::string authorName;
	std::string message;
	std::string timestamp;
	std::string avatarUrl;
	// Twitchスタンプ（IDと出現位置）。YouTubeは常に空（後述の調査結果を参照）。
	std::vector<CommentEmote> emotes;
};

// 各配信プラットフォームが実装するインターフェース。
// コアやUIはこの抽象クラスのみに依存し、プラットフォーム追加時にコアを変更しない。
class PlatformInterface {
public:
	virtual ~PlatformInterface() = default;

	virtual void connect() = 0;
	virtual void disconnect() = 0;
	virtual std::string getPlatformName() const = 0;
	virtual bool isConnected() const = 0;
};
