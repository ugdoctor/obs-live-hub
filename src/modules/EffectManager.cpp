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

#include "EffectManager.hpp"
#include "core/PluginConfig.hpp"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QTimer>

#include <obs-module.h>
#include <plugin-support.h>

static const char *TAG = "EffectManager";

EffectManager::EffectManager(QObject *parent) : QObject(parent) {}

void EffectManager::loadEffects(const QString &effectsDir)
{
	effectsDir_ = effectsDir;
	effects_.clear();
	while (!pendingQueue_.empty())
		pendingQueue_.pop();
	activeCount_ = 0;

	QDir dir(effectsDir);
	if (!dir.exists()) {
		obs_log(LOG_INFO, "[%s] effects dir not found: %s", TAG,
			effectsDir.toUtf8().constData());
		return;
	}

	const QStringList jsonFiles = dir.entryList({"*.json"}, QDir::Files);
	for (const QString &filename : jsonFiles) {
		QFile f(effectsDir + "/" + filename);
		if (!f.open(QIODevice::ReadOnly))
			continue;
		const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		f.close();
		if (!doc.isObject())
			continue;

		const QJsonObject obj = doc.object();
		EffectDef def;
		def.name = obj["name"].toString().toStdString();
		if (def.name.empty())
			continue;

		const QJsonObject trigger = obj["trigger"].toObject();
		def.triggerType  = trigger["type"].toString().toStdString();
		def.triggerValue = trigger["value"].toString().toStdString();
		def.file         = obj["file"].toString().toStdString();
		if (def.file.empty())
			continue;

		def.duration         = obj["duration"].toInt(3000);
		def.position         = obj["position"].toString("center").toStdString();
		def.positionOverride = obj["positionOverride"].toBool(false);
		def.size             = obj["size"].toString("medium").toStdString();
		def.sizeOverride     = obj["sizeOverride"].toBool(false);

		effects_.push_back(def);
		obs_log(LOG_INFO, "[%s] loaded: %s (trigger=%s:%s file=%s)", TAG,
			def.name.c_str(), def.triggerType.c_str(), def.triggerValue.c_str(),
			def.file.c_str());
	}
	obs_log(LOG_INFO, "[%s] %zu effect(s) loaded from %s", TAG, effects_.size(),
		effectsDir.toUtf8().constData());
}

void EffectManager::onComment(const QString &text, const QString &user)
{
	for (const auto &ef : effects_) {
		if (ef.triggerType != "emoji" || ef.triggerValue.empty())
			continue;
		if (text.contains(QString::fromStdString(ef.triggerValue)))
			triggerEffect(ef, user);
	}
}

void EffectManager::onOlhEffect(const QString &effectValue, const QString &user)
{
	const std::string target = "effect:" + effectValue.toStdString();
	for (const auto &ef : effects_) {
		if (ef.triggerType == "olh_command" && ef.triggerValue == target)
			triggerEffect(ef, user);
	}
}

void EffectManager::triggerEffect(const EffectDef &effect, const QString &user)
{
	const auto &cfg     = PluginConfig::instance();
	const int maxActive = cfg.effectMaxConcurrent;
	const int maxQueue  = cfg.effectMaxQueue;

	if (activeCount_ < maxActive) {
		++activeCount_;
		obs_log(LOG_INFO, "[%s] trigger: %s (active=%d)", TAG, effect.name.c_str(),
			activeCount_);
		emit broadcastEffect(makeEffectJson(effect));
		emit broadcastDebugLog(makeDebugLogJson(effect, user, "playing"));
		emit broadcastDebugStatus(makeDebugStatusJson());
		const int delay = (effect.duration > 0 ? effect.duration : 3000) + 300;
		QTimer::singleShot(delay, this, &EffectManager::onEffectFinished);
	} else if (static_cast<int>(pendingQueue_.size()) < maxQueue) {
		obs_log(LOG_INFO, "[%s] queued: %s (queue=%zu)", TAG, effect.name.c_str(),
			pendingQueue_.size() + 1);
		pendingQueue_.push(effect);
		emit broadcastDebugLog(makeDebugLogJson(effect, user, "queued"));
		emit broadcastDebugStatus(makeDebugStatusJson());
	} else {
		obs_log(LOG_INFO, "[%s] discarded (queue full): %s", TAG, effect.name.c_str());
		emit broadcastDebugLog(makeDebugLogJson(effect, user, "discarded"));
	}
}

void EffectManager::onEffectFinished()
{
	activeCount_ = std::max(0, activeCount_ - 1);
	if (!pendingQueue_.empty()) {
		const EffectDef next = pendingQueue_.front();
		pendingQueue_.pop();
		triggerEffect(next, {});
	} else {
		emit broadcastDebugStatus(makeDebugStatusJson());
	}
}

std::string EffectManager::makeDebugStatusJson() const
{
	const auto &cfg = PluginConfig::instance();
	return std::string("{\"type\":\"debug_effect_status\",") +
	       "\"activeCount\":" + std::to_string(activeCount_) + "," +
	       "\"maxActive\":"   + std::to_string(cfg.effectMaxConcurrent) + "," +
	       "\"queueCount\":"  + std::to_string(pendingQueue_.size()) + "," +
	       "\"maxQueue\":"    + std::to_string(cfg.effectMaxQueue) + "}";
}

std::string EffectManager::makeDebugLogJson(const EffectDef &effect, const QString &user,
                                             const std::string &status) const
{
	const std::string ts = QTime::currentTime().toString("HH:mm:ss").toStdString();

	auto esc = [](const std::string &s) -> std::string {
		std::string r;
		r.reserve(s.size() + 4);
		for (unsigned char c : s) {
			if (c == '"')       r += "\\\"";
			else if (c == '\\') r += "\\\\";
			else if (c >= 0x20) r += static_cast<char>(c);
		}
		return r;
	};

	return std::string("{\"type\":\"debug_effect_log\",") +
	       "\"time\":\""         + esc(ts)                          + "\"," +
	       "\"name\":\""         + esc(effect.name)                 + "\"," +
	       "\"triggerType\":\""  + esc(effect.triggerType)          + "\"," +
	       "\"triggerValue\":\"" + esc(effect.triggerValue)         + "\"," +
	       "\"user\":\""         + esc(user.toStdString())          + "\"," +
	       "\"status\":\""       + esc(status)                      + "\"," +
	       "\"discarded\":"      + (status == "discarded" ? "true" : "false") + "}";
}

std::string EffectManager::makeEffectJson(const EffectDef &effect) const
{
	const auto &cfg = PluginConfig::instance();
	const std::string position =
		effect.positionOverride ? effect.position : cfg.effectDefaultPosition;
	const std::string size =
		effect.sizeOverride ? effect.size : cfg.effectDefaultSize;

	auto esc = [](const std::string &s) -> std::string {
		std::string r;
		r.reserve(s.size() + 4);
		for (const unsigned char c : s) {
			if (c == '"')
				r += "\\\"";
			else if (c == '\\')
				r += "\\\\";
			else if (c >= 0x20)
				r += static_cast<char>(c);
		}
		return r;
	};

	return std::string("{\"type\":\"effect_trigger\",") +
	       "\"name\":\"" + esc(effect.name) + "\"," +
	       "\"file\":\"effects/" + esc(effect.file) + "\"," +
	       "\"position\":\"" + esc(position) + "\"," +
	       "\"size\":\"" + esc(size) + "\"," +
	       "\"duration\":" + std::to_string(effect.duration) + "}";
}
