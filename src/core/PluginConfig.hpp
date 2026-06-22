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
	bool ttsCheckEngineConnection = true;

	// VOICEVOX互換エンジン設定
	// ttsEngine: webspeech / aivisspeech / sharevox / lmroid / itvoice
	std::string ttsEngine = "webspeech";
	// エンジン別URL（デフォルトポートを設定済み）
	std::string aivisUrl    = "http://localhost:10101"; // AivisSpeech
	std::string sharevoxUrl = "http://localhost:50025"; // SHAREVOX
	std::string lmroidUrl   = "http://localhost:49973"; // LMROID
	std::string itvoiceUrl  = "http://localhost:49540"; // ITVOICE
	// 話者・スタイル（全VOICEVOX互換エンジン共通）
	std::string aivisSpeakerUuid;
	int64_t aivisStyleId = 0;
	std::string aivisStyleName;
	std::string aivisSpeakerName;

	// エンジン起動管理（エンジン別）
	std::string aivisEnginePath;
	bool aivisAutoStart       = false;
	std::string sharevoxEnginePath;
	bool sharevoxAutoStart    = false;
	std::string lmroidEnginePath;
	bool lmroidAutoStart      = false;
	std::string itvoiceEnginePath;
	bool itvoiceAutoStart     = false;

	// エンジン有効化フラグ（複数エンジン同時管理用）
	// webspeech は常時有効のためフラグ不要
	bool aivisspeechEnabled = false;
	bool sharevoxEnabled    = false;
	bool lmroidEnabled      = false;
	bool itvoiceEnabled     = false;
	bool bouyomiEnabled     = false;
	bool voiceroidEnabled   = false;

	// 現在選択中エンジンのURL
	std::string activeVoicevoxUrl() const
	{
		if (ttsEngine == "sharevox") return sharevoxUrl;
		if (ttsEngine == "lmroid")   return lmroidUrl;
		if (ttsEngine == "itvoice")  return itvoiceUrl;
		return aivisUrl;
	}
	// VOICEVOX互換エンジン選択中か
	bool isVoicevoxCompatible() const
	{
		return ttsEngine == "aivisspeech" || ttsEngine == "sharevox" ||
		       ttsEngine == "lmroid"      || ttsEngine == "itvoice";
	}

	// 棒読みちゃん設定
	std::string bouyomiHost     = "localhost";
	int         bouyomiPort     = 50080;
	int         bouyomiVoice    = 0; // -1=前回と同じ, 0=自動, 1-10=各声
	std::string bouyomiExePath;      // 実行ファイルパス（自動起動用）
	bool        bouyomiAutoStart = false;
	// [olh] bouyomi_* パラメータ上下限
	int bouyomiVolumeMin = 0;
	int bouyomiVolumeMax = 100;
	int bouyomiSpeedMin  = 50;
	int bouyomiSpeedMax  = 300;
	int bouyomiToneMin   = -100;
	int bouyomiToneMax   = 100;

	// VOICEROID（AssistantSeika）設定
	std::string voiceroidHost     = "localhost";
	int         voiceroidPort     = 7180;
	int         voiceroidCid      = 0;  // 話者番号（AssistantSeika の「設定2」タブで確認）
	std::string voiceroidUsername;       // AssistantSeika BASIC 認証ユーザー名
	std::string voiceroidPassword;       // AssistantSeika BASIC 認証パスワード

	// VOICEVOX互換エンジン [olh] コマンド パラメータ上下限
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

	// エフェクト設定
	std::string effectDefaultPosition = "center";
	std::string effectDefaultSize     = "medium";
	int effectMaxConcurrent = 3;
	int effectMaxQueue      = 10;

	// ポイントシステム設定
	bool pointEnabled           = true;
	int pointCommentAmount      = 1;  // コメント1回あたりの付与量
	int pointWatchInterval      = 5;  // 視聴ポイント付与間隔（分）
	int pointWatchAmount        = 1;  // 視聴ポイント付与量
	int pointCommentCooldown    = 10; // コメントポイント加算クールダウン（秒、0=無効）
	int pointUseCooldown        = 5;  // point_use実行クールダウン（秒、0=無効）

	// デバッグパネル表示設定
	bool debugShowConnection     = true;
	bool debugShowTts            = true;
	bool debugShowQuota          = true;
	bool debugShowVote           = true;
	bool debugShowLog            = true;
	bool debugShowCommentDetail  = true;
	bool debugShowEffect         = true;
	bool debugShowPoint          = true;

	// 会話風オーバーレイ設定
	int conversationMaxBubbles = 3;           // 1〜5
	std::string conversationZigzagMode = "alternate"; // "alternate" or "fixed"
	int conversationHorizontalOffset = 60;    // 対向側の余白px（0=全幅、大きいほど内側に寄る）

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
