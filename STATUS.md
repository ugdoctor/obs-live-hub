## 直前の作業（2026-06-17 更新）

## デプロイ状況（最新）

| ファイル | 場所 | 状態 |
|---|---|---|
| `obs-live-hub.dll` | `C:\Program Files\obs-studio\obs-plugins\64bit\` | ✅ 最新版 |
| `overlay.html` | `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub\` | ✅ 最新版 |
| `tts.html` | `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub\` | ✅ 最新版 |
| `debug.html` | `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub\` | ✅ 最新版 |
| APPDATA各html | `%APPDATA%\obs-studio\plugins\obs-live-hub\` | OBS起動時に自動コピー |

deploy.ps1: `cd D:\git\obs_plugin_commentViewer\obs-comment-viewer && .\deploy.ps1`

## GitHub
- リポジトリ: https://github.com/ugdoctor/obs-live-hub (Public公開済み)
- v0.1.0 リリース済み（DLL添付）
- v0.2.0 リリース済み（2026-06-17）

## 実装済み機能（全機能）

### コメント表示
- Twitch IRC over TLS (WinSock2 + SChannel)、IRCv3タグからdisplay-name/user-id抽出
- Helix API (WinHTTP)でアバターURL取得・キャッシュ
- YouTube Live `liveBroadcasts.list?broadcastStatus=active&mine=true` でliveChatId自動取得
- `liveChatMessages.list` ポーリング（設定可能間隔5〜120秒）
- overlay.html: コメントカード表示、エラーバナー（下部赤バナー）

### TTS / overlay 分離
- `overlay.html`: コメント表示専用
- `tts.html`: 音声再生専用（Chromeで開く・クリックで有効化）
- AivisSpeech使用時: `--disable-web-security` オプション付きChromeで起動
- 読み上げ設定ダイアログの「ユーザー名を読み上げる」チェックボックスでON/OFF制御（デフォルトON）
- YouTube コメントのユーザー名先頭 `@` は読み上げ時に自動除去（表示名は変更なし）
- 新規クライアント接続時に TTS設定・辞書・デバッグ設定を自動送信（設定の同期）

### AivisSpeech対応（実装済み）
- ローカルエンジン: `http://localhost:10101`（VOICEVOX互換API）
- OBS読み上げ設定ダイアログにエンジン切り替え・話者選択・自動起動設定を追加
- エンジン実行ファイルパス設定（デフォルト: `%LOCALAPPDATA%\Programs\AivisSpeech\AivisSpeech-Engine\run.exe`）
- `--allow_origin "*"` オプション付きで起動
- Web Speech APIとAivisSpeechの切り替え可能
- AivisSpeech使用時はChromeの `--disable-web-security` で起動必須

### TTS辞書機能
- OBSツールメニュー「obs-live-hub 読み上げ辞書」から管理
- 部分一致・大文字小文字区別なし全置換
- ユーザー名・コメントテキスト両方に適用（WebSpeech/AivisSpeech共通）
- 保存: `%APPDATA%\obs-studio\plugins\obs-live-hub\tts_dictionary.csv`
- broadcast形式: `{"type":"tts_dict","entries":[{"from":"...","to":"..."}]}`

### 視聴者コマンド（[olh]）
書式: `[olh] キー:値, キー:値, ...`

| キー | 説明 | 制限 |
|---|---|---|
| `color` | コメント文字色 | 輝度0.15未満NG |
| `name` | ユーザー名色 | 輝度0.15未満NG |
| `panel` | カード背景色 | 輝度0.05〜0.5のみ |
| `engine` | TTSエンジン切り替え | webspeech / aivis |
| `model` | AivisSpeech音声モデル指定 | 部分一致検索。切り替え時に aivis_* パラメータをリセット |
| `webs_rate` | Web Speech API 速度 | 0.7〜1.5 |
| `webs_pitch` | Web Speech API ピッチ | 0.7〜1.3 |
| `webs_volume` | Web Speech API 音量 | 0.0〜1.0 |
| `aivis_speed` | AivisSpeech 話速 (speedScale) | デフォルト 1.0。上下限: OBSメニューで設定 |
| `aivis_pitch` | AivisSpeech 音高 (pitchScale) | デフォルト 0.0。上下限: OBSメニューで設定 |
| `aivis_intonation` | AivisSpeech 抑揚 (intonationScale) | デフォルト 1.0。上下限: OBSメニューで設定 |
| `aivis_volume` | AivisSpeech 音量倍率 (volumeScale) | デフォルト 1.0。上下限: OBSメニューで設定 |
| `aivis_emotion` | AivisSpeech 感情表現強さ (tempoDynamicsScale) | デフォルト 1.0。上下限: OBSメニューで設定 |
| `reset` | 全設定をデフォルトに戻す | 書式: `[olh] reset` |

- コマンド行はカード表示・音声読み上げ対象外
- エラー時は「{user}さんへ：{エラー内容}」形式でsystemコメントとして表示
- systemコメント: overlay.htmlでグレー・イタリック表示、tts.htmlで読み上げ対象外
- localStorage永続化（キー: olhUserSettings）

### アンケート機能（VoteManagerDialog）
- OBSツールメニュー「obs-live-hub アンケート管理」
- 選択肢形式: `!vote A` / `!vote B` / `!vote C`
- 自由記述形式: `!vote ラーメン`
- 1人1票（同ユーザー上書き）、Twitch/YouTube両対応
- overlay.htmlにアンケートパネル表示（vote_start/vote_update/vote_end）
- VoteManagerDialogで結果リアルタイム表示

### 配信一括設定（StreamSettingsDialog）
- OBSツールメニュー「obs-live-hub 配信一括設定」
- Twitch: タイトル・カテゴリ検索・タグ・言語
- YouTube: タイトル・説明文・カテゴリ・公開設定
- OKボタンで即時API更新（WinHTTP PATCH）
- Twitchスコープ: `chat:read chat:edit channel:manage:broadcast user:edit:broadcast`
- YouTubeスコープ: `https://www.googleapis.com/auth/youtube`

### YouTube クォータ管理設定
- コメント取得間隔: 設定ダイアログで5〜120秒に変更可能（デフォルト5秒）
- クォータ消費目安表示（5ユニット/回で計算）
- PluginConfig保存キー: `youtube_poll_interval`

### デバッグ表示（debug.html）
- OBSツールメニュー「obs-live-hub デバッグ表示を開く」でブラウザ起動
- 「obs-live-hub デバッグ設定」で各パネルの表示/非表示を管理
- 表示パネル: 接続状態・TTS設定・クォータ・投票状態・broadcastログ・最新コメント詳細
- 最新コメント詳細: 原文・辞書置換後・テキスト表示設定・音声再生設定・[olh]ユーザー設定
- `%APPDATA%\obs-studio\plugins\obs-live-hub\debug.html` に自動コピー

### OBS ツールメニュー（最新）
サブメニュー構造（ツール → obs-live-hub → 各サブメニュー）

| サブメニュー | 項目 | 動作 |
|---|---|---|
| 接続・設定 | 設定 | APIキー・接続設定ダイアログ |
| 接続・設定 | リロード | 設定再読み込み＋再接続 |
| 接続・設定 | 配信一括設定 | StreamSettingsDialog（Twitch/YouTube配信設定） |
| オーバーレイ | オーバーレイ外観設定 | 色・フォント・幅・アンケート外観等 |
| オーバーレイ | オーバーレイをブラウザで開く | overlay.htmlをChrome起動 |
| オーバーレイ | TTS音声ページを開く | tts.htmlをChrome起動（AivisSpeech時は--disable-web-security付き） |
| 読み上げ | 読み上げ設定 | TTS設定・AivisSpeechエンジン管理 |
| 読み上げ | 読み上げ辞書 | 辞書登録・CSV管理 |
| 読み上げ | AivisSpeechモデル制限 | [olh]コマンドの各パラメータ上下限設定（AivisParamLimitDialog） |
| アンケート | アンケート管理 | VoteManagerDialog |
| デバッグ | デバッグ設定 | DebugSettingsDialog |
| デバッグ | デバッグ表示を開く | debug.htmlをブラウザ起動 |

### OAuth認証
- Twitch: ポート80でローカルHTTPサーバー、implicit flow、スコープ拡張済み
- YouTube: ポート8766でGoogleOAuthCallbackServer、authorization code flow
- 両プラットフォームともOBS再起動時に自動接続

### エラーコード
| コード | 原因 |
|---|---|
| TWITCH_AUTH_FAILED | OAuthトークン無効 |
| TWITCH_CONNECTION_LOST | 接続切断・再接続中 |
| YOUTUBE_AUTH_FAILED | OAuth 401エラー |
| YOUTUBE_QUOTA_EXCEEDED | クォータ超過 |

### 多プラットフォーム配信
- obs-multi-rtmp v0.7.4.0をインストール済み（OBS 32.x対応版）
- Twitch・YouTubeへの同時配信設定済み

## 現在の未解決問題（優先対応）

### 問題3: YouTube OAuth 401エラー
**状況:** リロード時にYouTube liveBroadcasts.list HTTP 401
→ アクセストークン期限切れ（1時間）、リフレッシュトークンで自動更新が必要
**対策:** refreshTokenを使ったトークン自動更新処理の実装
（配信中に401が出た場合に自動でrefresh→再試行）

## 次の予定（v0.2.0リリース完了・v0.3.0に向けて）
1. YouTube OAuth自動リフレッシュ実装（トークン期限切れ対応）
2. debug.html背景の修正
3. その他バグ修正・機能追加
4. 動作確認後、git commit・push・v0.2.0リリース公開