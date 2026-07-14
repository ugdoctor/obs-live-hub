#pragma once

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <unordered_map>
#include <vector>

enum class SupportEventType {
	SuperChat,
	SuperSticker,
	MembershipJoin,      // YouTube newSponsorEvent — 金額なし
	MemberMilestone,     // YouTube memberMilestoneChatEvent — 金額なし
	GiftMembership,      // YouTube giftMembershipReceivedEvent — 金額なし
	TwitchBits,
	TwitchSub,
	TwitchResub,
	TwitchSubGift,
};

struct SupporterEvent {
	QDateTime timestamp;
	QString platform;
	SupportEventType type;
	// 0 = 金額情報なし（メンバーシップ系）。micros = 通貨単位の 1/1,000,000。
	// Bits は bits数 × 10,000 micros（USD 換算、100bits=$1）。
	int64_t amountMicros = 0;
	QString currency;           // 空 = 金額情報なし
	double usdAmount = 0.0;     // 0.0 = 金額情報なし
	QString note;               // tier・メンバーレベル・bits数など補足
	QString message;
	QString planName;           // メンバーシップイベントのプラン名（例:「ファン」）。非メンバーシップは空。
};

// platform + userId をキーとする視聴者ごとの課金履歴台帳。
// Windows DPAPI（CryptProtectData/CryptUnprotectData）でファイルを暗号化保存。
// 復号は実行ユーザーアカウントに紐づくため、別 PC・別アカウントでは復号不可。
// 配信者の個人情報（課金金額・ユーザー名）を含む機密データ。
class SupporterLedger : public QObject {
	Q_OBJECT
public:
	struct UserEntry {
		QString displayName;
		QString platform;
		double totalUsdAmount = 0.0; // 金額あるイベントのみ合算
		std::vector<SupporterEvent> events;
	};

	static SupporterLedger &instance();

	void addEvent(const QString &userId, const QString &displayName,
	              const QString &platform, const SupporterEvent &event);

	// 累計USD金額の降順でソートしたユーザー一覧を返す
	QList<QPair<QString, UserEntry>> sortedUsers() const;
	const UserEntry *findUser(const QString &userKey) const;

	void save();
	void load();
	bool isLoaded() const { return loaded_; }

	// デバッグ：ダミーデータを注入して暗号化・復号・表示テストに使用
	void injectTestData();

	// amountMicros（通貨単位の百万分の一）を USD に変換（概算固定レート）
	static double toUsd(int64_t amountMicros, const QString &currency);
	// "platform:userId" 形式のキーを生成
	static QString makeUserKey(const QString &platform, const QString &userId);
	static QString eventTypeToString(SupportEventType t);

	// planUsdMap: planName → USD金額（呼び出し側が PluginConfig から変換して渡す）。
	// メンバーシップイベントに対してプラン価格マッピングを適用し USD を返す。
	// マッピングなし・planName 空の場合は 0.0 を返す。
	static double resolveMembershipUsd(const SupporterEvent &ev,
	                                   const QMap<QString, double> &planUsdMap);
	// ユーザーの累計USD（totalUsdAmount = 非メンバーシップ分）+
	// メンバーシップイベントへのプラン価格適用分 を合算した表示用合計を返す。
	static double computeTotalUsd(const UserEntry &entry,
	                              const QMap<QString, double> &planUsdMap);

	// PluginConfig::membershipPlanPrices から planName → USD 換算済み価格のマップを構築する
	static QMap<QString, double> buildPlanUsdMapFromConfig();

signals:
	void ledgerUpdated();

private:
	explicit SupporterLedger(QObject *parent = nullptr);

	QString dataFilePath() const;

#ifdef _WIN32
	static bool encryptAndWrite(const std::string &plaintext, const QString &filePath);
	static bool readAndDecrypt(const QString &filePath, std::string &out);
#endif

	std::unordered_map<std::string, UserEntry> users_;
	bool loaded_ = false;
};
