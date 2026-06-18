#pragma once
#include <QObject>
#include <QString>
#include <queue>
#include <string>
#include <vector>

struct EffectDef {
	std::string name;
	std::string triggerType;   // "emoji" or "olh_command"
	std::string triggerValue;  // emoji文字列 or "effect:xxx"
	std::string file;          // 画像ファイル名（effects/ 相対）
	int duration = 3000;       // 表示時間 ms
	std::string position = "center";
	bool positionOverride = false;
	std::string size = "medium";
	bool sizeOverride = false;
};

// エフェクトの読み込み・トリガー・同時実行/キュー管理。
// broadcastEffect(json) シグナルで WsServer へ送信データを通知する。
class EffectManager : public QObject {
	Q_OBJECT
public:
	explicit EffectManager(QObject *parent = nullptr);

	void loadEffects(const QString &effectsDir);
	const std::vector<EffectDef> &effects() const { return effects_; }
	QString effectsDir() const { return effectsDir_; }

	// コメントテキストから絵文字トリガーを検出して発火
	void onComment(const QString &text, const QString &user = {});
	// [olh] effect:xxx の xxx 部分を渡してトリガー判定
	void onOlhEffect(const QString &effectValue, const QString &user = {});

signals:
	void broadcastEffect(const std::string &json);
	void broadcastDebugStatus(const std::string &json);
	void broadcastDebugLog(const std::string &json);

private:
	void triggerEffect(const EffectDef &effect, const QString &user);
	void onEffectFinished();
	std::string makeEffectJson(const EffectDef &effect) const;
	std::string makeDebugStatusJson() const;
	std::string makeDebugLogJson(const EffectDef &effect, const QString &user,
	                             const std::string &status) const;

	std::vector<EffectDef> effects_;
	QString effectsDir_;
	int activeCount_ = 0;
	std::queue<EffectDef> pendingQueue_;
};
