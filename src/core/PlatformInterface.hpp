#pragma once
#include <string>

// コメント受信イベント。全プラットフォーム共通のデータ型。
struct CommentEvent {
	std::string platformName;
	std::string authorName;
	std::string message;
	std::string timestamp;
	std::string avatarUrl;
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
