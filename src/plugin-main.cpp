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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMutex>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWidget>

#include "core/EventBus.hpp"
#include "core/PlatformInterface.hpp"
#include "core/PluginConfig.hpp"
#include "modules/AivisEngine.hpp"
#include "modules/AivisStyleCache.hpp"
#include "modules/EngineManager.hpp"
#include "modules/BouyomiChanClient.hpp"
#include "modules/VoiceroidClient.hpp"
#include "modules/ViewerTtsSettings.hpp"
#include "modules/WsServer.hpp"
#include "platforms/TwitchPlatform.hpp"
#include "platforms/YouTubePlatform.hpp"
#include "ui/CommentDock.hpp"
#include "ui/AivisParamLimitDialog.hpp"
#include "ui/BouyomiParamLimitDialog.hpp"
#include "ui/DebugSettingsDialog.hpp"
#include "ui/EffectSettingsDialog.hpp"
#include "ui/OverlayStyleDialog.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/StreamSettingsDialog.hpp"
#include "ui/TtsDictionaryDialog.hpp"
#include "ui/VoteManagerDialog.hpp"
#include "ui/TtsSpeechDialog.hpp"
#include "ui/BrowserDiagDialog.hpp"
#include "ui/ConversationOverlayDialog.hpp"
#include "modules/EffectManager.hpp"
#include "modules/PointManager.hpp"
#include "ui/PointSettingsDialog.hpp"
#include "modules/XClient.hpp"
#include "ui/XAccountSettingsDialog.hpp"
#include "ui/XTemplateSettingsDialog.hpp"
#include "ui/XPostDock.hpp"
#include "ui/XPostConfirmDialog.hpp"
#include "ui/XApiTestDialog.hpp"
#include "ui/XManualPostDialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static CommentDock *s_dock = nullptr;
static YouTubePlatform *s_youtube = nullptr;
static TwitchPlatform *s_twitch = nullptr;
static WsServer *s_wsServer = nullptr;
static EffectManager *s_effectManager = nullptr;
static PointManager *s_pointManager = nullptr;
static XPostDock *s_xPostDock = nullptr;

// userId → platform マップ（overlay.html の WS メッセージにはプラットフォームが含まれないため）
static QMutex              s_userPlatformMutex;
static QMap<QString, QString> s_userPlatformMap;

// EventBus は Twitch → UI の橋渡し。YouTube は Qt シグナルを直接使う。
static EventBus<CommentEvent> s_eventBus;
static EventBus<CommentEvent>::SubId s_twitchSubId = 0;

// Vote state (main thread only)
static bool s_voteActive = false;
static QString s_voteMode;
static QString s_voteQuestion;
static QList<QPair<QString, QString>> s_voteChoices;
static QMap<QString, QString> s_voteUserVote;
static QPointer<VoteManagerDialog> s_voteDialog;

static std::string escapeJsonString(const std::string &s)
{
	std::string r;
	r.reserve(s.size() + 16);
	for (unsigned char c : s) {
		if (c == '"')       r += "\\\"";
		else if (c == '\\') r += "\\\\";
		else if (c == '\n') r += "\\n";
		else if (c == '\r') r += "\\r";
		else if (c >= 0x20) r += static_cast<char>(c);
	}
	return r;
}

static std::string makeCommentJson(const std::string &user, const std::string &text,
				   const std::string &platform, const std::string &avatar = "",
				   const std::string &ttsJson = "")
{
	std::string r = "{\"type\":\"comment\","
	                "\"user\":\"" + escapeJsonString(user) + "\","
	                "\"text\":\"" + escapeJsonString(text) + "\","
	                "\"platform\":\"" + platform + "\","
	                "\"avatar\":\"" + escapeJsonString(avatar) + "\"";
	if (!ttsJson.empty())
		r += ",\"tts\":" + ttsJson;
	r += "}";
	return r;
}

static std::string makeSystemCommentJson(const std::string &text)
{
	return "{\"type\":\"comment\","
	       "\"user\":\"obs-live-hub\","
	       "\"text\":\"" + escapeJsonString(text) + "\","
	       "\"platform\":\"system\","
	       "\"avatar\":\"\"}";
}

// ── 発言者 TTS 情報の解決 ──────────────────────────────────────────────────

static std::string engineBaseUrl(const std::string &engine)
{
	const auto &cfg = PluginConfig::instance();
	if (engine == "aivisspeech") return cfg.aivisUrl;
	if (engine == "sharevox")    return cfg.sharevoxUrl;
	if (engine == "lmroid")      return cfg.lmroidUrl;
	if (engine == "itvoice")     return cfg.itvoiceUrl;
	return {};
}

static bool isEngineConnected(const std::string &engine)
{
	if (engine == "webspeech")
		return true;
	const auto statuses = EngineManager::getAllStatuses();
	const auto it       = statuses.find(engine);
	return it != statuses.end() &&
	       it->second.state == EngineManager::EngineState::Connected;
}

// 発言者の TTS 設定を解決して "tts" オブジェクト JSON を返す
static std::string buildCommentTtsJson(const QString &userId, const QString &platform)
{
	const auto &cfg      = PluginConfig::instance();
	const auto  viewerOpt = ViewerTtsSettings::instance().get(userId, platform);

	// エンジン決定：個人設定 → グローバル設定 → webspeech フォールバック
	std::string engine = (viewerOpt && !viewerOpt->engine.empty())
	                         ? viewerOpt->engine
	                         : cfg.ttsEngine;
	if (cfg.ttsCheckEngineConnection && !isEngineConnected(engine))
		engine = "webspeech";

	const int64_t styleId         = viewerOpt ? viewerOpt->styleId         : cfg.aivisStyleId;
	const float   rate            = viewerOpt ? viewerOpt->rate             : cfg.ttsRate;
	const float   pitch           = viewerOpt ? viewerOpt->pitch            : cfg.ttsPitch;
	const float   volume          = viewerOpt ? viewerOpt->volume           : cfg.ttsVolume;
	const int     bouyomiVoice    = viewerOpt ? viewerOpt->bouyomiVoice     : cfg.bouyomiVoice;
	const float   aivisSpeed      = viewerOpt ? viewerOpt->aivisSpeed       : 1.0f;
	const float   aivisPitch      = viewerOpt ? viewerOpt->aivisPitch       : 0.0f;
	const float   aivisIntonation = viewerOpt ? viewerOpt->aivisIntonation  : 1.0f;
	const float   aivisVolume     = viewerOpt ? viewerOpt->aivisVolume      : 1.0f;
	const float   aivisEmotion    = viewerOpt ? viewerOpt->aivisEmotion     : 1.0f;
	const int     bouyomiVolume   = viewerOpt ? viewerOpt->bouyomiVolume    : -1;
	const int     bouyomiSpeed    = viewerOpt ? viewerOpt->bouyomiSpeed     : -1;
	const int     bouyomiTone     = viewerOpt ? viewerOpt->bouyomiTone      : -1;

	auto f = [](float v) -> std::string {
		char buf[16];
		snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
		for (char *p = buf; *p; ++p)
			if (*p == ',') *p = '.';
		return buf;
	};

	return std::string("{") +
	       "\"engine\":\"" + engine + "\"," +
	       "\"baseUrl\":\"" + escapeJsonString(engineBaseUrl(engine)) + "\"," +
	       "\"styleId\":" + std::to_string(styleId) + "," +
	       "\"rate\":" + f(rate) + "," +
	       "\"pitch\":" + f(pitch) + "," +
	       "\"volume\":" + f(volume) + "," +
	       "\"bouyomiVoice\":" + std::to_string(bouyomiVoice) + "," +
	       "\"aivisSpeed\":" + f(aivisSpeed) + "," +
	       "\"aivisPitch\":" + f(aivisPitch) + "," +
	       "\"aivisIntonation\":" + f(aivisIntonation) + "," +
	       "\"aivisVolume\":" + f(aivisVolume) + "," +
	       "\"aivisEmotion\":" + f(aivisEmotion) + "," +
	       "\"bouyomiVolume\":" + std::to_string(bouyomiVolume) + "," +
	       "\"bouyomiSpeed\":" + std::to_string(bouyomiSpeed) + "," +
	       "\"bouyomiTone\":" + std::to_string(bouyomiTone) +
	       "}";
}

static void connectTwitchSignals();               // forward declaration
static void handleWsClientMessage(const QString &json); // forward declaration
static std::string makeTtsJson();                 // forward declaration
static void broadcastTtsDict();                   // forward declaration
static void broadcastDebugConfig();               // forward declaration
static std::string makeConversationConfigJson();  // forward declaration
static void broadcastConversationConfig();        // forward declaration
static void processEffectOlhCommand(const QString &message, const QString &user = {}); // forward declaration
static void processPointOlhCommand(const QString &userId, const QString &platform,
                                   const QString &displayName,
                                   const QString &message); // forward declaration

static void recordUserPlatform(const QString &userId, const QString &platform)
{
	QMutexLocker lk(&s_userPlatformMutex);
	s_userPlatformMap.insert(userId, platform);
}

static QString platformForUser(const QString &userId)
{
	QMutexLocker lk(&s_userPlatformMutex);
	return s_userPlatformMap.value(userId);
}

static void applyWsCallbacks(WsServer *srv)
{
	if (!srv)
		return;
	srv->setMessageCallback([](const std::string &json) {
		const QString js = QString::fromStdString(json);
		QMetaObject::invokeMethod(qApp, [js]() { handleWsClientMessage(js); },
		                          Qt::QueuedConnection);
	});
	srv->setConnectCallback([]() {
		QMetaObject::invokeMethod(qApp, []() {
			if (!s_wsServer)
				return;
			s_wsServer->broadcast(makeTtsJson());
			s_wsServer->broadcast(TtsDictionaryDialog::makeDictJson());
			broadcastDebugConfig();
			broadcastConversationConfig();
		}, Qt::QueuedConnection);
	});
}

static void restartWsServer()
{
	const int newPort = PluginConfig::instance().wsPort;

	// ポートが変わっていなければ再起動不要（既存クライアント接続を維持）
	if (s_wsServer && s_wsServer->isRunning() &&
	    static_cast<int>(s_wsServer->port()) == newPort) {
		obs_log(LOG_INFO, "WsServer: port unchanged (%d), skipping restart", newPort);
		return;
	}

	if (s_wsServer) {
		s_wsServer->stop();
		delete s_wsServer;
		s_wsServer = nullptr;
	}

	s_wsServer = new WsServer(static_cast<uint16_t>(newPort));
	applyWsCallbacks(s_wsServer);
	if (!s_wsServer->start()) {
		obs_log(LOG_WARNING, "WsServer: Failed to start on port %d — "
			"接続診断ダイアログで詳細を確認してください", newPort);
		// インスタンスは保持する。isRunning()==false のため broadcast は no-op 。
		// listenState() でバインド失敗の理由を接続診断ダイアログに表示するために必要。
	}
}

static void reconnectTwitch()
{
	const auto &cfg = PluginConfig::instance();

	if (s_twitch) {
		s_twitch->disconnect();
		delete s_twitch;
		s_twitch = nullptr;
	}

	if (cfg.twitchOAuthToken.empty() || cfg.twitchChannel.empty()) {
		obs_log(LOG_WARNING, "Twitch: channel or token not configured — skipping connect");
		return;
	}

	s_twitch = new TwitchPlatform(s_eventBus, QString::fromStdString(cfg.twitchOAuthToken),
				      QString::fromStdString(cfg.twitchUsername),
				      QString::fromStdString(cfg.twitchChannel),
				      QString::fromStdString(cfg.twitchClientId));

	connectTwitchSignals();
	s_twitch->connect();
}

static void connectTwitchSignals()
{
	QObject::connect(s_twitch, &TwitchPlatform::errorOccurred, [](const QString &msg) {
		obs_log(LOG_WARNING, "Twitch error: %s", msg.toUtf8().constData());
	});

	QObject::connect(s_twitch, &TwitchPlatform::authFailed, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error\",\"code\":\"TWITCH_AUTH_FAILED\","
				"\"message\":\"Twitch認証失敗 - トークンを再取得してください\"}");
	});

	QObject::connect(s_twitch, &TwitchPlatform::connectionLost, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error\",\"code\":\"TWITCH_CONNECTION_LOST\","
				"\"message\":\"Twitch接続が切断されました - 再接続中\"}");
	});

	QObject::connect(s_twitch, &TwitchPlatform::joinSucceeded, []() {
		if (s_wsServer) {
			s_wsServer->broadcast(
				"{\"type\":\"error_clear\",\"code\":\"TWITCH_AUTH_FAILED\"}");
			s_wsServer->broadcast(
				"{\"type\":\"error_clear\",\"code\":\"TWITCH_CONNECTION_LOST\"}");
		}
	});
}

static void broadcastVoteUpdate(bool ending = false)
{
	QMap<QString, int> counts;

	if (s_voteMode == "choice") {
		for (const auto &p : s_voteChoices)
			counts[p.first] = 0;
		for (auto it = s_voteUserVote.cbegin(); it != s_voteUserVote.cend(); ++it)
			if (counts.contains(it.value()))
				counts[it.value()]++;
	} else {
		for (auto it = s_voteUserVote.cbegin(); it != s_voteUserVote.cend(); ++it)
			counts[it.value()]++;
	}

	const int total = s_voteUserVote.size();

	QList<VoteResultItem> items;
	if (s_voteMode == "choice") {
		for (const auto &p : s_voteChoices) {
			VoteResultItem item;
			item.key     = p.first;
			item.label   = p.second;
			item.count   = counts.value(p.first, 0);
			item.percent = total > 0 ? (item.count * 100.0 / total) : 0.0;
			items.append(item);
		}
	} else {
		QList<QPair<QString, int>> sorted;
		for (auto it = counts.cbegin(); it != counts.cend(); ++it)
			sorted.append({it.key(), it.value()});
		std::sort(sorted.begin(), sorted.end(),
			  [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
				  return a.second > b.second;
			  });
		for (const auto &p : sorted) {
			VoteResultItem item;
			item.label   = p.first;
			item.count   = p.second;
			item.percent = total > 0 ? (p.second * 100.0 / total) : 0.0;
			items.append(item);
		}
	}

	if (s_voteDialog)
		s_voteDialog->updateResults(s_voteQuestion, s_voteMode, total, items, s_voteActive);

	if (!s_wsServer)
		return;

	QJsonObject obj;
	obj["type"]     = ending ? QStringLiteral("vote_end") : QStringLiteral("vote_update");
	obj["question"] = s_voteQuestion;
	obj["mode"]     = s_voteMode;
	obj["total"]    = total;
	obj["active"]   = s_voteActive;

	QJsonArray arr;
	for (const auto &item : items) {
		QJsonObject r;
		r["key"]     = item.key;
		r["label"]   = item.label;
		r["count"]   = item.count;
		r["percent"] = item.percent;
		arr.append(r);
	}
	obj["results"] = arr;

	s_wsServer->broadcast(
		QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
}

static void processVoteComment(const QString &author, const QString &message)
{
	if (!s_voteActive)
		return;

	const QString text   = message.trimmed();
	const QString prefix = QStringLiteral("!vote ");
	if (!text.startsWith(prefix, Qt::CaseInsensitive))
		return;

	const QString value = text.mid(prefix.length()).trimmed();
	if (value.isEmpty())
		return;

	if (s_voteMode == "choice") {
		const QString upper = value.left(1).toUpper();
		bool valid = false;
		for (const auto &p : s_voteChoices) {
			if (p.first == upper) {
				valid = true;
				break;
			}
		}
		if (!valid)
			return;
		s_voteUserVote[author] = upper;
	} else {
		s_voteUserVote[author] = value;
	}

	broadcastVoteUpdate();
}

static void connectYouTubeSignals()
{
	// commentReceived: Qt は余分なパラメータを無視するので CommentDock 側は変更不要
	QObject::connect(s_youtube, &YouTubePlatform::commentReceived, s_dock,
			 &CommentDock::onCommentReceived);

	QObject::connect(
		s_youtube, &YouTubePlatform::commentReceived,
		[](const QString &author, const QString &message, const QString &avatarUrl) {
			recordUserPlatform(author, QStringLiteral("youtube"));
			if (s_wsServer)
				s_wsServer->broadcast(makeCommentJson(
					author.toStdString(), message.toStdString(), "youtube",
					avatarUrl.toStdString(),
					buildCommentTtsJson(author, QStringLiteral("youtube"))));
			processVoteComment(author, message);
			if (s_effectManager) {
				s_effectManager->onComment(message, author);
				processEffectOlhCommand(message, author);
			}
			if (s_pointManager) {
				s_pointManager->onComment(author, QStringLiteral("youtube"));
				processPointOlhCommand(author, QStringLiteral("youtube"),
				                       author, message);
			}
		});

	QObject::connect(s_youtube, &YouTubePlatform::quotaWarning, [](int remainingMinutes) {
		obs_log(LOG_WARNING, "YouTube quota warning: ~%d minutes remaining",
			remainingMinutes);
		if (s_wsServer)
			s_wsServer->broadcast("{\"type\":\"quota_warning\",\"remainingMinutes\":" +
					      std::to_string(remainingMinutes) + "}");
	});

	QObject::connect(s_youtube, &YouTubePlatform::quotaStopped, []() {
		obs_log(LOG_WARNING, "YouTube quota exceeded — polling stopped");
		if (s_wsServer)
			s_wsServer->broadcast("{\"type\":\"quota_exceeded\"}");
	});

	// 既存 quota_exceeded に加えてエラーバナー用 broadcast を併用
	QObject::connect(s_youtube, &YouTubePlatform::quotaStopped, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error\",\"code\":\"YOUTUBE_QUOTA_EXCEEDED\","
				"\"message\":\"YouTube APIクォータ超過 - UTCの0時にリセットされます\"}");
	});

	QObject::connect(s_youtube, &YouTubePlatform::quotaCleared, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error_clear\",\"code\":\"YOUTUBE_QUOTA_EXCEEDED\"}");
	});

	QObject::connect(s_youtube, &YouTubePlatform::authFailed, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error\",\"code\":\"YOUTUBE_AUTH_FAILED\","
				"\"message\":\"YouTube認証失敗 - OAuthトークンを再設定してください\"}");
	});

	QObject::connect(s_youtube, &YouTubePlatform::pollSucceeded, []() {
		if (s_wsServer)
			s_wsServer->broadcast(
				"{\"type\":\"error_clear\",\"code\":\"YOUTUBE_AUTH_FAILED\"}");
	});

	QObject::connect(s_youtube, &YouTubePlatform::errorOccurred, [](const QString &msg) {
		obs_log(LOG_WARNING, "YouTube error: %s", msg.toUtf8().constData());
	});
}

static void reconnectYouTube()
{
	if (s_youtube) {
		s_youtube->disconnect();
		delete s_youtube;
		s_youtube = nullptr;
	}

	const auto &cfg = PluginConfig::instance();
	if (cfg.youtubeApiKey.empty()) {
		obs_log(LOG_INFO, "reconnectYouTube: API key empty, skipping");
		return;
	}

	obs_log(LOG_INFO,
		"reconnectYouTube: apiKey(len=%zu) broadcastId='%s' "
		"accessToken(len=%zu) refreshToken(len=%zu) tokenExpiry=%lld ignoreQuota=%d",
		cfg.youtubeApiKey.size(), cfg.youtubeBroadcastId.c_str(),
		cfg.youtubeAccessToken.size(), cfg.youtubeRefreshToken.size(),
		static_cast<long long>(cfg.youtubeTokenExpiry), cfg.youtubeIgnoreQuota ? 1 : 0);

	s_youtube = new YouTubePlatform(QString::fromStdString(cfg.youtubeApiKey),
					QString::fromStdString(cfg.youtubeBroadcastId),
					QString::fromStdString(cfg.youtubeAccessToken),
					cfg.youtubeIgnoreQuota);
	connectYouTubeSignals();
	s_youtube->connect();
}

static void reloadAndReconnect()
{
	obs_log(LOG_INFO, "Reloading config and reconnecting all platforms...");
	PluginConfig::instance().load();
	restartWsServer();
	reconnectTwitch();
	reconnectYouTube();
}

// style メッセージ JSON を生成する（ロケール非依存の小数点 '.'）
static std::string makeStyleJson()
{
	const auto &cfg = PluginConfig::instance();

	auto f = [](float v) -> std::string {
		char buf[16];
		snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
		for (char *p = buf; *p; ++p)
			if (*p == ',')
				*p = '.';
		return buf;
	};

	return std::string("{\"type\":\"style\",") +
	       "\"overlayWidth\":" + std::to_string(cfg.overlayWidth) + "," +
	       "\"overlayHeight\":" + std::to_string(cfg.overlayHeight) + "," +
	       "\"bgColor\":\"" + cfg.bgColor + "\"," +
	       "\"bgOpacity\":" + f(cfg.bgOpacity) + "," +
	       "\"cardBgColor\":\"" + cfg.cardBgColor + "\"," +
	       "\"cardOpacity\":" + f(cfg.cardOpacity) + "," +
	       "\"cardBorderColor\":\"" + cfg.cardBorderColor + "\"," +
	       "\"cardBorderOpacity\":" + f(cfg.cardBorderOpacity) + "," +
	       "\"usernameColor\":\"" + cfg.usernameColor + "\"," +
	       "\"textColor\":\"" + cfg.textColor + "\"," +
	       "\"fontSize\":" + std::to_string(cfg.fontSize) + "," +
	       "\"iconSize\":" + std::to_string(cfg.iconSize) + "," +
	       "\"maxComments\":" + std::to_string(cfg.maxComments) + "," +
	       "\"voteBgColor\":\"" + cfg.voteBgColor + "\"," +
	       "\"voteBgOpacity\":" + f(cfg.voteBgOpacity) + "," +
	       "\"voteQuestionColor\":\"" + cfg.voteQuestionColor + "\"," +
	       "\"voteHintColor\":\"" + cfg.voteHintColor + "\"," +
	       "\"voteBarColor\":\"" + cfg.voteBarColor + "\"," +
	       "\"voteBarBgColor\":\"" + cfg.voteBarBgColor + "\"," +
	       "\"voteTotalColor\":\"" + cfg.voteTotalColor + "\"," +
	       "\"voteStatusColor\":\"" + cfg.voteStatusColor + "\"," +
	       "\"voteQuestionSize\":" + std::to_string(cfg.voteQuestionSize) + "," +
	       "\"voteHintSize\":"     + std::to_string(cfg.voteHintSize)     + "," +
	       "\"voteResultSize\":"   + std::to_string(cfg.voteResultSize)   + "," +
	       "\"voteTotalSize\":"    + std::to_string(cfg.voteTotalSize)    + "," +
	       "\"voteStatusSize\":"   + std::to_string(cfg.voteStatusSize)   + "}";
}

static std::string makeTtsJson()
{
	const auto &cfg = PluginConfig::instance();

	auto f = [](float v) -> std::string {
		char buf[16];
		snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
		for (char *p = buf; *p; ++p)
			if (*p == ',')
				*p = '.';
		return buf;
	};

	return std::string("{\"type\":\"tts\",") +
	       "\"enabled\":" + (cfg.ttsEnabled ? "true" : "false") + "," +
	       "\"volume\":" + f(cfg.ttsVolume) + "," +
	       "\"rate\":" + f(cfg.ttsRate) + "," +
	       "\"pitch\":" + f(cfg.ttsPitch) + "," +
	       "\"readUsername\":" + (cfg.ttsReadUsername ? "true" : "false") + "," +
	       "\"maxLength\":" + std::to_string(cfg.ttsMaxLength) + "," +
	       "\"twitch\":" + (cfg.ttsTwitch ? "true" : "false") + "," +
	       "\"youtube\":" + (cfg.ttsYoutube ? "true" : "false") + "," +
	       "\"ttsEngine\":\"" + cfg.ttsEngine + "\"," +
	       "\"aivisUrl\":\"" + cfg.activeVoicevoxUrl() + "\"," +
	       "\"aivisStyleId\":" + std::to_string(cfg.aivisStyleId) + "," +
	       "\"bouyomiVoice\":" + std::to_string(cfg.bouyomiVoice) + "}";
}

static void handleOlhAivisParamsRequest(const QJsonObject &obj)
{
	const QString userId = obj["userId"].toString();
	if (userId.isEmpty())
		return;

	const auto &cfg = PluginConfig::instance();

	auto clampD = [](double v, double lo, double hi) -> double {
		return v < lo ? lo : (v > hi ? hi : v);
	};

	QJsonObject params;
	if (obj.contains("speed") && obj["speed"].isDouble())
		params["speedScale"] = clampD(obj["speed"].toDouble(),
		                              cfg.aivisSpeedMin, cfg.aivisSpeedMax);
	if (obj.contains("pitch") && obj["pitch"].isDouble())
		params["pitchScale"] = clampD(obj["pitch"].toDouble(),
		                              cfg.aivisPitchMin, cfg.aivisPitchMax);
	if (obj.contains("intonation") && obj["intonation"].isDouble())
		params["intonationScale"] = clampD(obj["intonation"].toDouble(),
		                                   cfg.aivisIntonationMin, cfg.aivisIntonationMax);
	if (obj.contains("volume_scale") && obj["volume_scale"].isDouble())
		params["volumeScale"] = clampD(obj["volume_scale"].toDouble(),
		                               cfg.aivisVolumeScaleMin, cfg.aivisVolumeScaleMax);
	if (obj.contains("emotion") && obj["emotion"].isDouble())
		params["tempoDynamicsScale"] = clampD(obj["emotion"].toDouble(),
		                                      cfg.aivisEmotionMin, cfg.aivisEmotionMax);

	if (params.isEmpty())
		return;

	QJsonObject res;
	res["type"]        = QStringLiteral("olh_user_settings");
	res["userId"]      = userId;
	res["aivisParams"] = params;

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());

	// ViewerTtsSettings に書き込み
	{
		const QString platform = platformForUser(userId);
		auto entry = ViewerTtsSettings::instance().get(userId, platform)
		                 .value_or(ViewerTtsEntry{});
		if (params.contains("speedScale"))
			entry.aivisSpeed      = static_cast<float>(params["speedScale"].toDouble());
		if (params.contains("pitchScale"))
			entry.aivisPitch      = static_cast<float>(params["pitchScale"].toDouble());
		if (params.contains("intonationScale"))
			entry.aivisIntonation = static_cast<float>(params["intonationScale"].toDouble());
		if (params.contains("volumeScale"))
			entry.aivisVolume     = static_cast<float>(params["volumeScale"].toDouble());
		if (params.contains("tempoDynamicsScale"))
			entry.aivisEmotion    = static_cast<float>(params["tempoDynamicsScale"].toDouble());
		ViewerTtsSettings::instance().set(userId, platform, entry);
		obs_log(LOG_INFO, "[ViewerTtsSettings] updated: user=%s platform=%s aivis params",
		        userId.toUtf8().constData(), platform.toUtf8().constData());
	}
}

// 棒読みちゃん [olh] パラメータリクエスト（C++ で上下限クランプ後 broadcast）
static void handleOlhBouyomiParamsRequest(const QJsonObject &obj)
{
	const QString userId = obj["userId"].toString();
	if (userId.isEmpty())
		return;

	const auto &cfg = PluginConfig::instance();

	auto clampI = [](int v, int lo, int hi) -> int {
		return v < lo ? lo : (v > hi ? hi : v);
	};

	QJsonObject params;
	bool        has = false;

	// -1 は「棒読みちゃんの現在設定を使う」として常に許可
	if (obj.contains("volume") && obj["volume"].isDouble()) {
		const int v   = obj["volume"].toInt();
		params["volume"] = (v == -1) ? -1 : clampI(v, cfg.bouyomiVolumeMin, cfg.bouyomiVolumeMax);
		has = true;
	}
	if (obj.contains("speed") && obj["speed"].isDouble()) {
		const int v   = obj["speed"].toInt();
		params["speed"] = (v == -1) ? -1 : clampI(v, cfg.bouyomiSpeedMin, cfg.bouyomiSpeedMax);
		has = true;
	}
	if (obj.contains("tone") && obj["tone"].isDouble()) {
		const int v   = obj["tone"].toInt();
		params["tone"] = (v == -1) ? -1 : clampI(v, cfg.bouyomiToneMin, cfg.bouyomiToneMax);
		has = true;
	}

	if (!has)
		return;

	if (s_wsServer) {
		QJsonObject res;
		res["type"]          = QStringLiteral("olh_user_settings");
		res["userId"]        = userId;
		res["bouyomiParams"] = params;
		s_wsServer->broadcast(QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());
	}

	// ViewerTtsSettings に書き込み
	{
		const QString platform = platformForUser(userId);
		auto entry = ViewerTtsSettings::instance().get(userId, platform)
		                 .value_or(ViewerTtsEntry{});
		if (params.contains("volume")) entry.bouyomiVolume = params["volume"].toInt();
		if (params.contains("speed"))  entry.bouyomiSpeed  = params["speed"].toInt();
		if (params.contains("tone"))   entry.bouyomiTone   = params["tone"].toInt();
		ViewerTtsSettings::instance().set(userId, platform, entry);
		obs_log(LOG_INFO, "[ViewerTtsSettings] updated: user=%s platform=%s bouyomi params",
		        userId.toUtf8().constData(), platform.toUtf8().constData());
	}
}

// 棒読みちゃん読み上げリクエスト（tts.html → C++ → 棒読みちゃん HTTP GET）
static void handleBouyomiSpeakRequest(const QJsonObject &obj)
{
	const QString text   = obj["text"].toString();
	if (text.isEmpty())
		return;
	const int voice  = obj.contains("voice")  ? obj["voice"].toInt(0)  : 0;
	const int volume = obj.contains("volume") ? obj["volume"].toInt(-1) : -1;
	const int speed  = obj.contains("speed")  ? obj["speed"].toInt(-1)  : -1;
	const int tone   = obj.contains("tone")   ? obj["tone"].toInt(-1)   : -1;

	const auto &cfg = PluginConfig::instance();
	obs_log(LOG_INFO, "[BouyomiChanClient] speak request: host=%s port=%d voice=%d "
	        "volume=%d speed=%d tone=%d text=\"%s\"",
	        cfg.bouyomiHost.c_str(), cfg.bouyomiPort,
	        voice, volume, speed, tone,
	        text.toUtf8().constData());

	BouyomiChanClient::talk(QString::fromStdString(cfg.bouyomiHost), cfg.bouyomiPort,
	                        text, voice, volume, speed, tone);
}

// VOICEROID 読み上げリクエスト（tts.html → C++ → AssistantSeika HTTP POST）
static void handleVoiceroidSpeakRequest(const QJsonObject &obj)
{
	const QString text = obj["text"].toString();
	if (text.isEmpty())
		return;

	const auto &cfg = PluginConfig::instance();
	VoiceroidClient::talk(
		QString::fromStdString(cfg.voiceroidHost),
		cfg.voiceroidPort,
		cfg.voiceroidCid,
		text,
		QString::fromStdString(cfg.voiceroidUsername),
		QString::fromStdString(cfg.voiceroidPassword));
}

static void handleOlhWebSpeechParamsRequest(const QJsonObject &obj)
{
	const QString userId = obj["userId"].toString();
	if (userId.isEmpty())
		return;

	QJsonObject params;
	if (obj.contains("rate")   && obj["rate"].isDouble())   params["rate"]   = obj["rate"].toDouble();
	if (obj.contains("pitch")  && obj["pitch"].isDouble())  params["pitch"]  = obj["pitch"].toDouble();
	if (obj.contains("volume") && obj["volume"].isDouble()) params["volume"] = obj["volume"].toDouble();

	if (params.isEmpty())
		return;

	QJsonObject res;
	res["type"]            = QStringLiteral("olh_user_settings");
	res["userId"]          = userId;
	res["webSpeechParams"] = params;

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());

	// ViewerTtsSettings に書き込み
	{
		const QString platform = platformForUser(userId);
		auto entry = ViewerTtsSettings::instance().get(userId, platform)
		                 .value_or(ViewerTtsEntry{});
		if (params.contains("rate"))   entry.rate   = static_cast<float>(params["rate"].toDouble());
		if (params.contains("pitch"))  entry.pitch  = static_cast<float>(params["pitch"].toDouble());
		if (params.contains("volume")) entry.volume = static_cast<float>(params["volume"].toDouble());
		ViewerTtsSettings::instance().set(userId, platform, entry);
		obs_log(LOG_INFO, "[ViewerTtsSettings] updated: user=%s platform=%s webspeech params",
		        userId.toUtf8().constData(), platform.toUtf8().constData());
	}
}

static bool isVoicevoxEngineId(const QString &engine)
{
	return engine == "aivisspeech" || engine == "sharevox" ||
	       engine == "lmroid"      || engine == "itvoice";
}

static QString voicevoxUrlForEngine(const QString &engine)
{
	const auto &cfg = PluginConfig::instance();
	if (engine == "aivisspeech") return QString::fromStdString(cfg.aivisUrl);
	if (engine == "sharevox")    return QString::fromStdString(cfg.sharevoxUrl);
	if (engine == "lmroid")      return QString::fromStdString(cfg.lmroidUrl);
	if (engine == "itvoice")     return QString::fromStdString(cfg.itvoiceUrl);
	return {};
}

static void handleOlhEngineRequest(const QJsonObject &obj)
{
	const QString userId = obj["userId"].toString();
	const QString engine = obj["engine"].toString();

	QJsonObject res;
	res["type"]   = QStringLiteral("resolve_engine_result");
	res["userId"] = userId;

	const bool valid = engine == "webspeech"   || engine == "aivisspeech" ||
	                   engine == "sharevox"    || engine == "lmroid" ||
	                   engine == "itvoice"     || engine == "bouyomi";
	if (!valid) {
		res["ok"]    = false;
		res["error"] = QStringLiteral("不明なエンジン: ") + engine;
	} else {
		res["ok"]     = true;
		res["engine"] = engine;
	}

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());

	// ViewerTtsSettings に engine を書き込み
	if (res["ok"].toBool()) {
		const QString platform = platformForUser(userId);
		auto entry = ViewerTtsSettings::instance().get(userId, platform)
		                 .value_or(ViewerTtsEntry{});
		entry.engine  = engine.toStdString();
		entry.styleId = 0;
		ViewerTtsSettings::instance().set(userId, platform, entry);
		obs_log(LOG_INFO, "[ViewerTtsSettings] updated: user=%s platform=%s engine=%s",
		        userId.toUtf8().constData(),
		        platform.toUtf8().constData(),
		        entry.engine.c_str());
	}

	// VOICEVOX互換エンジンに切り替わった場合、先頭話者を resolve_model_result で通知
	if (res["ok"].toBool() && isVoicevoxEngineId(engine)) {
		const QString url      = voicevoxUrlForEngine(engine);
		const QString platform = platformForUser(userId);
		AivisStyleCache::fetchAndCacheAsyncWithResult(url,
			[userId, engine, platform](bool ok, int64_t styleId,
			                           const QString &spName, const QString &stName) {
				if (!ok || !s_wsServer)
					return;
				// 先頭話者の styleId を個人設定に書き込み
				auto entry = ViewerTtsSettings::instance().get(userId, platform)
				                 .value_or(ViewerTtsEntry{});
				entry.styleId = styleId;
				ViewerTtsSettings::instance().set(userId, platform, entry);

				QJsonObject r;
				r["type"]        = QStringLiteral("resolve_model_result");
				r["userId"]      = userId;
				r["ok"]          = true;
				r["engine"]      = engine;
				r["styleId"]     = static_cast<qint64>(styleId);
				r["speakerName"] = spName;
				r["styleName"]   = stName;
				s_wsServer->broadcast(
					QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
			});
	}
}

static void handleResolveModel(const QJsonObject &obj)
{
	const QString userId    = obj["userId"].toString();
	const QString modelName = obj["modelName"].toString();
	const QString platform  = platformForUser(userId);

	// 視聴者個人のエンジン設定を参照（未設定ならグローバル設定を使用）
	const auto    viewerOpt    = ViewerTtsSettings::instance().get(userId, platform);
	const QString viewerEngine = (viewerOpt && !viewerOpt->engine.empty())
	                                 ? QString::fromStdString(viewerOpt->engine)
	                                 : QString::fromStdString(PluginConfig::instance().ttsEngine);

	QJsonObject res;
	res["type"]   = QStringLiteral("resolve_model_result");
	res["userId"] = userId;

	int64_t resolvedStyleId = 0;
	if (AivisStyleCache::isEmpty()) {
		res["ok"]    = false;
		res["error"] = QStringLiteral(
			"音声モデル一覧が未取得です。しばらく待ってから再試行してください。");
	} else {
		const AivisCachedStyle *style = AivisStyleCache::findByName(modelName);
		if (!style) {
			res["ok"]    = false;
			res["error"] = QStringLiteral("モデルが見つかりません: ") + modelName;
		} else {
			resolvedStyleId    = style->styleId;
			res["ok"]          = true;
			res["engine"]      = viewerEngine;
			res["styleId"]     = static_cast<qint64>(style->styleId);
			res["speakerName"] = style->speakerName;
			res["styleName"]   = style->styleName;
		}
	}

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());

	// モデル切り替え成功時に ViewerTtsSettings に styleId を書き込み
	if (res["ok"].toBool()) {
		auto entry = viewerOpt.value_or(ViewerTtsEntry{});
		entry.styleId = resolvedStyleId;
		ViewerTtsSettings::instance().set(userId, platform, entry);
		obs_log(LOG_INFO, "[ViewerTtsSettings] updated: user=%s platform=%s styleId=%lld",
		        userId.toUtf8().constData(),
		        platform.toUtf8().constData(),
		        static_cast<long long>(resolvedStyleId));

		QJsonObject resetMsg;
		resetMsg["type"]        = QStringLiteral("olh_user_settings");
		resetMsg["userId"]      = userId;
		resetMsg["aivisParams"] = QJsonObject(); // 空 = リセット
		if (s_wsServer)
			s_wsServer->broadcast(
				QJsonDocument(resetMsg).toJson(QJsonDocument::Compact).toStdString());
	}
}

static void handleWsClientMessage(const QString &json)
{
	const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
	if (!doc.isObject())
		return;

	const QJsonObject obj  = doc.object();
	const QString     type = obj["type"].toString();

	if (type == "olh_engine_request")
		handleOlhEngineRequest(obj);
	else if (type == "olh_model_request")
		handleResolveModel(obj);
	else if (type == "olh_aivis_params_request")
		handleOlhAivisParamsRequest(obj);
	else if (type == "olh_webspeech_params_request")
		handleOlhWebSpeechParamsRequest(obj);
	else if (type == "olh_bouyomi_params_request")
		handleOlhBouyomiParamsRequest(obj);
	else if (type == "bouyomi_speak")
		handleBouyomiSpeakRequest(obj);
	else if (type == "voiceroid_speak")
		handleVoiceroidSpeakRequest(obj);
	else if (type == "tts_speaking_start") {
		// tts.html からの読み上げ開始通知を全クライアントへ中継する
		if (s_wsServer)
			s_wsServer->broadcast(json.toStdString());
	}
}

static std::string makeConversationConfigJson()
{
	const auto &cfg = PluginConfig::instance();
	return std::string("{\"type\":\"conversation_config\","
	                   "\"maxBubbles\":"       + std::to_string(cfg.conversationMaxBubbles) + ","
	                   "\"zigzagMode\":\""     + cfg.conversationZigzagMode + "\","
	                   "\"horizontalOffset\":" + std::to_string(cfg.conversationHorizontalOffset) + "}");
}

static void broadcastConversationConfig()
{
	if (s_wsServer)
		s_wsServer->broadcast(makeConversationConfigJson());
}

static void onConversationOverlayMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	ConversationOverlayDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted)
		broadcastConversationConfig();
}

static void onOpenConversationOverlayMenuClick(void * /* data */)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return;
	const std::string path =
		std::string(appdata) +
		"\\obs-studio\\plugins\\obs-live-hub\\conversation_overlay.html";
	ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

static void onOverlayStyleMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	OverlayStyleDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted) {
		const std::string json = makeStyleJson();
		obs_log(LOG_INFO, "[StyleMenu] style JSON: %s", json.c_str());
		if (!s_wsServer) {
			obs_log(LOG_WARNING,
				"[StyleMenu] s_wsServer is null — WsServer not running, style not sent");
			return;
		}
		obs_log(LOG_INFO, "[StyleMenu] calling WsServer::broadcast...");
		s_wsServer->broadcast(json);
	}
}

static void doXPostTweet(const QString &text)
{
	const auto &cfg = PluginConfig::instance();
	if (cfg.xApiKey.empty() || cfg.xApiSecret.empty() ||
	    cfg.xAccessToken.empty() || cfg.xAccessTokenSecret.empty()) {
		auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
		QMessageBox::warning(w, "設定不足",
		    "X APIの認証情報が設定されていません。\n"
		    "「obs-live-hub > X投稿 > Xアカウント設定」から設定してください。");
		return;
	}
	if (text.trimmed().isEmpty()) {
		auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
		QMessageBox::warning(w, "テキストが空", "投稿テキストを入力してください。");
		return;
	}
	const std::string apiKey            = cfg.xApiKey;
	const std::string apiSecret         = cfg.xApiSecret;
	const std::string accessToken       = cfg.xAccessToken;
	const std::string accessTokenSecret = cfg.xAccessTokenSecret;
	const std::string textStr           = text.toStdString();

	std::thread([apiKey, apiSecret, accessToken, accessTokenSecret, textStr]() {
		const auto result = XClient::postTweet(
			apiKey, apiSecret, accessToken, accessTokenSecret, textStr);
		QMetaObject::invokeMethod(qApp, [result]() {
			auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
			if (result.ok) {
				QMessageBox::information(w, "X投稿完了", "Xへの投稿が完了しました。");
			} else {
				QMessageBox::warning(w, "X投稿失敗",
				    QString("Xへの投稿に失敗しました。\nHTTP %1").arg(result.httpStatus));
			}
		}, Qt::QueuedConnection);
	}).detach();
}

static void onXApiTestMenuClick(void *)
{
	auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
	XApiTestDialog dlg(w);
	dlg.exec();
}

static void onXManualPostMenuClick(void *)
{
	auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
	XManualPostDialog dlg(w, s_youtube);
	dlg.exec();
}

static void onXAutoPostModeMenuClick(void *)
{
	auto *mainWin = static_cast<QWidget *>(obs_frontend_get_main_window());

	QDialog dlg(mainWin);
	dlg.setWindowTitle("配信開始時の自動投稿設定");
	dlg.setFixedWidth(380);

	auto *radioOff    = new QRadioButton("自動表示しない",                        &dlg);
	auto *radioApi    = new QRadioButton("API投稿確認ダイアログを表示",            &dlg);
	auto *radioManual = new QRadioButton("手動投稿ダイアログ（Web Intent）を表示", &dlg);

	auto *btnGroup = new QButtonGroup(&dlg);
	btnGroup->addButton(radioOff,    0);
	btnGroup->addButton(radioApi,    1);
	btnGroup->addButton(radioManual, 2);

	const int cur = PluginConfig::instance().xAutoPostOnStreamStart;
	if      (cur == 1) radioApi->setChecked(true);
	else if (cur == 2) radioManual->setChecked(true);
	else               radioOff->setChecked(true);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	auto *layout = new QVBoxLayout(&dlg);
	layout->addWidget(radioOff);
	layout->addWidget(radioApi);
	layout->addWidget(radioManual);
	layout->addSpacing(8);
	layout->addWidget(buttons);

	if (dlg.exec() == QDialog::Accepted) {
		int mode = 0;
		if      (radioApi->isChecked())    mode = 1;
		else if (radioManual->isChecked()) mode = 2;
		PluginConfig::instance().xAutoPostOnStreamStart = mode;
		PluginConfig::instance().save();
		if (s_xPostDock)
			s_xPostDock->refresh();
	}
}

static void onXAccountSettingsMenuClick(void *)
{
	auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
	XAccountSettingsDialog dlg(w);
	dlg.exec();
}

static void onXTemplateSettingsMenuClick(void *)
{
	auto *w = static_cast<QWidget *>(obs_frontend_get_main_window());
	XTemplateSettingsDialog dlg(w);
	if (dlg.exec() == QDialog::Accepted && s_xPostDock)
		s_xPostDock->refresh();
}

static void onTtsSpeechMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	TtsSpeechDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted) {
		if (s_wsServer)
			s_wsServer->broadcast(makeTtsJson());
	}
}

static void onBrowserDiagMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	BrowserDiagDialog dlg(s_wsServer, mainWindow);
	dlg.exec();
}

static void onSettingsMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	SettingsDialog dlg(mainWindow);
	// OAuth でトークン取得成功時にダイアログを閉じることなく即座に Twitch 再接続する
	QObject::connect(&dlg, &SettingsDialog::twitchTokenUpdated, []() {
		reconnectTwitch();
	});
	if (dlg.exec() == QDialog::Accepted)
		reloadAndReconnect();
}

static void onOpenOverlayMenuClick(void * /* data */)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return;
	const std::string path =
		std::string(appdata) + "\\obs-studio\\plugins\\obs-live-hub\\overlay.html";
	ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

#ifdef _WIN32
static QString findChromePath()
{
	const char *envVars[] = {"PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"};
	for (const char *env : envVars) {
		char buf[MAX_PATH] = {};
		if (GetEnvironmentVariableA(env, buf, MAX_PATH) == 0)
			continue;
		const QString candidate = QString::fromLocal8Bit(buf) +
		                          "\\Google\\Chrome\\Application\\chrome.exe";
		if (QFile::exists(candidate))
			return candidate;
	}
	return {};
}
#endif

static void onOpenTtsMenuClick(void * /* data */)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return;
	const std::string ttsPathStd =
		std::string(appdata) + "\\obs-studio\\plugins\\obs-live-hub\\tts.html";

	if (PluginConfig::instance().isVoicevoxCompatible()) {
		const QString chromePath = findChromePath();
		if (chromePath.isEmpty()) {
			auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
			QMessageBox::warning(
				mainWindow, "Chromeが見つかりません",
				"Chromeが見つかりません。\n"
				"VOICEVOX互換エンジン使用時はChromeが必要です。\n"
				"Chromeをインストールするか、手動でCORSを無効にして\n"
				"tts.htmlを開いてください。");
			return;
		}
		const QString ttsPath = QString::fromLocal8Bit(ttsPathStd.c_str());
		QProcess::startDetached(chromePath,
		                        {"--disable-web-security",
		                         "--user-data-dir=" + QDir::tempPath() +
		                                 "/obs-live-hub-chrome",
		                         ttsPath});
	} else {
		ShellExecuteA(nullptr, "open", ttsPathStd.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
#endif
}

static void broadcastTtsDict()
{
	if (s_wsServer)
		s_wsServer->broadcast(TtsDictionaryDialog::makeDictJson());
}

static void broadcastDebugConfig()
{
	if (!s_wsServer)
		return;
	const auto &cfg = PluginConfig::instance();
	QJsonObject obj;
	obj["type"]           = QStringLiteral("debug_config");
	obj["showConnection"] = cfg.debugShowConnection;
	obj["showTts"]        = cfg.debugShowTts;
	obj["showQuota"]      = cfg.debugShowQuota;
	obj["showVote"]       = cfg.debugShowVote;
	obj["showLog"]           = cfg.debugShowLog;
	obj["showCommentDetail"] = cfg.debugShowCommentDetail;
	obj["showEffect"]        = cfg.debugShowEffect;
	obj["showPoint"]         = cfg.debugShowPoint;
	obj["pollInterval"]      = cfg.youtubePollInterval;
	s_wsServer->broadcast(QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
}

static void onOpenDebugMenuClick(void * /* data */)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return;
	const std::string path =
		std::string(appdata) + "\\obs-studio\\plugins\\obs-live-hub\\debug.html";
	ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

static void onAivisParamLimitMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	AivisParamLimitDialog dlg(mainWindow);
	dlg.exec();
}

static void onBouyomiParamLimitMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	BouyomiParamLimitDialog dlg(mainWindow);
	dlg.exec();
}

static void onDebugSettingsMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	DebugSettingsDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted)
		broadcastDebugConfig();
}

static void onTtsDictionaryMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	TtsDictionaryDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted)
		broadcastTtsDict();
}

static void onStreamSettingsMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	StreamSettingsDialog dlg(mainWindow);
	dlg.exec();
}

static void onVoteManagerMenuClick(void * /* data */)
{
	if (!s_voteDialog) {
		auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		s_voteDialog     = new VoteManagerDialog(mainWindow);

		QObject::connect(
			s_voteDialog, &VoteManagerDialog::voteStartRequested,
			[](const QString &question, const QString &mode,
			   const QList<QPair<QString, QString>> &choices) {
				s_voteActive   = true;
				s_voteMode     = mode;
				s_voteQuestion = question;
				s_voteChoices  = choices;
				s_voteUserVote.clear();

				if (s_wsServer) {
					QJsonObject obj;
					obj["type"]     = QStringLiteral("vote_start");
					obj["question"] = question;
					obj["mode"]     = mode;
					obj["total"]    = 0;
					obj["active"]   = true;

					QJsonArray choicesArr;
					for (const auto &p : choices) {
						QJsonObject c;
						c["key"]   = p.first;
						c["label"] = p.second;
						choicesArr.append(c);
					}
					obj["choices"] = choicesArr;
					obj["results"] = QJsonArray();
					s_wsServer->broadcast(QJsonDocument(obj)
								      .toJson(QJsonDocument::Compact)
								      .toStdString());
				}

				if (s_voteDialog)
					s_voteDialog->updateResults(question, mode, 0, {}, true);
			});

		QObject::connect(s_voteDialog, &VoteManagerDialog::voteEndRequested, []() {
			s_voteActive = false;
			broadcastVoteUpdate(true);
		});

		QObject::connect(s_voteDialog, &VoteManagerDialog::voteClearRequested, []() {
			s_voteActive = false;
			s_voteQuestion.clear();
			s_voteMode.clear();
			s_voteChoices.clear();
			s_voteUserVote.clear();
			if (s_voteDialog)
				s_voteDialog->updateResults({}, {}, 0, {}, false);
			if (s_wsServer)
				s_wsServer->broadcast(
					"{\"type\":\"vote_end\",\"active\":false,\"total\":0,\"results\":[]}");
		});
	}

	s_voteDialog->show();
	s_voteDialog->raise();
	s_voteDialog->activateWindow();
}

static void onReloadMenuClick(void * /* data */)
{
	reloadAndReconnect();
}

// ── エフェクト ──────────────────────────────────────────────────────────────

static QString getEffectsDir()
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return {};
	return QString::fromLocal8Bit(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\effects";
#else
	return {};
#endif
}

static QString getPointsActionsDir()
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return {};
	return QString::fromLocal8Bit(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\points_actions";
#else
	return {};
#endif
}

static void processPointOlhCommand(const QString &userId, const QString &platform,
                                   const QString &displayName, const QString &message)
{
	if (!s_pointManager || !PluginConfig::instance().pointEnabled)
		return;
	const int olhIdx = message.indexOf("[olh]", Qt::CaseInsensitive);
	if (olhIdx < 0)
		return;
	const QString params    = message.mid(olhIdx + 5).trimmed();
	const QStringList pairs = params.split(',', Qt::SkipEmptyParts);
	for (const QString &pair : pairs) {
		const int colonIdx = pair.indexOf(':');
		const QString key =
			(colonIdx >= 0 ? pair.left(colonIdx) : pair).trimmed();
		const QString value =
			colonIdx >= 0 ? pair.mid(colonIdx + 1).trimmed() : QString();
		if (key.compare("point_use", Qt::CaseInsensitive) == 0 && !value.isEmpty())
			s_pointManager->onPointUse(userId, platform, displayName, value);
		else if (key.compare("point_check", Qt::CaseInsensitive) == 0)
			s_pointManager->onPointCheck(userId, platform, displayName);
	}
}

static void onPointSettingsMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	PointSettingsDialog dlg(s_pointManager, mainWindow);
	dlg.exec();
}

static void processEffectOlhCommand(const QString &message, const QString &user)
{
	if (!s_effectManager)
		return;
	const int olhIdx = message.indexOf("[olh]", Qt::CaseInsensitive);
	if (olhIdx < 0)
		return;
	const QString params    = message.mid(olhIdx + 5).trimmed();
	const QStringList pairs = params.split(',', Qt::SkipEmptyParts);
	for (const QString &pair : pairs) {
		const int colonIdx = pair.indexOf(':');
		if (colonIdx < 0)
			continue;
		const QString key   = pair.left(colonIdx).trimmed();
		const QString value = pair.mid(colonIdx + 1).trimmed();
		if (key.compare("effect", Qt::CaseInsensitive) == 0 && !value.isEmpty())
			s_effectManager->onOlhEffect(value, user);
	}
}

static void onEffectSettingsMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	EffectSettingsDialog dlg(s_effectManager, mainWindow);
	dlg.exec();
}

static void onOpenEffectMenuClick(void * /* data */)
{
#ifdef _WIN32
	char appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) == 0)
		return;
	const std::string path =
		std::string(appdata) + "\\obs-studio\\plugins\\obs-live-hub\\effect.html";
	ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

// ── サブメニュー構築 ────────────────────────────────────────────────────────
// obs_frontend_add_tools_menu_item で追加したマーカー項目を QMenu で置き換える

static const char *kHubMarker = "obs-live-hub\xe2\x80\x8b"; // zero-width space で一意化
static void        obsHubMenuPlaceholder(void *) {}

static void buildObsLiveHubMenu()
{
	auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWin)
		return;

	// マーカー QAction とそれが属するメニューを特定
	QMenu   *toolsMenu = nullptr;
	QAction *markerAct = nullptr;
	const QString marker = QString::fromUtf8(kHubMarker);

	for (QAction *act : mainWin->menuBar()->actions()) {
		QMenu *m = act->menu();
		if (!m)
			continue;
		for (QAction *item : m->actions()) {
			if (item->text() == marker) {
				toolsMenu = m;
				markerAct = item;
				break;
			}
		}
		if (toolsMenu)
			break;
	}

	if (!toolsMenu || !markerAct)
		return;

	auto *hubMenu = new QMenu("obs-live-hub", mainWin);

	auto *connMenu = hubMenu->addMenu("接続・設定");
	connMenu->addAction("設定",         []() { onSettingsMenuClick(nullptr); });
	connMenu->addAction("リロード",     []() { onReloadMenuClick(nullptr); });
	connMenu->addAction("接続診断",     []() { onBrowserDiagMenuClick(nullptr); });
	connMenu->addAction("配信一括設定", []() { onStreamSettingsMenuClick(nullptr); });

	auto *ovMenu = hubMenu->addMenu("オーバーレイ");
	ovMenu->addAction("コメントオーバーレイ設定",              []() { onOverlayStyleMenuClick(nullptr); });
	ovMenu->addAction("コメントオーバーレイをブラウザで開く", []() { onOpenOverlayMenuClick(nullptr); });
	ovMenu->addAction("TTS音声ページを開く",                  []() { onOpenTtsMenuClick(nullptr); });
	ovMenu->addSeparator();
	ovMenu->addAction("チャットオーバーレイ設定",              []() { onConversationOverlayMenuClick(nullptr); });
	ovMenu->addAction("チャットオーバーレイをブラウザで開く",  []() { onOpenConversationOverlayMenuClick(nullptr); });

	auto *ttsMenu = hubMenu->addMenu("読み上げ");
	ttsMenu->addAction("読み上げ設定",             []() { onTtsSpeechMenuClick(nullptr); });
	ttsMenu->addAction("読み上げ辞書",             []() { onTtsDictionaryMenuClick(nullptr); });
	ttsMenu->addAction("AivisSpeechモデル制限",    []() { onAivisParamLimitMenuClick(nullptr); });
	ttsMenu->addAction("棒読みちゃんパラメータ制限", []() { onBouyomiParamLimitMenuClick(nullptr); });

	auto *voteMenu = hubMenu->addMenu("アンケート");
	voteMenu->addAction("アンケート管理", []() { onVoteManagerMenuClick(nullptr); });

	auto *fxMenu = hubMenu->addMenu("エフェクト");
	fxMenu->addAction("エフェクト設定",          []() { onEffectSettingsMenuClick(nullptr); });
	fxMenu->addAction("エフェクトページを開く", []() { onOpenEffectMenuClick(nullptr); });

	auto *ptMenu = hubMenu->addMenu("ポイント");
	ptMenu->addAction("ポイント設定", []() { onPointSettingsMenuClick(nullptr); });

	auto *xMenu = hubMenu->addMenu("X投稿");
	xMenu->addAction("X手動投稿",               []() { onXManualPostMenuClick(nullptr); });
	xMenu->addAction("配信開始時の自動投稿設定", []() { onXAutoPostModeMenuClick(nullptr); });
	xMenu->addSeparator();
	xMenu->addAction("Xアカウント設定",         []() { onXAccountSettingsMenuClick(nullptr); });
	xMenu->addAction("Xテンプレート設定",       []() { onXTemplateSettingsMenuClick(nullptr); });
	xMenu->addAction("X APIテスト",             []() { onXApiTestMenuClick(nullptr); });

	auto *dbgMenu = hubMenu->addMenu("デバッグ");
	dbgMenu->addAction("デバッグ設定",       []() { onDebugSettingsMenuClick(nullptr); });
	dbgMenu->addAction("デバッグ表示を開く", []() { onOpenDebugMenuClick(nullptr); });

	toolsMenu->insertMenu(markerAct, hubMenu);
	toolsMenu->removeAction(markerAct);
	delete markerAct;
}

#ifdef _WIN32
static bool createDirectoryRecursiveW(const std::wstring &path)
{
	if (CreateDirectoryW(path.c_str(), nullptr))
		return true;
	const DWORD err = GetLastError();
	if (err == ERROR_ALREADY_EXISTS)
		return true;
	if (err == ERROR_PATH_NOT_FOUND) {
		const auto pos = path.rfind(L'\\');
		if (pos == std::wstring::npos)
			return false;
		if (!createDirectoryRecursiveW(path.substr(0, pos)))
			return false;
		if (CreateDirectoryW(path.c_str(), nullptr))
			return true;
		return GetLastError() == ERROR_ALREADY_EXISTS;
	}
	return false;
}

static std::string wstrToUtf8(const wchar_t *ws)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return {};
	std::string s(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], n, nullptr, nullptr);
	return s;
}

// プラグインロード時に指定 HTML ファイルを %APPDATA%\obs-studio\plugins\obs-live-hub\ へ
// 強制上書きコピーする。DLL 隣接フォルダ（obs-live-hub\<filename>）をコピー元とする。
static void ensureHtmlFileInAppData(const wchar_t *filename)
{
	const std::wstring tag = L"[auto-copy:" + std::wstring(filename) + L"]";
	const std::string tagUtf8 = wstrToUtf8(tag.c_str());

	// 1. APPDATA 取得
	DWORD needed = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
	if (needed == 0) {
		obs_log(LOG_WARNING, "%s APPDATA not set (err=%lu)", tagUtf8.c_str(),
			GetLastError());
		return;
	}
	std::wstring appdata(needed - 1, L'\0');
	GetEnvironmentVariableW(L"APPDATA", &appdata[0], needed);

	// 2. ターゲットパス構築
	const std::wstring targetDir = appdata + L"\\obs-studio\\plugins\\obs-live-hub";
	const std::wstring targetPath = targetDir + L"\\" + filename;

	// 3. ディレクトリ作成
	if (!createDirectoryRecursiveW(targetDir)) {
		obs_log(LOG_WARNING, "%s mkdir failed (err=%lu)", tagUtf8.c_str(), GetLastError());
		return;
	}

	// 4. この DLL のパスを取得してコピー元を特定
	HMODULE hMod = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ensureHtmlFileInAppData), &hMod) ||
	    !hMod) {
		obs_log(LOG_WARNING, "%s GetModuleHandleExW failed (err=%lu)", tagUtf8.c_str(),
			GetLastError());
		return;
	}

	wchar_t buf[MAX_PATH] = {};
	if (!GetModuleFileNameW(hMod, buf, MAX_PATH)) {
		obs_log(LOG_WARNING, "%s GetModuleFileNameW failed (err=%lu)", tagUtf8.c_str(),
			GetLastError());
		return;
	}

	const std::wstring dllPath(buf);
	const auto slash = dllPath.rfind(L'\\');
	const auto dot = dllPath.rfind(L'.');
	if (slash == std::wstring::npos) {
		obs_log(LOG_WARNING, "%s unexpected DLL path (no backslash)", tagUtf8.c_str());
		return;
	}

	const std::wstring dllDir = dllPath.substr(0, slash);
	const std::wstring baseName = (dot != std::wstring::npos && dot > slash)
					      ? dllPath.substr(slash + 1, dot - slash - 1)
					      : dllPath.substr(slash + 1);
	const std::wstring sourcePath = dllDir + L"\\" + baseName + L"\\" + filename;

	// 5. コピー元の存在確認
	if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		obs_log(LOG_WARNING, "%s source not found: %s (err=%lu)", tagUtf8.c_str(),
			wstrToUtf8(sourcePath.c_str()).c_str(), GetLastError());
		return;
	}

	// 6. コピー実行（常に上書き）
	const bool existed = (GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES);
	if (CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
		obs_log(LOG_INFO, "%s %s -> %s", tagUtf8.c_str(),
			existed ? "overwritten" : "first-copy",
			wstrToUtf8(targetPath.c_str()).c_str());
	} else {
		obs_log(LOG_WARNING, "%s CopyFileW failed (err=%lu)", tagUtf8.c_str(),
			GetLastError());
	}
}
#endif

static void onFrontendEvent(obs_frontend_event event, void * /* data */)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		buildObsLiveHubMenu();
		obs_log(LOG_INFO, "OBS finished loading — auto-connecting Twitch...");
		reconnectTwitch();
		broadcastTtsDict();
		broadcastDebugConfig();
		if (s_effectManager) {
			const QString effectsDir = getEffectsDir();
			if (!effectsDir.isEmpty()) {
				QDir().mkpath(effectsDir);
				s_effectManager->loadEffects(effectsDir);
			}
		}
		if (s_pointManager) {
			const QString actionsDir = getPointsActionsDir();
			if (!actionsDir.isEmpty()) {
				QDir().mkpath(actionsDir);
				s_pointManager->loadActions(actionsDir);
			}
			// 改ざん検知ダイアログ
			if (s_pointManager->hasTamperWarning()) {
				auto *mainWin = static_cast<QWidget *>(obs_frontend_get_main_window());
				QMessageBox msgBox(mainWin);
				msgBox.setIcon(QMessageBox::Warning);
				msgBox.setWindowTitle("ポイントデータ整合性エラー");
				msgBox.setText(
					"ポイントデータの整合性チェックに失敗しました。\n"
					"データが手動で変更された可能性があります。\n\n"
					"このまま読み込みますか？それとも安全のため\n"
					"全ユーザーのポイントを 0 にリセットしますか？");
				auto *keepBtn  = msgBox.addButton("そのまま読み込む",
				                                  QMessageBox::AcceptRole);
				auto *resetBtn = msgBox.addButton("リセットする",
				                                  QMessageBox::DestructiveRole);
				Q_UNUSED(keepBtn);
				msgBox.exec();
				if (msgBox.clickedButton() == resetBtn)
					s_pointManager->resetAllPoints();
				else
					s_pointManager->saveNow(); // チェックサムを再計算して保存
			}
		}
		// 有効化されたエンジンを全て起動・接続確認する
		EngineManager::startAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (s_youtube)
			s_youtube->connect();
		if (s_twitch)
			s_twitch->connect();
		{
			const int autoMode = PluginConfig::instance().xAutoPostOnStreamStart;
			obs_log(LOG_INFO, "[obs-live-hub] STREAMING_STARTED: xAutoPostMode=%d (%s)",
			        autoMode,
			        autoMode == 0 ? "オフ" :
			        autoMode == 1 ? "API投稿確認" :
			        autoMode == 2 ? "手動投稿" : "不明");
			if (autoMode == 1) {
				// API投稿確認ダイアログ
				QMetaObject::invokeMethod(qApp, []() {
					auto *mainWin =
						static_cast<QWidget *>(obs_frontend_get_main_window());
					XPostConfirmDialog dlg(mainWin);
					if (dlg.exec() == QDialog::Accepted)
						doXPostTweet(dlg.postText());
				}, Qt::QueuedConnection);
			} else if (autoMode == 2) {
				// 手動投稿ダイアログ（Web Intent）
				QMetaObject::invokeMethod(qApp, []() {
					auto *mainWin =
						static_cast<QWidget *>(obs_frontend_get_main_window());
					XManualPostDialog dlg(mainWin, s_youtube);
					dlg.exec();
				}, Qt::QueuedConnection);
			}
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (s_youtube)
			s_youtube->disconnect();
		if (s_twitch)
			s_twitch->disconnect();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

#ifdef _WIN32
	ensureHtmlFileInAppData(L"overlay.html");
	ensureHtmlFileInAppData(L"tts.html");
	ensureHtmlFileInAppData(L"debug.html");
	ensureHtmlFileInAppData(L"effect.html");
	ensureHtmlFileInAppData(L"conversation_overlay.html");
#endif

	PluginConfig::instance().load();
	ViewerTtsSettings::instance(); // CSV から読み込み（ログに件数を出力）
	const auto &cfg = PluginConfig::instance();

	// エフェクトマネージャー
	s_effectManager = new EffectManager();
	QObject::connect(s_effectManager, &EffectManager::broadcastEffect,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });

	// ポイントマネージャー
	s_pointManager = new PointManager();
	QObject::connect(s_pointManager, &PointManager::broadcastJson,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });
	QObject::connect(s_pointManager, &PointManager::systemCommentRequested,
	                 [](const std::string &text) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(makeSystemCommentJson(text));
	                 });
	QObject::connect(s_pointManager, &PointManager::triggerEffectRequested,
	                 [](const QString &effectName) {
		                 if (s_effectManager)
			                 s_effectManager->onOlhEffect(effectName);
	                 });
	QObject::connect(s_pointManager, &PointManager::setModelRequested,
	                 [](const QString &userId, const QString &modelName) {
		                 QJsonObject obj;
		                 obj["userId"]    = userId;
		                 obj["modelName"] = modelName;
		                 handleResolveModel(obj);
	                 });
	QObject::connect(s_effectManager, &EffectManager::broadcastDebugStatus,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });
	QObject::connect(s_effectManager, &EffectManager::broadcastDebugLog,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });
	QObject::connect(s_pointManager, &PointManager::debugPointLog,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });
	QObject::connect(s_pointManager, &PointManager::debugPointUseLog,
	                 [](const std::string &json) {
		                 if (s_wsServer)
			                 s_wsServer->broadcast(json);
	                 });

	// ドック
	s_dock = new CommentDock();
	obs_frontend_add_dock_by_id("obs-live-hub-comment-dock", "コメントビューワー", s_dock);

	// X投稿ドック
	s_xPostDock = new XPostDock();
	obs_frontend_add_dock_by_id("obs-live-hub-x-post-dock", "X投稿", s_xPostDock);
	QObject::connect(s_xPostDock, &XPostDock::postRequested,
	                 [](const QString &text) { doXPostTweet(text); });

	// WebSocket サーバー起動
	s_wsServer = new WsServer(static_cast<uint16_t>(cfg.wsPort));
	applyWsCallbacks(s_wsServer);
	if (!s_wsServer->start()) {
		obs_log(LOG_WARNING, "WsServer: Failed to start on port %d", cfg.wsPort);
		delete s_wsServer;
		s_wsServer = nullptr;
	}

	// YouTube（Qt シグナル直結、配信開始で connect()）
	s_youtube = new YouTubePlatform(QString::fromStdString(cfg.youtubeApiKey),
					QString::fromStdString(cfg.youtubeBroadcastId),
					QString::fromStdString(cfg.youtubeAccessToken),
					cfg.youtubeIgnoreQuota);
	connectYouTubeSignals();

	// Twitch（EventBus 経由）
	s_twitch = new TwitchPlatform(s_eventBus, QString::fromStdString(cfg.twitchOAuthToken),
				      QString::fromStdString(cfg.twitchUsername),
				      QString::fromStdString(cfg.twitchChannel),
				      QString::fromStdString(cfg.twitchClientId));

	connectTwitchSignals();

	// EventBus → CommentDock + WebSocket ブロードキャスト (Twitch)
	s_twitchSubId = s_eventBus.subscribe([](const CommentEvent &ev) {
		if (s_dock)
			QMetaObject::invokeMethod(s_dock, "onCommentReceived", Qt::QueuedConnection,
						  Q_ARG(QString, QString::fromStdString(ev.authorName)),
						  Q_ARG(QString, QString::fromStdString(ev.message)));
		if (s_wsServer)
			s_wsServer->broadcast(makeCommentJson(
				ev.authorName, ev.message, "twitch", ev.avatarUrl,
				buildCommentTtsJson(QString::fromStdString(ev.authorName),
				                    QStringLiteral("twitch"))));
		const QString msg        = QString::fromStdString(ev.message);
		const QString authorName = QString::fromStdString(ev.authorName);
		recordUserPlatform(authorName, QStringLiteral("twitch"));
		processVoteComment(authorName, msg);
		if (s_effectManager) {
			s_effectManager->onComment(msg, authorName);
			processEffectOlhCommand(msg, authorName);
		}
		if (s_pointManager) {
			s_pointManager->onComment(authorName, QStringLiteral("twitch"));
			processPointOlhCommand(authorName, QStringLiteral("twitch"), authorName, msg);
		}
	});

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	// サブメニュー挿入位置を確保するマーカー（FINISHED_LOADING で buildObsLiveHubMenu が置換）
	obs_frontend_add_tools_menu_item(kHubMarker, obsHubMenuPlaceholder, nullptr);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

	EngineManager::stopAll();
	AivisEngine::stop(); // safety net（EngineManager 管理外の旧インスタンス用）

	s_eventBus.unsubscribe(s_twitchSubId);

	delete s_effectManager;
	s_effectManager = nullptr;

	delete s_pointManager;
	s_pointManager = nullptr;

	delete s_twitch;
	s_twitch = nullptr;

	delete s_youtube;
	s_youtube = nullptr;

	if (s_wsServer) {
		s_wsServer->stop();
		delete s_wsServer;
		s_wsServer = nullptr;
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
