#pragma once
#include <string>

class XClient {
public:
	struct PostResult {
		bool        ok         = false;
		int         httpStatus = 0;
		std::string responseBody;
		std::string tweetId; // 成功時：投稿された tweet ID
	};

	struct VerifyResult {
		bool        ok          = false;
		int         httpStatus  = 0;
		std::string username;    // 成功時：@handle
		std::string displayName; // 成功時：表示名
		std::string errorDesc;   // 失敗時：エラー説明（APIキー等の値は含まない）
	};

	// X API v2 POST /2/tweets でテキスト投稿（OAuth 1.0a）
	// バックグラウンドスレッドから呼ぶこと。APIキー等はログに出さない。
	static PostResult postTweet(
		const std::string &apiKey,
		const std::string &apiSecret,
		const std::string &accessToken,
		const std::string &accessTokenSecret,
		const std::string &text);

	// X API v2 GET /2/users/me で認証情報の有効性を確認（投稿は発生しない）
	// バックグラウンドスレッドから呼ぶこと。
	static VerifyResult verifyCredentials(
		const std::string &apiKey,
		const std::string &apiSecret,
		const std::string &accessToken,
		const std::string &accessTokenSecret);
};
