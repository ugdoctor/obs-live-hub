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

#include "PluginConfig.hpp"

#include <obs-module.h>
#include <util/platform.h>

static const char *CONFIG_FILENAME = "config.json";

void PluginConfig::load()
{
	char *path = obs_module_config_path(CONFIG_FILENAME);
	if (!path)
		return;

	obs_data_t *data = obs_data_create_from_json_file(path);
	bfree(path);

	if (!data)
		return;

	// YouTube
	youtubeApiKey = obs_data_get_string(data, "youtube_api_key");
	youtubeBroadcastId = obs_data_get_string(data, "youtube_broadcast_id");
	youtubeClientId = obs_data_get_string(data, "youtube_client_id");
	youtubeClientSecret = obs_data_get_string(data, "youtube_client_secret");
	youtubeRefreshToken = obs_data_get_string(data, "youtube_refresh_token");
	youtubeAccessToken = obs_data_get_string(data, "youtube_access_token");
	// 旧フィールド移行: youtube_oauth_token → youtube_access_token
	if (youtubeAccessToken.empty()) {
		const char *legacy = obs_data_get_string(data, "youtube_oauth_token");
		if (legacy && *legacy)
			youtubeAccessToken = legacy;
	}
	if (obs_data_has_user_value(data, "youtube_token_expiry"))
		youtubeTokenExpiry =
			static_cast<int64_t>(obs_data_get_int(data, "youtube_token_expiry"));
	if (obs_data_has_user_value(data, "youtube_ignore_quota"))
		youtubeIgnoreQuota = obs_data_get_bool(data, "youtube_ignore_quota");
	if (obs_data_has_user_value(data, "youtube_poll_interval")) {
		youtubePollInterval = static_cast<int>(obs_data_get_int(data, "youtube_poll_interval"));
		if (youtubePollInterval < 5 || youtubePollInterval > 120)
			youtubePollInterval = 5;
	}

	// Twitch
	twitchOAuthToken = obs_data_get_string(data, "twitch_oauth_token");
	twitchUsername = obs_data_get_string(data, "twitch_username");
	twitchChannel = obs_data_get_string(data, "twitch_channel");
	twitchClientId = obs_data_get_string(data, "twitch_client_id");

	// WebSocket
	wsPort = static_cast<int>(obs_data_get_int(data, "ws_port"));
	if (wsPort <= 0 || wsPort > 65535)
		wsPort = 8765;

	// オーバーレイ外観設定（キーが存在する場合のみ上書き）
	if (obs_data_has_user_value(data, "overlay_width")) {
		overlayWidth = static_cast<int>(obs_data_get_int(data, "overlay_width"));
		if (overlayWidth < 200 || overlayWidth > 1920)
			overlayWidth = 400;
	}
	if (obs_data_has_user_value(data, "overlay_height")) {
		overlayHeight = static_cast<int>(obs_data_get_int(data, "overlay_height"));
		if (overlayHeight < 0 || overlayHeight > 2160)
			overlayHeight = 0;
	}
	if (obs_data_has_user_value(data, "bg_opacity"))
		bgOpacity = static_cast<float>(obs_data_get_double(data, "bg_opacity"));
	if (obs_data_has_user_value(data, "bg_color")) {
		const char *s = obs_data_get_string(data, "bg_color");
		if (s && *s)
			bgColor = s;
	}
	if (obs_data_has_user_value(data, "card_opacity"))
		cardOpacity = static_cast<float>(obs_data_get_double(data, "card_opacity"));
	if (obs_data_has_user_value(data, "card_bg_color")) {
		const char *s = obs_data_get_string(data, "card_bg_color");
		if (s && *s)
			cardBgColor = s;
	}
	if (obs_data_has_user_value(data, "card_border_color")) {
		const char *s = obs_data_get_string(data, "card_border_color");
		if (s && *s)
			cardBorderColor = s;
	}
	if (obs_data_has_user_value(data, "card_border_opacity"))
		cardBorderOpacity =
			static_cast<float>(obs_data_get_double(data, "card_border_opacity"));
	if (obs_data_has_user_value(data, "username_color")) {
		const char *s = obs_data_get_string(data, "username_color");
		if (s && *s)
			usernameColor = s;
	}
	if (obs_data_has_user_value(data, "text_color")) {
		const char *s = obs_data_get_string(data, "text_color");
		if (s && *s)
			textColor = s;
	}
	if (obs_data_has_user_value(data, "font_size")) {
		fontSize = static_cast<int>(obs_data_get_int(data, "font_size"));
		if (fontSize < 8 || fontSize > 48)
			fontSize = 14;
	}
	if (obs_data_has_user_value(data, "icon_size")) {
		iconSize = static_cast<int>(obs_data_get_int(data, "icon_size"));
		if (iconSize < 24 || iconSize > 64)
			iconSize = 36;
	}
	if (obs_data_has_user_value(data, "max_comments")) {
		maxComments = static_cast<int>(obs_data_get_int(data, "max_comments"));
		if (maxComments < 5 || maxComments > 50)
			maxComments = 20;
	}

	// アンケートパネル外観設定
	if (obs_data_has_user_value(data, "vote_bg_color")) {
		const char *s = obs_data_get_string(data, "vote_bg_color");
		if (s && *s) voteBgColor = s;
	}
	if (obs_data_has_user_value(data, "vote_bg_opacity"))
		voteBgOpacity = static_cast<float>(obs_data_get_double(data, "vote_bg_opacity"));
	if (obs_data_has_user_value(data, "vote_question_color")) {
		const char *s = obs_data_get_string(data, "vote_question_color");
		if (s && *s) voteQuestionColor = s;
	}
	if (obs_data_has_user_value(data, "vote_hint_color")) {
		const char *s = obs_data_get_string(data, "vote_hint_color");
		if (s && *s) voteHintColor = s;
	}
	if (obs_data_has_user_value(data, "vote_bar_color")) {
		const char *s = obs_data_get_string(data, "vote_bar_color");
		if (s && *s) voteBarColor = s;
	}
	if (obs_data_has_user_value(data, "vote_bar_bg_color")) {
		const char *s = obs_data_get_string(data, "vote_bar_bg_color");
		if (s && *s) voteBarBgColor = s;
	}
	if (obs_data_has_user_value(data, "vote_total_color")) {
		const char *s = obs_data_get_string(data, "vote_total_color");
		if (s && *s) voteTotalColor = s;
	}
	if (obs_data_has_user_value(data, "vote_status_color")) {
		const char *s = obs_data_get_string(data, "vote_status_color");
		if (s && *s) voteStatusColor = s;
	}
	if (obs_data_has_user_value(data, "vote_question_size")) {
		voteQuestionSize = static_cast<int>(obs_data_get_int(data, "vote_question_size"));
		if (voteQuestionSize < 8 || voteQuestionSize > 48) voteQuestionSize = 15;
	}
	if (obs_data_has_user_value(data, "vote_hint_size")) {
		voteHintSize = static_cast<int>(obs_data_get_int(data, "vote_hint_size"));
		if (voteHintSize < 8 || voteHintSize > 48) voteHintSize = 12;
	}
	if (obs_data_has_user_value(data, "vote_result_size")) {
		voteResultSize = static_cast<int>(obs_data_get_int(data, "vote_result_size"));
		if (voteResultSize < 8 || voteResultSize > 48) voteResultSize = 12;
	}
	if (obs_data_has_user_value(data, "vote_total_size")) {
		voteTotalSize = static_cast<int>(obs_data_get_int(data, "vote_total_size"));
		if (voteTotalSize < 8 || voteTotalSize > 48) voteTotalSize = 11;
	}
	if (obs_data_has_user_value(data, "vote_status_size")) {
		voteStatusSize = static_cast<int>(obs_data_get_int(data, "vote_status_size"));
		if (voteStatusSize < 8 || voteStatusSize > 48) voteStatusSize = 11;
	}

	// TTS
	if (obs_data_has_user_value(data, "tts_enabled"))
		ttsEnabled = obs_data_get_bool(data, "tts_enabled");
	if (obs_data_has_user_value(data, "tts_volume"))
		ttsVolume = static_cast<float>(obs_data_get_double(data, "tts_volume"));
	if (obs_data_has_user_value(data, "tts_rate"))
		ttsRate = static_cast<float>(obs_data_get_double(data, "tts_rate"));
	if (obs_data_has_user_value(data, "tts_pitch"))
		ttsPitch = static_cast<float>(obs_data_get_double(data, "tts_pitch"));
	if (obs_data_has_user_value(data, "tts_voice")) {
		const char *sv = obs_data_get_string(data, "tts_voice");
		if (sv)
			ttsVoice = sv;
	}
	if (obs_data_has_user_value(data, "tts_read_username"))
		ttsReadUsername = obs_data_get_bool(data, "tts_read_username");
	if (obs_data_has_user_value(data, "tts_max_length"))
		ttsMaxLength = static_cast<int>(obs_data_get_int(data, "tts_max_length"));
	if (obs_data_has_user_value(data, "tts_twitch"))
		ttsTwitch = obs_data_get_bool(data, "tts_twitch");
	if (obs_data_has_user_value(data, "tts_youtube"))
		ttsYoutube = obs_data_get_bool(data, "tts_youtube");
	if (obs_data_has_user_value(data, "tts_check_engine_connection"))
		ttsCheckEngineConnection = obs_data_get_bool(data, "tts_check_engine_connection");

	// AivisSpeech
	if (obs_data_has_user_value(data, "tts_engine")) {
		const char *s = obs_data_get_string(data, "tts_engine");
		if (s && *s) ttsEngine = s;
	}
	if (obs_data_has_user_value(data, "aivis_url")) {
		const char *s = obs_data_get_string(data, "aivis_url");
		if (s && *s) aivisUrl = s;
	}
	if (obs_data_has_user_value(data, "aivis_speaker_uuid")) {
		const char *s = obs_data_get_string(data, "aivis_speaker_uuid");
		if (s) aivisSpeakerUuid = s;
	}
	if (obs_data_has_user_value(data, "aivis_style_id"))
		aivisStyleId = obs_data_get_int(data, "aivis_style_id");
	if (obs_data_has_user_value(data, "aivis_style_name")) {
		const char *s = obs_data_get_string(data, "aivis_style_name");
		if (s) aivisStyleName = s;
	}
	if (obs_data_has_user_value(data, "aivis_speaker_name")) {
		const char *s = obs_data_get_string(data, "aivis_speaker_name");
		if (s) aivisSpeakerName = s;
	}
	if (obs_data_has_user_value(data, "aivis_engine_path")) {
		const char *s = obs_data_get_string(data, "aivis_engine_path");
		if (s) aivisEnginePath = s;
	}
	if (obs_data_has_user_value(data, "aivis_auto_start"))
		aivisAutoStart = obs_data_get_bool(data, "aivis_auto_start");

	// SHAREVOX
	if (obs_data_has_user_value(data, "sharevox_url")) {
		const char *s = obs_data_get_string(data, "sharevox_url");
		if (s && *s) sharevoxUrl = s;
	}
	if (obs_data_has_user_value(data, "sharevox_engine_path")) {
		const char *s = obs_data_get_string(data, "sharevox_engine_path");
		if (s) sharevoxEnginePath = s;
	}
	if (obs_data_has_user_value(data, "sharevox_auto_start"))
		sharevoxAutoStart = obs_data_get_bool(data, "sharevox_auto_start");

	// LMROID
	if (obs_data_has_user_value(data, "lmroid_url")) {
		const char *s = obs_data_get_string(data, "lmroid_url");
		if (s && *s) lmroidUrl = s;
	}
	if (obs_data_has_user_value(data, "lmroid_engine_path")) {
		const char *s = obs_data_get_string(data, "lmroid_engine_path");
		if (s) lmroidEnginePath = s;
	}
	if (obs_data_has_user_value(data, "lmroid_auto_start"))
		lmroidAutoStart = obs_data_get_bool(data, "lmroid_auto_start");

	// ITVOICE
	if (obs_data_has_user_value(data, "itvoice_url")) {
		const char *s = obs_data_get_string(data, "itvoice_url");
		if (s && *s) itvoiceUrl = s;
	}
	if (obs_data_has_user_value(data, "itvoice_engine_path")) {
		const char *s = obs_data_get_string(data, "itvoice_engine_path");
		if (s) itvoiceEnginePath = s;
	}
	if (obs_data_has_user_value(data, "itvoice_auto_start"))
		itvoiceAutoStart = obs_data_get_bool(data, "itvoice_auto_start");

	// エンジン有効化フラグ（キーが無い場合は ttsEngine からの自然な移行）
	if (obs_data_has_user_value(data, "aivisspeech_enabled"))
		aivisspeechEnabled = obs_data_get_bool(data, "aivisspeech_enabled");
	else
		aivisspeechEnabled = (ttsEngine == "aivisspeech");
	if (obs_data_has_user_value(data, "sharevox_enabled"))
		sharevoxEnabled = obs_data_get_bool(data, "sharevox_enabled");
	else
		sharevoxEnabled = (ttsEngine == "sharevox");
	if (obs_data_has_user_value(data, "lmroid_enabled"))
		lmroidEnabled = obs_data_get_bool(data, "lmroid_enabled");
	else
		lmroidEnabled = (ttsEngine == "lmroid");
	if (obs_data_has_user_value(data, "itvoice_enabled"))
		itvoiceEnabled = obs_data_get_bool(data, "itvoice_enabled");
	else
		itvoiceEnabled = (ttsEngine == "itvoice");
	if (obs_data_has_user_value(data, "bouyomi_enabled"))
		bouyomiEnabled = obs_data_get_bool(data, "bouyomi_enabled");
	else
		bouyomiEnabled = (ttsEngine == "bouyomi");
	if (obs_data_has_user_value(data, "voiceroid_enabled"))
		voiceroidEnabled = obs_data_get_bool(data, "voiceroid_enabled");
	else
		voiceroidEnabled = (ttsEngine == "voiceroid");

	// 棒読みちゃん設定
	if (obs_data_has_user_value(data, "bouyomi_host")) {
		const char *s = obs_data_get_string(data, "bouyomi_host");
		if (s && *s) bouyomiHost = s;
	}
	if (obs_data_has_user_value(data, "bouyomi_port")) {
		bouyomiPort = static_cast<int>(obs_data_get_int(data, "bouyomi_port"));
		if (bouyomiPort <= 0 || bouyomiPort > 65535) bouyomiPort = 50080;
	}
	if (obs_data_has_user_value(data, "bouyomi_voice")) {
		bouyomiVoice = static_cast<int>(obs_data_get_int(data, "bouyomi_voice"));
		if (bouyomiVoice < -1 || bouyomiVoice > 10000) bouyomiVoice = 0;
	}
	if (obs_data_has_user_value(data, "bouyomi_exe_path")) {
		const char *s = obs_data_get_string(data, "bouyomi_exe_path");
		if (s) bouyomiExePath = s;
	}
	if (obs_data_has_user_value(data, "bouyomi_auto_start"))
		bouyomiAutoStart = obs_data_get_bool(data, "bouyomi_auto_start");
	if (obs_data_has_user_value(data, "bouyomi_volume_min"))
		bouyomiVolumeMin = static_cast<int>(obs_data_get_int(data, "bouyomi_volume_min"));
	if (obs_data_has_user_value(data, "bouyomi_volume_max"))
		bouyomiVolumeMax = static_cast<int>(obs_data_get_int(data, "bouyomi_volume_max"));
	if (obs_data_has_user_value(data, "bouyomi_speed_min"))
		bouyomiSpeedMin = static_cast<int>(obs_data_get_int(data, "bouyomi_speed_min"));
	if (obs_data_has_user_value(data, "bouyomi_speed_max"))
		bouyomiSpeedMax = static_cast<int>(obs_data_get_int(data, "bouyomi_speed_max"));
	if (obs_data_has_user_value(data, "bouyomi_tone_min"))
		bouyomiToneMin = static_cast<int>(obs_data_get_int(data, "bouyomi_tone_min"));
	if (obs_data_has_user_value(data, "bouyomi_tone_max"))
		bouyomiToneMax = static_cast<int>(obs_data_get_int(data, "bouyomi_tone_max"));

	// VOICEROID（AssistantSeika）設定
	if (obs_data_has_user_value(data, "voiceroid_host")) {
		const char *s = obs_data_get_string(data, "voiceroid_host");
		if (s && *s) voiceroidHost = s;
	}
	if (obs_data_has_user_value(data, "voiceroid_port")) {
		voiceroidPort = static_cast<int>(obs_data_get_int(data, "voiceroid_port"));
		if (voiceroidPort <= 0 || voiceroidPort > 65535) voiceroidPort = 7180;
	}
	if (obs_data_has_user_value(data, "voiceroid_cid"))
		voiceroidCid = static_cast<int>(obs_data_get_int(data, "voiceroid_cid"));
	if (obs_data_has_user_value(data, "voiceroid_username")) {
		const char *s = obs_data_get_string(data, "voiceroid_username");
		if (s) voiceroidUsername = s;
	}
	if (obs_data_has_user_value(data, "voiceroid_password")) {
		const char *s = obs_data_get_string(data, "voiceroid_password");
		if (s) voiceroidPassword = s;
	}

	// AivisSpeech [olh] パラメータ上下限
	if (obs_data_has_user_value(data, "aivis_speed_min"))
		aivisSpeedMin = static_cast<float>(obs_data_get_double(data, "aivis_speed_min"));
	if (obs_data_has_user_value(data, "aivis_speed_max"))
		aivisSpeedMax = static_cast<float>(obs_data_get_double(data, "aivis_speed_max"));
	if (obs_data_has_user_value(data, "aivis_pitch_min"))
		aivisPitchMin = static_cast<float>(obs_data_get_double(data, "aivis_pitch_min"));
	if (obs_data_has_user_value(data, "aivis_pitch_max"))
		aivisPitchMax = static_cast<float>(obs_data_get_double(data, "aivis_pitch_max"));
	if (obs_data_has_user_value(data, "aivis_intonation_min"))
		aivisIntonationMin = static_cast<float>(obs_data_get_double(data, "aivis_intonation_min"));
	if (obs_data_has_user_value(data, "aivis_intonation_max"))
		aivisIntonationMax = static_cast<float>(obs_data_get_double(data, "aivis_intonation_max"));
	if (obs_data_has_user_value(data, "aivis_volume_scale_min"))
		aivisVolumeScaleMin = static_cast<float>(obs_data_get_double(data, "aivis_volume_scale_min"));
	if (obs_data_has_user_value(data, "aivis_volume_scale_max"))
		aivisVolumeScaleMax = static_cast<float>(obs_data_get_double(data, "aivis_volume_scale_max"));
	if (obs_data_has_user_value(data, "aivis_emotion_min"))
		aivisEmotionMin = static_cast<float>(obs_data_get_double(data, "aivis_emotion_min"));
	if (obs_data_has_user_value(data, "aivis_emotion_max"))
		aivisEmotionMax = static_cast<float>(obs_data_get_double(data, "aivis_emotion_max"));

	// ポイントシステム設定
	if (obs_data_has_user_value(data, "point_enabled"))
		pointEnabled = obs_data_get_bool(data, "point_enabled");
	if (obs_data_has_user_value(data, "point_comment_amount")) {
		pointCommentAmount = static_cast<int>(obs_data_get_int(data, "point_comment_amount"));
		if (pointCommentAmount < 0 || pointCommentAmount > 100)
			pointCommentAmount = 1;
	}
	if (obs_data_has_user_value(data, "point_watch_interval")) {
		pointWatchInterval = static_cast<int>(obs_data_get_int(data, "point_watch_interval"));
		if (pointWatchInterval < 1 || pointWatchInterval > 60)
			pointWatchInterval = 5;
	}
	if (obs_data_has_user_value(data, "point_watch_amount")) {
		pointWatchAmount = static_cast<int>(obs_data_get_int(data, "point_watch_amount"));
		if (pointWatchAmount < 0 || pointWatchAmount > 100)
			pointWatchAmount = 1;
	}
	if (obs_data_has_user_value(data, "point_comment_cooldown")) {
		pointCommentCooldown = static_cast<int>(obs_data_get_int(data, "point_comment_cooldown"));
		if (pointCommentCooldown < 0 || pointCommentCooldown > 300)
			pointCommentCooldown = 10;
	}
	if (obs_data_has_user_value(data, "point_use_cooldown")) {
		pointUseCooldown = static_cast<int>(obs_data_get_int(data, "point_use_cooldown"));
		if (pointUseCooldown < 0 || pointUseCooldown > 300)
			pointUseCooldown = 5;
	}

	// エフェクト設定
	if (obs_data_has_user_value(data, "effect_default_position")) {
		const char *s = obs_data_get_string(data, "effect_default_position");
		if (s && *s) effectDefaultPosition = s;
	}
	if (obs_data_has_user_value(data, "effect_default_size")) {
		const char *s = obs_data_get_string(data, "effect_default_size");
		if (s && *s) effectDefaultSize = s;
	}
	if (obs_data_has_user_value(data, "effect_max_concurrent")) {
		effectMaxConcurrent = static_cast<int>(obs_data_get_int(data, "effect_max_concurrent"));
		if (effectMaxConcurrent < 1 || effectMaxConcurrent > 10) effectMaxConcurrent = 3;
	}
	if (obs_data_has_user_value(data, "effect_max_queue")) {
		effectMaxQueue = static_cast<int>(obs_data_get_int(data, "effect_max_queue"));
		if (effectMaxQueue < 1 || effectMaxQueue > 50) effectMaxQueue = 10;
	}

	// デバッグパネル表示設定
	if (obs_data_has_user_value(data, "debug_show_connection"))
		debugShowConnection = obs_data_get_bool(data, "debug_show_connection");
	if (obs_data_has_user_value(data, "debug_show_tts"))
		debugShowTts = obs_data_get_bool(data, "debug_show_tts");
	if (obs_data_has_user_value(data, "debug_show_quota"))
		debugShowQuota = obs_data_get_bool(data, "debug_show_quota");
	if (obs_data_has_user_value(data, "debug_show_vote"))
		debugShowVote = obs_data_get_bool(data, "debug_show_vote");
	if (obs_data_has_user_value(data, "debug_show_log"))
		debugShowLog = obs_data_get_bool(data, "debug_show_log");
	if (obs_data_has_user_value(data, "debug_show_comment_detail"))
		debugShowCommentDetail = obs_data_get_bool(data, "debug_show_comment_detail");
	if (obs_data_has_user_value(data, "debug_show_effect"))
		debugShowEffect = obs_data_get_bool(data, "debug_show_effect");
	if (obs_data_has_user_value(data, "debug_show_point"))
		debugShowPoint = obs_data_get_bool(data, "debug_show_point");

	// 会話風オーバーレイ設定
	if (obs_data_has_user_value(data, "conversation_max_bubbles")) {
		conversationMaxBubbles = static_cast<int>(obs_data_get_int(data, "conversation_max_bubbles"));
		if (conversationMaxBubbles < 1 || conversationMaxBubbles > 5)
			conversationMaxBubbles = 3;
	}
	if (obs_data_has_user_value(data, "conversation_zigzag_mode")) {
		const char *s = obs_data_get_string(data, "conversation_zigzag_mode");
		if (s && *s)
			conversationZigzagMode = s;
	}
	if (obs_data_has_user_value(data, "conversation_horizontal_offset")) {
		conversationHorizontalOffset =
			static_cast<int>(obs_data_get_int(data, "conversation_horizontal_offset"));
		if (conversationHorizontalOffset < 0 || conversationHorizontalOffset > 300)
			conversationHorizontalOffset = 60;
	}

	// 配信一括設定
	if (obs_data_has_user_value(data, "stream_twitch_title")) {
		const char *s = obs_data_get_string(data, "stream_twitch_title");
		if (s) streamTwitchTitle = s;
	}
	if (obs_data_has_user_value(data, "stream_twitch_game_id")) {
		const char *s = obs_data_get_string(data, "stream_twitch_game_id");
		if (s) streamTwitchGameId = s;
	}
	if (obs_data_has_user_value(data, "stream_twitch_game_name")) {
		const char *s = obs_data_get_string(data, "stream_twitch_game_name");
		if (s) streamTwitchGameName = s;
	}
	if (obs_data_has_user_value(data, "stream_twitch_tags")) {
		const char *s = obs_data_get_string(data, "stream_twitch_tags");
		if (s) streamTwitchTags = s;
	}
	if (obs_data_has_user_value(data, "stream_twitch_language")) {
		const char *s = obs_data_get_string(data, "stream_twitch_language");
		if (s && *s) streamTwitchLanguage = s;
	}
	if (obs_data_has_user_value(data, "stream_youtube_title")) {
		const char *s = obs_data_get_string(data, "stream_youtube_title");
		if (s) streamYoutubeTitle = s;
	}
	if (obs_data_has_user_value(data, "stream_youtube_description")) {
		const char *s = obs_data_get_string(data, "stream_youtube_description");
		if (s) streamYoutubeDescription = s;
	}
	if (obs_data_has_user_value(data, "stream_youtube_category_id")) {
		const char *s = obs_data_get_string(data, "stream_youtube_category_id");
		if (s && *s) streamYoutubeCategoryId = s;
	}
	if (obs_data_has_user_value(data, "stream_youtube_privacy")) {
		const char *s = obs_data_get_string(data, "stream_youtube_privacy");
		if (s && *s) streamYoutubePrivacy = s;
	}

	obs_data_release(data);
}

void PluginConfig::save()
{
	// 設定ディレクトリが存在しない場合は作成する
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	char *path = obs_module_config_path(CONFIG_FILENAME);
	if (!path)
		return;

	obs_data_t *data = obs_data_create();

	// YouTube
	obs_data_set_string(data, "youtube_api_key", youtubeApiKey.c_str());
	obs_data_set_string(data, "youtube_broadcast_id", youtubeBroadcastId.c_str());
	obs_data_set_string(data, "youtube_client_id", youtubeClientId.c_str());
	obs_data_set_string(data, "youtube_client_secret", youtubeClientSecret.c_str());
	obs_data_set_string(data, "youtube_refresh_token", youtubeRefreshToken.c_str());
	obs_data_set_string(data, "youtube_access_token", youtubeAccessToken.c_str());
	obs_data_set_int(data, "youtube_token_expiry", static_cast<long long>(youtubeTokenExpiry));
	obs_data_set_bool(data, "youtube_ignore_quota", youtubeIgnoreQuota);
	obs_data_set_int(data, "youtube_poll_interval", youtubePollInterval);

	// Twitch
	obs_data_set_string(data, "twitch_oauth_token", twitchOAuthToken.c_str());
	obs_data_set_string(data, "twitch_username", twitchUsername.c_str());
	obs_data_set_string(data, "twitch_channel", twitchChannel.c_str());
	obs_data_set_string(data, "twitch_client_id", twitchClientId.c_str());

	// WebSocket
	obs_data_set_int(data, "ws_port", wsPort);

	// オーバーレイ外観設定
	obs_data_set_int(data, "overlay_width",  overlayWidth);
	obs_data_set_int(data, "overlay_height", overlayHeight);
	obs_data_set_double(data, "bg_opacity", static_cast<double>(bgOpacity));
	obs_data_set_string(data, "bg_color", bgColor.c_str());
	obs_data_set_double(data, "card_opacity", static_cast<double>(cardOpacity));
	obs_data_set_string(data, "card_bg_color", cardBgColor.c_str());
	obs_data_set_string(data, "card_border_color", cardBorderColor.c_str());
	obs_data_set_double(data, "card_border_opacity", static_cast<double>(cardBorderOpacity));
	obs_data_set_string(data, "username_color", usernameColor.c_str());
	obs_data_set_string(data, "text_color", textColor.c_str());
	obs_data_set_int(data, "font_size", fontSize);
	obs_data_set_int(data, "icon_size", iconSize);
	obs_data_set_int(data, "max_comments", maxComments);

	// アンケートパネル外観設定
	obs_data_set_string(data, "vote_bg_color",       voteBgColor.c_str());
	obs_data_set_double(data, "vote_bg_opacity",     static_cast<double>(voteBgOpacity));
	obs_data_set_string(data, "vote_question_color", voteQuestionColor.c_str());
	obs_data_set_string(data, "vote_hint_color",     voteHintColor.c_str());
	obs_data_set_string(data, "vote_bar_color",      voteBarColor.c_str());
	obs_data_set_string(data, "vote_bar_bg_color",   voteBarBgColor.c_str());
	obs_data_set_string(data, "vote_total_color",    voteTotalColor.c_str());
	obs_data_set_string(data, "vote_status_color",   voteStatusColor.c_str());
	obs_data_set_int(data, "vote_question_size", voteQuestionSize);
	obs_data_set_int(data, "vote_hint_size",     voteHintSize);
	obs_data_set_int(data, "vote_result_size",   voteResultSize);
	obs_data_set_int(data, "vote_total_size",    voteTotalSize);
	obs_data_set_int(data, "vote_status_size",   voteStatusSize);

	// TTS
	obs_data_set_bool(data, "tts_enabled", ttsEnabled);
	obs_data_set_double(data, "tts_volume", static_cast<double>(ttsVolume));
	obs_data_set_double(data, "tts_rate", static_cast<double>(ttsRate));
	obs_data_set_double(data, "tts_pitch", static_cast<double>(ttsPitch));
	obs_data_set_string(data, "tts_voice", ttsVoice.c_str());
	obs_data_set_bool(data, "tts_read_username", ttsReadUsername);
	obs_data_set_int(data, "tts_max_length", ttsMaxLength);
	obs_data_set_bool(data, "tts_twitch", ttsTwitch);
	obs_data_set_bool(data, "tts_youtube", ttsYoutube);
	obs_data_set_bool(data, "tts_check_engine_connection", ttsCheckEngineConnection);

	// AivisSpeech
	obs_data_set_string(data, "tts_engine",          ttsEngine.c_str());
	obs_data_set_string(data, "aivis_url",            aivisUrl.c_str());
	obs_data_set_string(data, "aivis_speaker_uuid",   aivisSpeakerUuid.c_str());
	obs_data_set_int   (data, "aivis_style_id",       aivisStyleId);
	obs_data_set_string(data, "aivis_style_name",     aivisStyleName.c_str());
	obs_data_set_string(data, "aivis_speaker_name",   aivisSpeakerName.c_str());
	obs_data_set_string(data, "aivis_engine_path",    aivisEnginePath.c_str());
	obs_data_set_bool  (data, "aivis_auto_start",     aivisAutoStart);
	obs_data_set_string(data, "sharevox_url",          sharevoxUrl.c_str());
	obs_data_set_string(data, "sharevox_engine_path",  sharevoxEnginePath.c_str());
	obs_data_set_bool  (data, "sharevox_auto_start",   sharevoxAutoStart);
	obs_data_set_string(data, "lmroid_url",            lmroidUrl.c_str());
	obs_data_set_string(data, "lmroid_engine_path",    lmroidEnginePath.c_str());
	obs_data_set_bool  (data, "lmroid_auto_start",     lmroidAutoStart);
	obs_data_set_string(data, "itvoice_url",           itvoiceUrl.c_str());
	obs_data_set_string(data, "itvoice_engine_path",   itvoiceEnginePath.c_str());
	obs_data_set_bool  (data, "itvoice_auto_start",    itvoiceAutoStart);
	obs_data_set_bool  (data, "aivisspeech_enabled",   aivisspeechEnabled);
	obs_data_set_bool  (data, "sharevox_enabled",      sharevoxEnabled);
	obs_data_set_bool  (data, "lmroid_enabled",        lmroidEnabled);
	obs_data_set_bool  (data, "itvoice_enabled",       itvoiceEnabled);
	obs_data_set_bool  (data, "bouyomi_enabled",       bouyomiEnabled);
	obs_data_set_bool  (data, "voiceroid_enabled",     voiceroidEnabled);

	// 棒読みちゃん設定
	obs_data_set_string(data, "bouyomi_host",        bouyomiHost.c_str());
	obs_data_set_int   (data, "bouyomi_port",        bouyomiPort);
	obs_data_set_int   (data, "bouyomi_voice",       bouyomiVoice);
	obs_data_set_string(data, "bouyomi_exe_path",    bouyomiExePath.c_str());
	obs_data_set_bool  (data, "bouyomi_auto_start",  bouyomiAutoStart);
	obs_data_set_int   (data, "bouyomi_volume_min",  bouyomiVolumeMin);
	obs_data_set_int   (data, "bouyomi_volume_max",  bouyomiVolumeMax);
	obs_data_set_int   (data, "bouyomi_speed_min",   bouyomiSpeedMin);
	obs_data_set_int   (data, "bouyomi_speed_max",   bouyomiSpeedMax);
	obs_data_set_int   (data, "bouyomi_tone_min",    bouyomiToneMin);
	obs_data_set_int   (data, "bouyomi_tone_max",    bouyomiToneMax);

	// VOICEROID（AssistantSeika）設定
	obs_data_set_string(data, "voiceroid_host",      voiceroidHost.c_str());
	obs_data_set_int   (data, "voiceroid_port",      voiceroidPort);
	obs_data_set_int   (data, "voiceroid_cid",       voiceroidCid);
	obs_data_set_string(data, "voiceroid_username",  voiceroidUsername.c_str());
	obs_data_set_string(data, "voiceroid_password",  voiceroidPassword.c_str());

	// AivisSpeech [olh] パラメータ上下限
	obs_data_set_double(data, "aivis_speed_min",        static_cast<double>(aivisSpeedMin));
	obs_data_set_double(data, "aivis_speed_max",        static_cast<double>(aivisSpeedMax));
	obs_data_set_double(data, "aivis_pitch_min",        static_cast<double>(aivisPitchMin));
	obs_data_set_double(data, "aivis_pitch_max",        static_cast<double>(aivisPitchMax));
	obs_data_set_double(data, "aivis_intonation_min",   static_cast<double>(aivisIntonationMin));
	obs_data_set_double(data, "aivis_intonation_max",   static_cast<double>(aivisIntonationMax));
	obs_data_set_double(data, "aivis_volume_scale_min", static_cast<double>(aivisVolumeScaleMin));
	obs_data_set_double(data, "aivis_volume_scale_max", static_cast<double>(aivisVolumeScaleMax));
	obs_data_set_double(data, "aivis_emotion_min",      static_cast<double>(aivisEmotionMin));
	obs_data_set_double(data, "aivis_emotion_max",      static_cast<double>(aivisEmotionMax));

	// ポイントシステム設定
	obs_data_set_bool(data, "point_enabled",           pointEnabled);
	obs_data_set_int (data, "point_comment_amount",    pointCommentAmount);
	obs_data_set_int (data, "point_watch_interval",    pointWatchInterval);
	obs_data_set_int (data, "point_watch_amount",      pointWatchAmount);
	obs_data_set_int (data, "point_comment_cooldown",  pointCommentCooldown);
	obs_data_set_int (data, "point_use_cooldown",      pointUseCooldown);

	// エフェクト設定
	obs_data_set_string(data, "effect_default_position", effectDefaultPosition.c_str());
	obs_data_set_string(data, "effect_default_size",     effectDefaultSize.c_str());
	obs_data_set_int   (data, "effect_max_concurrent",   effectMaxConcurrent);
	obs_data_set_int   (data, "effect_max_queue",        effectMaxQueue);

	// デバッグパネル表示設定
	obs_data_set_bool(data, "debug_show_connection", debugShowConnection);
	obs_data_set_bool(data, "debug_show_tts",        debugShowTts);
	obs_data_set_bool(data, "debug_show_quota",      debugShowQuota);
	obs_data_set_bool(data, "debug_show_vote",       debugShowVote);
	obs_data_set_bool(data, "debug_show_log",            debugShowLog);
	obs_data_set_bool(data, "debug_show_comment_detail", debugShowCommentDetail);
	obs_data_set_bool(data, "debug_show_effect",         debugShowEffect);
	obs_data_set_bool(data, "debug_show_point",          debugShowPoint);

	// 会話風オーバーレイ設定
	obs_data_set_int   (data, "conversation_max_bubbles",       conversationMaxBubbles);
	obs_data_set_string(data, "conversation_zigzag_mode",       conversationZigzagMode.c_str());
	obs_data_set_int   (data, "conversation_horizontal_offset", conversationHorizontalOffset);

	// 配信一括設定
	obs_data_set_string(data, "stream_twitch_title",    streamTwitchTitle.c_str());
	obs_data_set_string(data, "stream_twitch_game_id",  streamTwitchGameId.c_str());
	obs_data_set_string(data, "stream_twitch_game_name", streamTwitchGameName.c_str());
	obs_data_set_string(data, "stream_twitch_tags",     streamTwitchTags.c_str());
	obs_data_set_string(data, "stream_twitch_language", streamTwitchLanguage.c_str());
	obs_data_set_string(data, "stream_youtube_title",        streamYoutubeTitle.c_str());
	obs_data_set_string(data, "stream_youtube_description",  streamYoutubeDescription.c_str());
	obs_data_set_string(data, "stream_youtube_category_id",  streamYoutubeCategoryId.c_str());
	obs_data_set_string(data, "stream_youtube_privacy",      streamYoutubePrivacy.c_str());

	obs_data_save_json_safe(data, path, "tmp", "bak");

	obs_data_release(data);
	bfree(path);
}
