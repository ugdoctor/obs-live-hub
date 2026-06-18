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

#include "PointManager.hpp"
#include "core/PluginConfig.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QTextStream>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "PointManager";

static std::string escapeJson(const std::string &s)
{
	std::string r;
	r.reserve(s.size() + 8);
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

PointManager::PointManager(QObject *parent) : QObject(parent)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) > 0) {
		csvPath_ = QString::fromLocal8Bit(appdata) +
		           "\\obs-studio\\plugins\\obs-live-hub\\points.csv";
	}
#endif
	loadCsv();

	watchTimer_ = new QTimer(this);
	watchTimer_->setTimerType(Qt::VeryCoarseTimer);
	connect(watchTimer_, &QTimer::timeout, this, &PointManager::onWatchTimer);

	saveTimer_ = new QTimer(this);
	saveTimer_->setSingleShot(true);
	connect(saveTimer_, &QTimer::timeout, this, &PointManager::onSaveTimer);

	reloadSettings();
}

PointManager::~PointManager()
{
	watchTimer_->stop();
	if (saveTimer_->isActive()) {
		saveTimer_->stop();
		saveNow();
	}
}

void PointManager::reloadSettings()
{
	const auto &cfg = PluginConfig::instance();
	watchTimer_->stop();
	if (!cfg.pointEnabled || cfg.pointWatchInterval <= 0 || cfg.pointWatchAmount <= 0)
		return;
	watchTimer_->setInterval(cfg.pointWatchInterval * 60 * 1000);
	watchTimer_->start();
	obs_log(LOG_INFO, "[%s] watch timer started: every %d min, +%d pts", TAG,
	        cfg.pointWatchInterval, cfg.pointWatchAmount);
}

void PointManager::loadActions(const QString &actionsDir)
{
	actionsDir_ = actionsDir;
	actions_.clear();

	QDir dir(actionsDir);
	if (!dir.exists()) {
		obs_log(LOG_INFO, "[%s] actions dir not found: %s", TAG,
		        actionsDir.toUtf8().constData());
		return;
	}

	for (const QString &filename : dir.entryList({"*.json"}, QDir::Files)) {
		QFile f(actionsDir + "/" + filename);
		if (!f.open(QIODevice::ReadOnly))
			continue;
		const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		f.close();
		if (!doc.isObject())
			continue;

		const QJsonObject obj = doc.object();
		PointActionDef def;
		def.name    = obj["name"].toString().toStdString();
		def.command = obj["command"].toString().toStdString();
		def.cost    = obj["cost"].toInt(0);
		if (def.command.empty())
			continue;

		const QJsonObject act = obj["action"].toObject();
		def.actionType  = act["type"].toString().toStdString();
		def.actionValue = act["value"].toString().toStdString();

		actions_.push_back(def);
		obs_log(LOG_INFO, "[%s] loaded action: %s (cost=%d type=%s)", TAG,
		        def.command.c_str(), def.cost, def.actionType.c_str());
	}
	obs_log(LOG_INFO, "[%s] %zu action(s) loaded from %s", TAG, actions_.size(),
	        actionsDir.toUtf8().constData());
}

void PointManager::onComment(const QString &userId, const QString &platform)
{
	const auto &cfg = PluginConfig::instance();
	if (!cfg.pointEnabled || cfg.pointCommentAmount <= 0)
		return;

	if (cfg.pointCommentCooldown > 0) {
		const std::string key = makeKey(userId, platform).toStdString();
		const qint64 now      = QDateTime::currentMSecsSinceEpoch();
		const auto it         = commentCooldowns_.find(key);
		if (it != commentCooldowns_.end() &&
		    now - it->second < static_cast<qint64>(cfg.pointCommentCooldown) * 1000)
			return;
		commentCooldowns_[key] = now;
	}

	addPoints(userId, platform, cfg.pointCommentAmount, QStringLiteral("comment"));
}

void PointManager::onPointUse(const QString &userId, const QString &platform,
                               const QString &displayName, const QString &command)
{
	const auto &cfg = PluginConfig::instance();

	// point_use クールダウンチェック
	if (cfg.pointUseCooldown > 0) {
		const std::string key = makeKey(userId, platform).toStdString();
		const qint64 now      = QDateTime::currentMSecsSinceEpoch();
		const auto it         = useCooldowns_.find(key);
		if (it != useCooldowns_.end() &&
		    now - it->second < static_cast<qint64>(cfg.pointUseCooldown) * 1000) {
			const int bal = getPoints(userId, platform);
			emit systemCommentRequested(displayName.toStdString() +
			                            "さんへ：操作が早すぎます。しばらく待ってから再試行してください。");
			emit debugPointUseLog(makeDebugPointUseLogJson(displayName, command, false, bal,
			                                               "cooldown"));
			return;
		}
		useCooldowns_[key] = now;
	}

	const PointActionDef *found = nullptr;
	for (const auto &a : actions_) {
		if (QString::fromStdString(a.command).compare(command, Qt::CaseInsensitive) == 0) {
			found = &a;
			break;
		}
	}

	auto makeResult = [&](bool ok, int remaining,
	                      const std::string &err = {}) -> std::string {
		std::string j = "{\"type\":\"point_use_result\",\"ok\":" +
		                std::string(ok ? "true" : "false");
		j += ",\"userId\":\"" + escapeJson(userId.toStdString()) + "\"";
		j += ",\"command\":\"" + escapeJson(command.toStdString()) + "\"";
		j += ",\"remainingPoints\":" + std::to_string(remaining);
		if (!err.empty())
			j += ",\"error\":\"" + escapeJson(err) + "\"";
		j += "}";
		return j;
	};

	if (!found) {
		const int bal = getPoints(userId, platform);
		emit systemCommentRequested(displayName.toStdString() +
		                            "さんへ：そのアクションは存在しません");
		emit broadcastJson(makeResult(false, bal, "action_not_found"));
		emit debugPointUseLog(makeDebugPointUseLogJson(displayName, command, false, bal,
		                                               "action_not_found"));
		return;
	}

	const int currentPts = getPoints(userId, platform);
	if (currentPts < found->cost) {
		emit systemCommentRequested(displayName.toStdString() +
		                            "さんへ：ポイントが足りません（現在" +
		                            std::to_string(currentPts) + "、必要" +
		                            std::to_string(found->cost) + "）");
		emit broadcastJson(makeResult(false, currentPts, "insufficient_points"));
		emit debugPointUseLog(makeDebugPointUseLogJson(displayName, command, false,
		                                               currentPts, "insufficient_points"));
		return;
	}

	addPoints(userId, platform, -found->cost, QStringLiteral("point_use"));
	const int remaining = getPoints(userId, platform);
	executeAction(*found, userId);
	emit broadcastJson(makeResult(true, remaining));
	emit debugPointUseLog(makeDebugPointUseLogJson(displayName, command, true, remaining));

	obs_log(LOG_INFO, "[%s] %s used action '%s' (cost=%d, remaining=%d)", TAG,
	        userId.toUtf8().constData(), found->command.c_str(), found->cost, remaining);
}

void PointManager::onPointCheck(const QString &userId, const QString &platform,
                                 const QString &displayName)
{
	const int pts = getPoints(userId, platform);
	emit systemCommentRequested(displayName.toStdString() + "さんへ：現在のポイントは" +
	                            std::to_string(pts) + "ptです");
}

int PointManager::getPoints(const QString &userId, const QString &platform) const
{
	const std::string key = makeKey(userId, platform).toStdString();
	const auto it = points_.find(key);
	return it != points_.end() ? it->second : 0;
}

std::vector<PointManager::UserPoints> PointManager::allUserPoints() const
{
	std::vector<UserPoints> result;
	result.reserve(points_.size());
	for (const auto &kv : points_) {
		const auto it  = keyToUserId_.find(kv.first);
		const auto it2 = keyToPlatform_.find(kv.first);
		if (it == keyToUserId_.end() || it2 == keyToPlatform_.end())
			continue;
		UserPoints up;
		up.userId   = it->second;
		up.platform = it2->second;
		up.points   = kv.second;
		result.push_back(up);
	}
	std::sort(result.begin(), result.end(),
	          [](const UserPoints &a, const UserPoints &b) { return a.points > b.points; });
	return result;
}

void PointManager::setPoints(const QString &userId, const QString &platform, int points)
{
	const std::string key   = makeKey(userId, platform).toStdString();
	points_[key]            = std::max(0, points);
	keyToUserId_[key]   = userId;
	keyToPlatform_[key] = platform;
	scheduleSave();
}

void PointManager::adjustPoints(const QString &userId, const QString &platform, int delta)
{
	const std::string key = makeKey(userId, platform).toStdString();
	int &p                = points_[key];
	keyToUserId_[key]   = userId;
	keyToPlatform_[key] = platform;
	p = std::max(0, p + delta);
	scheduleSave();
}

void PointManager::onWatchTimer()
{
	const auto &cfg = PluginConfig::instance();
	if (!cfg.pointEnabled || cfg.pointWatchAmount <= 0 || points_.empty())
		return;
	for (auto &kv : points_) {
		kv.second += cfg.pointWatchAmount;
		const auto it = keyToUserId_.find(kv.first);
		if (it != keyToUserId_.end())
			emit debugPointLog(makeDebugPointLogJson(
				it->second, cfg.pointWatchAmount,
				QStringLiteral("watch"), kv.second));
	}
	scheduleSave();
	obs_log(LOG_INFO, "[%s] watch tick: +%d to %zu users", TAG,
	        cfg.pointWatchAmount, points_.size());
}

void PointManager::onSaveTimer()
{
	saveNow();
}

// static
QString PointManager::makeKey(const QString &userId, const QString &platform)
{
	return platform + ":" + userId;
}

void PointManager::addPoints(const QString &userId, const QString &platform, int amount,
                              const QString &reason)
{
	const std::string key = makeKey(userId, platform).toStdString();
	int &p                = points_[key];
	keyToUserId_[key]   = userId;
	keyToPlatform_[key] = platform;
	p = std::max(0, p + amount);
	scheduleSave();
	if (!reason.isEmpty())
		emit debugPointLog(makeDebugPointLogJson(userId, amount, reason, p));
}

void PointManager::scheduleSave()
{
	QMetaObject::invokeMethod(this, [this]() {
		if (saveTimer_->isActive())
			saveTimer_->stop();
		saveTimer_->start(3000);
	}, Qt::QueuedConnection);
}

void PointManager::saveNow()
{
	if (csvPath_.isEmpty())
		return;

	QDir().mkpath(QFileInfo(csvPath_).absolutePath());

	// CSV 内容をメモリ上に構築してからチェックサムを計算・保存
	QString content = QStringLiteral("userId,platform,points\n");
	for (const auto &kv : points_) {
		const auto it  = keyToUserId_.find(kv.first);
		const auto it2 = keyToPlatform_.find(kv.first);
		if (it == keyToUserId_.end() || it2 == keyToPlatform_.end())
			continue;
		content += it->second + "," + it2->second + "," +
		           QString::number(kv.second) + "\n";
	}
	const QByteArray data = content.toUtf8();

	QFile f(csvPath_);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		obs_log(LOG_WARNING, "[%s] failed to save CSV: %s", TAG,
		        csvPath_.toUtf8().constData());
		return;
	}
	f.write(data);
	f.close();

	saveChecksumFor(data);
	obs_log(LOG_INFO, "[%s] saved %zu users to CSV", TAG, points_.size());
}

void PointManager::saveChecksumFor(const QByteArray &csvData)
{
	const QByteArray hash =
		QCryptographicHash::hash(csvData, QCryptographicHash::Sha256).toHex();
	QFile cf(csvPath_ + ".checksum");
	if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate))
		cf.write(hash);
}

void PointManager::resetAllPoints()
{
	for (auto &kv : points_)
		kv.second = 0;
	tamperDetected_ = false;
	saveNow();
	obs_log(LOG_INFO, "[%s] all user points reset to 0", TAG);
}

void PointManager::loadCsv()
{
	if (csvPath_.isEmpty())
		return;

	QFile f(csvPath_);
	if (!f.open(QIODevice::ReadOnly))
		return;
	const QByteArray data = f.readAll();
	f.close();

	// チェックサム検証
	const QString checksumPath = csvPath_ + ".checksum";
	QFile cf(checksumPath);
	if (!cf.open(QIODevice::ReadOnly)) {
		obs_log(LOG_WARNING, "[%s] checksum file not found — tamper warning", TAG);
		tamperDetected_ = true;
	} else {
		const QByteArray stored  = cf.readAll().trimmed();
		cf.close();
		const QByteArray computed =
			QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
		if (stored != computed) {
			obs_log(LOG_WARNING, "[%s] checksum mismatch — tamper warning", TAG);
			tamperDetected_ = true;
		}
	}

	// CSV をパース（改ざん有無にかかわらず読み込む）
	QTextStream ts(data);
	bool firstLine = true;
	while (!ts.atEnd()) {
		const QString line = ts.readLine().trimmed();
		if (firstLine) {
			firstLine = false;
			continue;
		}
		if (line.isEmpty())
			continue;
		const QStringList parts = line.split(',');
		if (parts.size() < 3)
			continue;
		const QString userId   = parts[0].trimmed();
		const QString platform = parts[1].trimmed();
		bool ok;
		const int pts = parts[2].trimmed().toInt(&ok);
		if (userId.isEmpty() || platform.isEmpty() || !ok)
			continue;
		const std::string key = makeKey(userId, platform).toStdString();
		points_[key]          = std::max(0, pts);
		keyToUserId_[key]   = userId;
		keyToPlatform_[key] = platform;
	}
	obs_log(LOG_INFO, "[%s] loaded %zu users from CSV", TAG, points_.size());
}

std::string PointManager::makeDebugPointLogJson(const QString &user, int delta,
                                                  const QString &reason, int balance) const
{
	const std::string ts = QTime::currentTime().toString("HH:mm:ss").toStdString();
	return std::string("{\"type\":\"debug_point_log\",") +
	       "\"time\":\""    + escapeJson(ts)                     + "\"," +
	       "\"user\":\""    + escapeJson(user.toStdString())     + "\"," +
	       "\"delta\":"     + std::to_string(delta)              + "," +
	       "\"reason\":\""  + escapeJson(reason.toStdString())   + "\"," +
	       "\"balance\":"   + std::to_string(balance)            + "}";
}

std::string PointManager::makeDebugPointUseLogJson(const QString &user, const QString &command,
                                                    bool ok, int balance,
                                                    const std::string &error) const
{
	const std::string ts = QTime::currentTime().toString("HH:mm:ss").toStdString();
	std::string j = std::string("{\"type\":\"debug_point_use_log\",") +
	                "\"time\":\""    + escapeJson(ts)                       + "\"," +
	                "\"user\":\""    + escapeJson(user.toStdString())       + "\"," +
	                "\"command\":\"" + escapeJson(command.toStdString())    + "\"," +
	                "\"ok\":"        + (ok ? "true" : "false")              + "," +
	                "\"balance\":"   + std::to_string(balance);
	if (!error.empty())
		j += ",\"error\":\"" + escapeJson(error) + "\"";
	j += "}";
	return j;
}

void PointManager::executeAction(const PointActionDef &action, const QString &userId)
{
	if (action.actionType == "trigger_effect") {
		emit triggerEffectRequested(QString::fromStdString(action.actionValue));
	} else if (action.actionType == "set_model") {
		emit setModelRequested(userId, QString::fromStdString(action.actionValue));
	} else {
		obs_log(LOG_WARNING, "[%s] unknown action type: %s", TAG,
		        action.actionType.c_str());
	}
}
