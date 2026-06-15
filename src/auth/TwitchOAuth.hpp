#pragma once
#include <functional>
#include <string>

// Twitch OAuth 2.0 Implicit Grant フロー用コールバックサーバー（純粋 TCP）。
// ポート 8767 でブラウザからのリダイレクトを 2 ステップで受け取る。
//
// Step1: GET /  → フラグメント (#access_token=...) をクエリに変換する JS ページを返す
// Step2: GET /callback2?access_token=xxx → トークンを抽出して onToken を呼ぶ
//
// onToken は detach スレッドから呼ばれるため、UI 更新には QMetaObject::invokeMethod を使うこと。
//
// 事前準備: Twitch Developer Console (https://dev.twitch.tv/console) で
// アプリのリダイレクト URI に http://localhost （ポートなし）を追加すること。
//
// ポート選択: 80 を最初に試み、失敗したら 8767 を試みる。
// 両方失敗した場合は onToken を呼ばず onError を呼ぶ。
class TwitchOAuthCallbackServer {
public:
	static void startAsync(std::function<void(const std::string &token)> onToken,
			       std::function<void(const std::string &error)> onError = nullptr,
			       int timeoutSec = 300);
};
