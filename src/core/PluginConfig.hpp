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
	int youtubePollInterval = 5; // コメント取得間隔（秒）

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

	// AivisSpeech 設定
	std::string ttsEngine = "webspeech"; // "webspeech" or "aivisspeech"
	std::string aivisUrl = "http://localhost:10101";
	std::string aivisSpeakerUuid;
	int64_t aivisStyleId = 0;
	std::string aivisStyleName;
	std::string aivisSpeakerName;

	// AivisSpeech Engine 起動管理
	std::string aivisEnginePath;
	bool aivisAutoStart = false;

	// AivisSpeech [olh] コマンド パラメータ上下限
	float aivisSpeedMin        =  0.5f;
	float aivisSpeedMax        =  2.0f;
	float aivisPitchMin        = -0.15f;
	float aivisPitchMax        =  0.15f;
	float aivisIntonationMin   =  0.0f;
	float aivisIntonationMax   =  2.0f;
	float aivisVolumeScaleMin  =  0.0f;
	float aivisVolumeScaleMax  =  2.0f;
	float aivisEmotionMin      =  0.0f;
	float aivisEmotionMax      =  2.0f;

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

	// デバッグパネル表示設定
	bool debugShowConnection     = true;
	bool debugShowTts            = true;
	bool debugShowQuota          = true;
	bool debugShowVote           = true;
	bool debugShowLog            = true;
	bool debugShowCommentDetail  = true;

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
