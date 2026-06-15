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
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QWidget>

#include "core/EventBus.hpp"
#include "core/PlatformInterface.hpp"
#include "core/PluginConfig.hpp"
#include "modules/AivisEngine.hpp"
#include "modules/AivisStyleCache.hpp"
#include "modules/WsServer.hpp"
#include "platforms/TwitchPlatform.hpp"
#include "platforms/YouTubePlatform.hpp"
#include "ui/CommentDock.hpp"
#include "ui/OverlayStyleDialog.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/StreamSettingsDialog.hpp"
#include "ui/TtsDictionaryDialog.hpp"
#include "ui/VoteManagerDialog.hpp"
#include "ui/TtsSpeechDialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static CommentDock *s_dock = nullptr;
static YouTubePlatform *s_youtube = nullptr;
static TwitchPlatform *s_twitch = nullptr;
static WsServer *s_wsServer = nullptr;

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
				   const std::string &platform, const std::string &avatar = "")
{
	return "{\"type\":\"comment\","
	       "\"user\":\"" + escapeJsonString(user) + "\","
	       "\"text\":\"" + escapeJsonString(text) + "\","
	       "\"platform\":\"" + platform + "\","
	       "\"avatar\":\"" + escapeJsonString(avatar) + "\"}";
}

static std::string makeSystemCommentJson(const std::string &text)
{
	return "{\"type\":\"comment\","
	       "\"user\":\"obs-live-hub\","
	       "\"text\":\"" + escapeJsonString(text) + "\","
	       "\"platform\":\"system\","
	       "\"avatar\":\"\"}";
}

static void connectTwitchSignals();   // forward declaration
static void handleWsClientMessage(const QString &json); // forward declaration

static void applyWsCallbacks(WsServer *srv)
{
	if (!srv)
		return;
	srv->setMessageCallback([](const std::string &json) {
		const QString js = QString::fromStdString(json);
		QMetaObject::invokeMethod(qApp, [js]() { handleWsClientMessage(js); },
		                          Qt::QueuedConnection);
	});
}

static void restartWsServer()
{
	if (s_wsServer) {
		s_wsServer->stop();
		delete s_wsServer;
		s_wsServer = nullptr;
	}

	const int port = PluginConfig::instance().wsPort;
	s_wsServer = new WsServer(static_cast<uint16_t>(port));
	applyWsCallbacks(s_wsServer);
	if (!s_wsServer->start()) {
		obs_log(LOG_WARNING, "WsServer: Failed to start on port %d", port);
		delete s_wsServer;
		s_wsServer = nullptr;
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
			if (s_wsServer)
				s_wsServer->broadcast(makeCommentJson(author.toStdString(),
								      message.toStdString(), "youtube",
								      avatarUrl.toStdString()));
			processVoteComment(author, message);
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
	       "\"aivisUrl\":\"" + cfg.aivisUrl + "\"," +
	       "\"aivisStyleId\":" + std::to_string(cfg.aivisStyleId) + "}";
}

static void handleOlhEngineRequest(const QJsonObject &obj)
{
	const QString userId = obj["userId"].toString();
	const QString engine = obj["engine"].toString();

	QJsonObject res;
	res["type"]   = QStringLiteral("resolve_engine_result");
	res["userId"] = userId;

	if (engine != "webspeech" && engine != "aivisspeech") {
		res["ok"]    = false;
		res["error"] = QStringLiteral("不明なエンジン: ") + engine;
	} else {
		res["ok"]     = true;
		res["engine"] = engine;
	}

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());
}

static void handleResolveModel(const QJsonObject &obj)
{
	const QString userId    = obj["userId"].toString();
	const QString modelName = obj["modelName"].toString();

	QJsonObject res;
	res["type"]   = QStringLiteral("resolve_model_result");
	res["userId"] = userId;

	if (AivisStyleCache::isEmpty()) {
		res["ok"]    = false;
		res["error"] = QStringLiteral(
			"音声モデル一覧が未取得です。読み上げ設定を開いて更新してください。");
	} else {
		const AivisCachedStyle *style = AivisStyleCache::findByName(modelName);
		if (!style) {
			res["ok"]    = false;
			res["error"] = QStringLiteral("モデルが見つかりません: ") + modelName;
		} else {
			res["ok"]          = true;
			res["styleId"]     = static_cast<qint64>(style->styleId);
			res["speakerName"] = style->speakerName;
			res["styleName"]   = style->styleName;
		}
	}

	if (s_wsServer)
		s_wsServer->broadcast(
			QJsonDocument(res).toJson(QJsonDocument::Compact).toStdString());
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

static void onTtsSpeechMenuClick(void * /* data */)
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	TtsSpeechDialog dlg(mainWindow);
	if (dlg.exec() == QDialog::Accepted) {
		if (s_wsServer)
			s_wsServer->broadcast(makeTtsJson());
	}
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

	if (PluginConfig::instance().ttsEngine == "aivisspeech") {
		const QString chromePath = findChromePath();
		if (chromePath.isEmpty()) {
			auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
			QMessageBox::warning(
				mainWindow, "Chromeが見つかりません",
				"Chromeが見つかりません。\n"
				"AivisSpeech使用時はChromeが必要です。\n"
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
		obs_log(LOG_INFO, "OBS finished loading — auto-connecting Twitch...");
		reconnectTwitch();
		broadcastTtsDict();
		// AivisSpeech Engine 自動起動
		if (PluginConfig::instance().aivisAutoStart &&
		    !PluginConfig::instance().aivisEnginePath.empty()) {
			obs_log(LOG_INFO, "AivisEngine: auto-starting...");
			AivisEngine::start(
				QString::fromStdString(PluginConfig::instance().aivisEnginePath));
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (s_youtube)
			s_youtube->connect();
		if (s_twitch)
			s_twitch->connect();
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
#endif

	PluginConfig::instance().load();
	const auto &cfg = PluginConfig::instance();

	// ドック
	s_dock = new CommentDock();
	obs_frontend_add_dock_by_id("obs-live-hub-comment-dock", "コメントビューワー", s_dock);

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
			s_wsServer->broadcast(
				makeCommentJson(ev.authorName, ev.message, "twitch", ev.avatarUrl));
		processVoteComment(QString::fromStdString(ev.authorName),
				   QString::fromStdString(ev.message));
	});

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub 設定", onSettingsMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub 配信一括設定", onStreamSettingsMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub アンケート管理", onVoteManagerMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub オーバーレイ外観設定", onOverlayStyleMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub 読み上げ設定", onTtsSpeechMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub 読み上げ辞書", onTtsDictionaryMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub オーバーレイをブラウザで開く", onOpenOverlayMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub TTS音声ページを開く", onOpenTtsMenuClick, nullptr);
	obs_frontend_add_tools_menu_item("obs-live-hub リロード", onReloadMenuClick, nullptr);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

	AivisEngine::stop();

	s_eventBus.unsubscribe(s_twitchSubId);

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
