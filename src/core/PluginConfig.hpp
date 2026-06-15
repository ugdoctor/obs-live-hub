#pragma once
#include <cstdint>
#include <string>

// プラグイン全体で共有する設定。シングルトン。
// obs_data_t のラップは PluginConfig.cpp で実装。
class PluginConfig {
public:
	static PluginConfig &instance()
	{
		static PluginConfig cfg;
		return cfg;
	}

	void load();
	void save();

	// YouTube
	std::string youtubeApiKey;
	std::string youtubeBroadcastId;
	// OAuth 認証情報（Google Cloud Console で作成した OAuth 2.0 クライアント）
	std::string youtubeClientId;
	std::string youtubeClientSecret;
	// トークン（OAuth フローで自動取得・自動更新）
	std::string youtubeRefreshToken;
	std::string youtubeAccessToken;
	int64_t youtubeTokenExpiry = 0; // UNIX タイムスタンプ（秒）。0 = 不明
	bool youtubeIgnoreQuota = false;

	// Twitch
	std::string twitchOAuthToken;
	std::string twitchUsername;
	std::string twitchChannel;
	std::string twitchClientId;

	// WebSocket
	int wsPort = 8765;

	// オーバーレイ外観設定
	int overlayWidth  = 400;
	int overlayHeight = 0; // 0 = 100vh
	float bgOpacity = 0.85f;
	std::string bgColor = "#121212";
	float cardOpacity = 0.90f;
	std::string cardBgColor = "#1e1e1e";
	std::string cardBorderColor = "#ffffff";
	float cardBorderOpacity = 0.08f;
	std::string usernameColor = "#e0e0e0";
	std::string textColor = "#ffffff";
	int fontSize = 14;
	int iconSize = 36;
	int maxComments = 20;

	// TTS 設定
	bool ttsEnabled = false;
	float ttsVolume = 1.0f;
	float ttsRate = 1.0f;
	float ttsPitch = 1.0f;
	std::string ttsVoice;
	bool ttsReadUsername = true;
	int ttsMaxLength = 100;
	bool ttsTwitch = true;
	bool ttsYoutube = true;

	// アンケートパネル外観設定
	std::string voteBgColor      = "#000000";
	float       voteBgOpacity    = 0.88f;
	std::string voteQuestionColor = "#ffffff";
	std::string voteHintColor    = "#aaaaaa";
	std::string voteBarColor      = "#4488ff";
	std::string voteBarBgColor    = "#333333";
	std::string voteTotalColor    = "#888888";
	std::string voteStatusColor   = "#44ff44";

	// アンケートパネル フォントサイズ
	int voteQuestionSize = 15;
	int voteHintSize     = 12;
	int voteResultSize   = 12;
	int voteTotalSize    = 11;
	int voteStatusSize   = 11;

	// 配信一括設定（StreamSettingsDialog）
	std::string streamTwitchTitle;
	std::string streamTwitchGameId;
	std::string streamTwitchGameName;
	std::string streamTwitchTags;
	std::string streamTwitchLanguage = "ja";
	std::string streamYoutubeTitle;
	std::string streamYoutubeDescription;
	std::string streamYoutubeCategoryId = "20";
	std::string streamYoutubePrivacy = "public";

private:
	PluginConfig() = default;
};
