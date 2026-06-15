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

	// AivisSpeech
	obs_data_set_string(data, "tts_engine",          ttsEngine.c_str());
	obs_data_set_string(data, "aivis_url",            aivisUrl.c_str());
	obs_data_set_string(data, "aivis_speaker_uuid",   aivisSpeakerUuid.c_str());
	obs_data_set_int   (data, "aivis_style_id",       aivisStyleId);
	obs_data_set_string(data, "aivis_style_name",     aivisStyleName.c_str());
	obs_data_set_string(data, "aivis_speaker_name",   aivisSpeakerName.c_str());
	obs_data_set_string(data, "aivis_engine_path",    aivisEnginePath.c_str());
	obs_data_set_bool  (data, "aivis_auto_start",     aivisAutoStart);

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
