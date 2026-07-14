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

#include "SupporterLedger.hpp"
#include "core/PluginConfig.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "SupporterLedger";

// ── 固定為替レートテーブル（概算・リアルタイム取得なし） ─────────────────────
// 1 単位の通貨 = 何 USD か。未知通貨は 1:1 とみなす。
static double currencyToUsdRate(const QString &currency)
{
	static const std::unordered_map<std::string, double> kRates = {
		{"USD", 1.0},     {"JPY", 0.0065},  {"EUR", 1.08},   {"GBP", 1.27},
		{"KRW", 0.00073}, {"TWD", 0.031},   {"CAD", 0.73},   {"AUD", 0.65},
		{"MXN", 0.058},   {"BRL", 0.20},    {"INR", 0.012},  {"HKD", 0.128},
		{"SGD", 0.74},    {"SEK", 0.095},   {"NOK", 0.093},  {"DKK", 0.144},
		{"PLN", 0.25},    {"CZK", 0.043},   {"HUF", 0.0027}, {"RON", 0.216},
		{"ILS", 0.27},    {"ARS", 0.001},   {"CLP", 0.0011}, {"COP", 0.00025},
		{"PEN", 0.27},    {"PHP", 0.017},   {"MYR", 0.22},   {"THB", 0.028},
		{"IDR", 0.000062},{"VND", 0.000039},{"ZAR", 0.055},  {"TRY", 0.031},
		{"SAR", 0.267},   {"AED", 0.272},   {"PKR", 0.0036}, {"BGN", 0.55},
		{"NZD", 0.60},    {"CHF", 1.12},    {"RUB", 0.011},  {"UAH", 0.024},
	};
	const auto it = kRates.find(currency.toStdString());
	return (it != kRates.end()) ? it->second : 1.0;
}

// ── 静的ユーティリティ ────────────────────────────────────────────────────────

double SupporterLedger::toUsd(int64_t amountMicros, const QString &currency)
{
	// amountMicros は通貨単位の 1/1,000,000
	const double amount = static_cast<double>(amountMicros) / 1'000'000.0;
	return amount * currencyToUsdRate(currency);
}

QString SupporterLedger::makeUserKey(const QString &platform, const QString &userId)
{
	return platform + ":" + userId;
}

QString SupporterLedger::eventTypeToString(SupportEventType t)
{
	switch (t) {
	case SupportEventType::SuperChat:       return "スーパーチャット";
	case SupportEventType::SuperSticker:    return "スーパーステッカー";
	case SupportEventType::MembershipJoin:  return "メンバーシップ加入";
	case SupportEventType::MemberMilestone: return "メンバーシップ継続";
	case SupportEventType::GiftMembership:  return "ギフトメンバーシップ";
	case SupportEventType::TwitchBits:      return "Bits（チア）";
	case SupportEventType::TwitchSub:       return "サブスクリプション";
	case SupportEventType::TwitchResub:     return "サブスク継続";
	case SupportEventType::TwitchSubGift:   return "サブギフト";
	}
	return "不明";
}

// ── シングルトン ──────────────────────────────────────────────────────────────

SupporterLedger &SupporterLedger::instance()
{
	static SupporterLedger inst;
	return inst;
}

SupporterLedger::SupporterLedger(QObject *parent) : QObject(parent)
{
	load();
}

// ── イベント追加 ──────────────────────────────────────────────────────────────

void SupporterLedger::addEvent(const QString &userId, const QString &displayName,
                                const QString &platform, const SupporterEvent &event)
{
	const std::string key = makeUserKey(platform, userId).toStdString();
	auto &entry = users_[key];
	entry.displayName = displayName;
	entry.platform = platform;
	entry.totalUsdAmount += event.usdAmount;
	entry.events.push_back(event);

	obs_log(LOG_INFO, "[%s] event added: user=%s platform=%s type=%s usdAmount=%.4f",
	        TAG, displayName.toUtf8().constData(), platform.toUtf8().constData(),
	        eventTypeToString(event.type).toUtf8().constData(), event.usdAmount);

	save();
	emit ledgerUpdated();
}

// ── ソート済みユーザー一覧 ────────────────────────────────────────────────────

QList<QPair<QString, SupporterLedger::UserEntry>> SupporterLedger::sortedUsers() const
{
	QList<QPair<QString, UserEntry>> result;
	result.reserve(static_cast<int>(users_.size()));
	for (const auto &[k, v] : users_)
		result.append({QString::fromStdString(k), v});

	std::sort(result.begin(), result.end(),
	          [](const QPair<QString, UserEntry> &a, const QPair<QString, UserEntry> &b) {
		          return a.second.totalUsdAmount > b.second.totalUsdAmount;
	          });
	return result;
}

const SupporterLedger::UserEntry *SupporterLedger::findUser(const QString &userKey) const
{
	const auto it = users_.find(userKey.toStdString());
	return (it != users_.end()) ? &it->second : nullptr;
}

// ── ファイルパス ──────────────────────────────────────────────────────────────

QString SupporterLedger::dataFilePath() const
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return {};
	return QString::fromLocal8Bit(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\supporter_ledger.dat";
#else
	return {};
#endif
}

// ── DPAPI 暗号化（Windows ユーザーアカウント固有）────────────────────────────

#ifdef _WIN32
bool SupporterLedger::encryptAndWrite(const std::string &plaintext, const QString &filePath)
{
	DATA_BLOB inBlob;
	inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.data()));
	inBlob.cbData = static_cast<DWORD>(plaintext.size());

	DATA_BLOB outBlob = {};
	if (!CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
	                      CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
		obs_log(LOG_WARNING, "[%s] CryptProtectData failed (err=%lu)", TAG, GetLastError());
		return false;
	}

	QDir().mkpath(QFileInfo(filePath).absolutePath());
	QFile f(filePath);
	if (!f.open(QIODevice::WriteOnly)) {
		LocalFree(outBlob.pbData);
		obs_log(LOG_WARNING, "[%s] cannot open for writing: %s", TAG,
		        filePath.toUtf8().constData());
		return false;
	}
	f.write(reinterpret_cast<const char *>(outBlob.pbData),
	        static_cast<qint64>(outBlob.cbData));
	f.close();

	LocalFree(outBlob.pbData);
	obs_log(LOG_INFO, "[%s] saved (DPAPI-encrypted, %lu bytes)", TAG, outBlob.cbData);
	return true;
}

bool SupporterLedger::readAndDecrypt(const QString &filePath, std::string &out)
{
	QFile f(filePath);
	if (!f.open(QIODevice::ReadOnly)) {
		obs_log(LOG_INFO, "[%s] no existing data file: %s", TAG,
		        filePath.toUtf8().constData());
		return false;
	}
	const QByteArray encrypted = f.readAll();
	f.close();

	DATA_BLOB inBlob;
	inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.data()));
	inBlob.cbData = static_cast<DWORD>(encrypted.size());

	DATA_BLOB outBlob = {};
	if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
	                        CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
		obs_log(LOG_WARNING, "[%s] CryptUnprotectData failed (err=%lu)", TAG, GetLastError());
		return false;
	}

	out.assign(reinterpret_cast<char *>(outBlob.pbData), outBlob.cbData);
	LocalFree(outBlob.pbData);
	obs_log(LOG_INFO, "[%s] loaded and decrypted (%zu bytes)", TAG, out.size());
	return true;
}
#endif

// ── 保存（JSON → DPAPI 暗号化 → ファイル書き込み）────────────────────────────

void SupporterLedger::save()
{
	const QString path = dataFilePath();
	if (path.isEmpty())
		return;

	QJsonArray usersArr;
	for (const auto &[k, entry] : users_) {
		QJsonObject uObj;
		uObj["key"]         = QString::fromStdString(k);
		uObj["displayName"] = entry.displayName;
		uObj["platform"]    = entry.platform;
		uObj["totalUsd"]    = entry.totalUsdAmount;

		QJsonArray evArr;
		for (const auto &ev : entry.events) {
			QJsonObject evObj;
			evObj["ts"]       = ev.timestamp.toMSecsSinceEpoch();
			evObj["platform"] = ev.platform;
			evObj["type"]     = static_cast<int>(ev.type);
			evObj["micros"]   = QString::number(ev.amountMicros); // 精度保持のため文字列
			evObj["currency"] = ev.currency;
			evObj["usd"]      = ev.usdAmount;
			evObj["note"]     = ev.note;
			evObj["message"]  = ev.message;
			evObj["planName"] = ev.planName;
			evArr.append(evObj);
		}
		uObj["events"] = evArr;
		usersArr.append(uObj);
	}

	QJsonObject root;
	root["users"] = usersArr;
	const std::string json =
		QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();

#ifdef _WIN32
	encryptAndWrite(json, path);
#else
	Q_UNUSED(json);
#endif
}

// ── 読み込み（ファイル → DPAPI 復号 → JSON デシリアライズ）──────────────────

void SupporterLedger::load()
{
	users_.clear();
	loaded_ = false;

	const QString path = dataFilePath();
	if (path.isEmpty()) {
		loaded_ = true;
		return;
	}

	std::string json;
#ifdef _WIN32
	if (!readAndDecrypt(path, json)) {
		loaded_ = true; // ファイルなし = 初回起動
		return;
	}
#else
	loaded_ = true;
	return;
#endif

	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
	if (!doc.isObject()) {
		obs_log(LOG_WARNING, "[%s] JSON parse error", TAG);
		loaded_ = true;
		return;
	}

	for (const QJsonValue &uVal : doc.object()["users"].toArray()) {
		const QJsonObject uObj = uVal.toObject();
		const std::string key  = uObj["key"].toString().toStdString();
		if (key.empty())
			continue;

		UserEntry &entry     = users_[key];
		entry.displayName    = uObj["displayName"].toString();
		entry.platform       = uObj["platform"].toString();
		entry.totalUsdAmount = uObj["totalUsd"].toDouble();

		for (const QJsonValue &evVal : uObj["events"].toArray()) {
			const QJsonObject evObj = evVal.toObject();
			SupporterEvent ev;
			ev.timestamp    = QDateTime::fromMSecsSinceEpoch(
				evObj["ts"].toVariant().toLongLong());
			ev.platform     = evObj["platform"].toString();
			ev.type         = static_cast<SupportEventType>(evObj["type"].toInt());
			ev.amountMicros = evObj["micros"].toString().toLongLong();
			ev.currency     = evObj["currency"].toString();
			ev.usdAmount    = evObj["usd"].toDouble();
			ev.note         = evObj["note"].toString();
			ev.message      = evObj["message"].toString();
			ev.planName     = evObj["planName"].toString(); // 旧データは空文字
			entry.events.push_back(ev);
		}
	}

	obs_log(LOG_INFO, "[%s] loaded %zu users", TAG, users_.size());
	loaded_ = true;
}

// ── メンバーシップ価格解決（表示・集計時に呼び出す） ─────────────────────────

double SupporterLedger::resolveMembershipUsd(const SupporterEvent &ev,
                                              const QMap<QString, double> &planUsdMap)
{
	if (ev.planName.isEmpty())
		return 0.0;
	const auto it = planUsdMap.find(ev.planName);
	return (it != planUsdMap.end()) ? it.value() : 0.0;
}

double SupporterLedger::computeTotalUsd(const UserEntry &entry,
                                         const QMap<QString, double> &planUsdMap)
{
	double total = entry.totalUsdAmount; // 非メンバーシップ分（SC・Bits・Sub等）
	for (const auto &ev : entry.events) {
		switch (ev.type) {
		case SupportEventType::MembershipJoin:
		case SupportEventType::MemberMilestone:
		case SupportEventType::GiftMembership:
			total += resolveMembershipUsd(ev, planUsdMap);
			break;
		default:
			break;
		}
	}
	return total;
}

// static
QMap<QString, double> SupporterLedger::buildPlanUsdMapFromConfig()
{
	QMap<QString, double> map;
	for (const auto &p : PluginConfig::instance().membershipPlanPrices) {
		if (p.planName.empty() || p.amount <= 0.0)
			continue;
		const double usd = toUsd(static_cast<int64_t>(p.amount * 1'000'000.0),
		                          QString::fromStdString(p.currency));
		if (usd > 0.0)
			map[QString::fromStdString(p.planName)] = usd;
	}
	return map;
}

// ── テストデータ注入（暗号化・復号・表示テスト用） ────────────────────────────

void SupporterLedger::injectTestData()
{
	const QDateTime now = QDateTime::currentDateTime();

	// YouTube スーパーチャット（¥500 JPY → ~$3.25）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-600);
		ev.platform     = "YouTube";
		ev.type         = SupportEventType::SuperChat;
		ev.amountMicros = 500'000'000LL;  // ¥500
		ev.currency     = "JPY";
		ev.usdAmount    = toUsd(500'000'000LL, "JPY");
		ev.note         = "Tier 3";
		ev.message      = "テストスーパーチャットです！応援してます！";
		addEvent("UC_test_yt_1", "テストユーザー1（YouTube）", "YouTube", ev);
	}

	// YouTube スーパーステッカー（¥200 JPY）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-500);
		ev.platform     = "YouTube";
		ev.type         = SupportEventType::SuperSticker;
		ev.amountMicros = 200'000'000LL;  // ¥200
		ev.currency     = "JPY";
		ev.usdAmount    = toUsd(200'000'000LL, "JPY");
		ev.note         = "Tier 1";
		ev.message      = {};
		addEvent("UC_test_yt_1", "テストユーザー1（YouTube）", "YouTube", ev);
	}

	// YouTube メンバーシップ加入（金額なし → planName でマッピング解決）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-400);
		ev.platform     = "YouTube";
		ev.type         = SupportEventType::MembershipJoin;
		ev.amountMicros = 0;
		ev.currency     = {};
		ev.usdAmount    = 0.0;
		ev.note         = "テストメンバーレベル";
		ev.planName     = "テストメンバーレベル";
		ev.message      = {};
		addEvent("UC_test_yt_2", "テストユーザー2（YouTube）", "YouTube", ev);
	}

	// YouTube メンバーシップ継続6ヶ月（金額なし → planName でマッピング解決）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-300);
		ev.platform     = "YouTube";
		ev.type         = SupportEventType::MemberMilestone;
		ev.amountMicros = 0;
		ev.currency     = {};
		ev.usdAmount    = 0.0;
		ev.note         = "継続6ヶ月 / テストメンバーレベル";
		ev.planName     = "テストメンバーレベル";
		ev.message      = "半年ありがとうございます！";
		addEvent("UC_test_yt_3", "テストユーザー3（YouTube）", "YouTube", ev);
	}

	// Twitch Bits 1000bits（= $10.00）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-200);
		ev.platform     = "Twitch";
		ev.type         = SupportEventType::TwitchBits;
		ev.amountMicros = 1000LL * 10'000LL;  // 1000 bits × $0.01 → micros
		ev.currency     = "USD";
		ev.usdAmount    = 10.0;
		ev.note         = "1000 bits";
		ev.message      = "Cheer1000 テストチアです！";
		addEvent("twitch_test_1", "TwitchTestUser1", "Twitch", ev);
	}

	// Twitch サブスクリプション Tier 1（$4.99）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-100);
		ev.platform     = "Twitch";
		ev.type         = SupportEventType::TwitchSub;
		ev.amountMicros = 4'990'000LL;  // $4.99
		ev.currency     = "USD";
		ev.usdAmount    = 4.99;
		ev.note         = "Tier 1";
		ev.message      = {};
		addEvent("twitch_test_2", "TwitchTestUser2", "Twitch", ev);
	}

	// Twitch サブスクリプション継続（Tier 2, $9.99）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-50);
		ev.platform     = "Twitch";
		ev.type         = SupportEventType::TwitchResub;
		ev.amountMicros = 9'990'000LL;  // $9.99
		ev.currency     = "USD";
		ev.usdAmount    = 9.99;
		ev.note         = "Tier 2 / 3ヶ月継続";
		ev.message      = "継続テストです！";
		addEvent("twitch_test_1", "TwitchTestUser1", "Twitch", ev);
	}

	// YouTube スーパーチャット（$10 USD）
	{
		SupporterEvent ev;
		ev.timestamp    = now.addSecs(-10);
		ev.platform     = "YouTube";
		ev.type         = SupportEventType::SuperChat;
		ev.amountMicros = 10'000'000LL;  // $10
		ev.currency     = "USD";
		ev.usdAmount    = toUsd(10'000'000LL, "USD");
		ev.note         = "Tier 5";
		ev.message      = "USD テストチャット！";
		addEvent("UC_test_yt_4", "テストユーザー4（YouTube・$）", "YouTube", ev);
	}

	obs_log(LOG_INFO, "[%s] test data injected (%zu total users)", TAG, users_.size());
}
