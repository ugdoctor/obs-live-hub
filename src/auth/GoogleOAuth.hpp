#pragma once
#include <cstdint>
#include <functional>
#include <string>

// Google OAuth 2.0 コールバック受信用の一時的 HTTP サーバー（純粋 TCP）。
// ポート 8766 でブラウザからのリダイレクトを受け取り authorization code を取得する。
// onCode は detach スレッドから呼ばれるため、UI 更新には QMetaObject::invokeMethod を使うこと。
class GoogleOAuthCallbackServer {
public:
	static void startAsync(std::function<void(const std::string &code)> onCode,
			       int port = 8766, int timeoutSec = 300);
};

// WinHTTP を使ったトークン交換結果（Qt TLS に依存しない）。
struct GoogleTokenResult {
	bool ok = false;
	std::string accessToken;
	std::string refreshToken; // authorization_code 交換時のみ
	int64_t expiresIn = 3600; // 秒
	std::string error;        // エラーメッセージ（ok=false 時）
};

// WinHTTP + Windows SChannel で Google OAuth トークンエンドポイントを呼ぶ。
// QNetworkAccessManager / Qt TLS を使わないため OBS プラグイン環境でも動作する。
class GoogleOAuthTokenClient {
public:
	// authorization code → access_token + refresh_token
	static void exchangeCodeAsync(const std::string &clientId,
				      const std::string &clientSecret,
				      const std::string &code,
				      const std::string &redirectUri,
				      std::function<void(GoogleTokenResult)> onResult);

	// refresh_token → 新しい access_token
	static void refreshAccessTokenAsync(const std::string &clientId,
					    const std::string &clientSecret,
					    const std::string &refreshToken,
					    std::function<void(GoogleTokenResult)> onResult);
};
