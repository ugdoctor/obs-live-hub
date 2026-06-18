#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <string>
#include <unordered_map>
#include <vector>

#include <QtGlobal> // qint64

struct PointActionDef {
	std::string name;
	std::string command;
	int cost = 0;
	std::string actionType;
	std::string actionValue;
};

// ユーザーポイントの管理・CSVへの永続化・アクション定義の読み込み・実行。
// ブロードキャストや副作用はシグナルで通知し、plugin-main.cpp が接続する。
class PointManager : public QObject {
	Q_OBJECT
public:
	struct UserPoints {
		QString userId;
		QString platform;
		int points = 0;
	};

	explicit PointManager(QObject *parent = nullptr);
	~PointManager() override;

	void loadActions(const QString &actionsDir);
	const std::vector<PointActionDef> &actions() const { return actions_; }
	QString actionsDir() const { return actionsDir_; }

	// コメント受信時にコメントポイントを付与
	void onComment(const QString &userId, const QString &platform);

	// [olh] point_use:command
	void onPointUse(const QString &userId, const QString &platform,
	                const QString &displayName, const QString &command);

	// [olh] point_check
	void onPointCheck(const QString &userId, const QString &platform,
	                  const QString &displayName);

	int getPoints(const QString &userId, const QString &platform) const;
	std::vector<UserPoints> allUserPoints() const;
	void setPoints(const QString &userId, const QString &platform, int points);
	void adjustPoints(const QString &userId, const QString &platform, int delta);

	// 設定変更後に呼び出すと視聴タイマーを再設定する
	void reloadSettings();

	// CSVの即時保存（チェックサムも再計算して保存）
	void saveNow();

	// 改ざん検知フラグ（loadCsv後に参照する）
	bool hasTamperWarning() const { return tamperDetected_; }

	// 全ユーザーのポイントを0にリセットしてCSVを保存
	void resetAllPoints();

signals:
	void broadcastJson(const std::string &json);
	void systemCommentRequested(const std::string &text);
	void triggerEffectRequested(const QString &effectName);
	void setModelRequested(const QString &userId, const QString &modelName);
	void debugPointLog(const std::string &json);
	void debugPointUseLog(const std::string &json);

private slots:
	void onWatchTimer();
	void onSaveTimer();

private:
	static QString makeKey(const QString &userId, const QString &platform);
	void addPoints(const QString &userId, const QString &platform, int amount,
	               const QString &reason = {});
	void scheduleSave();
	void loadCsv();
	void saveChecksumFor(const QByteArray &csvData);
	void executeAction(const PointActionDef &action, const QString &userId);
	std::string makeDebugPointLogJson(const QString &user, int delta,
	                                   const QString &reason, int balance) const;
	std::string makeDebugPointUseLogJson(const QString &user, const QString &command,
	                                      bool ok, int balance,
	                                      const std::string &error = {}) const;

	std::unordered_map<std::string, int>     points_;
	std::unordered_map<std::string, QString> keyToUserId_;
	std::unordered_map<std::string, QString> keyToPlatform_;
	std::vector<PointActionDef> actions_;
	QString actionsDir_;
	QString csvPath_;
	QTimer *watchTimer_  = nullptr;
	QTimer *saveTimer_   = nullptr;
	bool    tamperDetected_ = false;

	// クールダウン管理（msec タイムスタンプ）
	std::unordered_map<std::string, qint64> commentCooldowns_;
	std::unordered_map<std::string, qint64> useCooldowns_;
};
