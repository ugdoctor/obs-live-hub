# STATUS.md

## 現在の状態

- **v0.3.0 リリース済み（2026-06-19）**
- 主要機能（TTS複数エンジン、棒読みちゃん、視聴者コマンド、ポイント、エフェクト等）は安定動作を確認済み

---

## 未確認・保留中のタスク（優先度順）

### 1. ttsCheckEngineConnection=true に戻してのテスト

- **経緯:** `[olh] engine:xxx` 問題の根本修正後、本来の安全設計（接続チェック有効）に戻して問題ないか未確認のまま v0.3.0 をリリースした
- **確認方法:** 設定ダイアログで「エンジン接続チェックを有効にする」を ON に戻し、有効化済みエンジンへの `[olh] engine:xxx` が正しく動作するか確認

### 2. マルチユーザーテスト

- 複数の異なる視聴者が同時に異なるエンジンを指定するケース（例: AivisSpeech と 棒読みちゃんを同時に指定した2人が同時コメント）
- ホスト単独テスト環境のため後日実施

### 3. AivisSpeech CORS エラー対応の最終確認

- 「OBS起動時に自動起動」を ON にした状態で `--allow_origin "*"` が正しく付与されて起動されるか確認
- 手動起動時は `--allow_origin "*"` を自分で付ける必要あり（設定ダイアログの「起動」ボタン経由なら自動付与される）

---

## 最近完了した対策（参照用）

### 初心者向け導入ガイド作成（2026-06-25）
- `GETTING_STARTED.md` を新規作成。ダウンロード〜Twitch接続・TTS・YouTube・X投稿・FAQ を網羅
- `CLAUDE.md` に「初心者向けガイドは `GETTING_STARTED.md` を参照」の一文を追記

### X投稿機能 一連の実装・実機検証完了（2026-06-24）
API投稿・手動投稿・テンプレート・Twitch/YouTubeリンク自動構築のすべてが実機で動作確認済み。
- X API 投稿（OAuth 1.0a / WinHTTP）: `POST OK: HTTP 201` 確認
- X 手動投稿（Web Intent）: ブラウザで `x.com/intent/post` が開き投稿完了
- Twitch/YouTubeリンクのテンプレート保存 → ダイアログ初期反映: 動作確認済み
- `broadcastResolved` シグナル経由の YouTube URL 動的更新: 動作確認済み
  - `isGuiThread=true` / ラベル更新正常 / チェック状態維持を確認
  - 配信開始後 15〜30秒で `lifeCycleStatus: live` → URL取得というタイムラグは仕様通り

### XManualPostDialog broadcastResolved 診断ログ追加（2026-06-24）
- `broadcastResolved` ラムダ内に以下3点のログを追加:
  1. シグナル受信確認 + `isGuiThread`（`QThread::currentThread() == QApplication::instance()->thread()`）
  2. URL更新直前（newUrl と現在のチェック状態）
  3. UI更新完了確認（チェック状態が変わっていないことを記録）
- これにより「シグナル未着」「スレッド不整合」「ダイアログ破棄済み（受信なし）」を
  ログだけで判別可能

### 自動投稿設定ダイアログのメニュー追加・STREAMING_STARTEDログ追加（2026-06-24）
- `ツール → obs-live-hub → X投稿 → 配信開始時の自動投稿設定` メニュー項目を追加
  - クリックするとラジオボタン3択ダイアログ（オフ/API投稿確認/手動投稿）が開く
  - OK時: PluginConfig に保存 + `s_xPostDock->refresh()` でドック側 UI と同期
  - ドックパネルが非表示でも設定変更可能
- `STREAMING_STARTED` ハンドラに `xAutoPostMode=N (...)` のログ出力を追加
  - ログから「設定がオフだったのか」「処理が動かなかったのか」を即座に判別できる
- **調査結果:** `obs_frontend_add_dock_by_id()` でのデフォルト表示制御: OBS公式APIなし。
  `QDockWidget::show()` を直接呼ぶと毎回強制表示されてしまうため非推奨。
  ドック非表示の問題はメニューからの設定アクセスで対処済み。

### XTemplate へのリンクチェック設定追加（2026-06-24）
- `XTemplate` 構造体に `includeTwitchLink` / `includeYoutubeLink`（bool）を追加
- `PluginConfig.cpp` の load/save に `include_twitch_link` / `include_youtube_link` キーを追加（後方互換: 既存テンプレートはデフォルト false）
- `XTemplateSettingsDialog` の右ペインに「Twitchリンクを含める」「YouTubeリンクを含める」チェックボックスを追加
- `XManualPostDialog::onTemplateChanged()` でテンプレートのチェック値を Twitch/YouTube チェックボックスの初期値として反映
- `broadcastResolved` シグナル受信時はラベルのみ更新し、チェック状態は変更しない（テンプレート由来の初期値を維持）
- **実機確認済み（2026-06-24）**

### XManualPostDialog YouTube チェックボックス挙動修正（2026-06-24）
- 「取得中」状態でもチェックボックスを有効化し、配信前から先行チェックできるように変更
- `broadcastResolved` 受信時はラベルのみ更新し、チェック状態（ON/OFF）を変更しないように修正
- 「ブラウザで投稿」押下時に YouTube チェック済みかつ `youtubeUrl_` が空（未取得）の場合はブロックし案内を表示
- **実機確認済み（2026-06-24）**

### YouTube 動的 URL 取得対応（2026-06-24 完了）
- `YouTubePlatform` に `resolvedBroadcastId_` フィールド・`currentBroadcastId()` getter・`broadcastResolved(QString)` シグナルを追加
- `fetchActiveBroadcast()` で `item.id`（動画ID）を取得して保存 → `broadcastResolved` emit
- `fetchVideoInfo()` で `broadcastId_`（手動指定ID）を `resolvedBroadcastId_` に保存 → `broadcastResolved` emit
- `XManualPostDialog` を `YouTubePlatform*` 引数に対応:
  - 即座に ID が取得済み → 即時 URL 表示
  - 未取得 → 「取得中...」＋チェック可能（先行チェック対応）＋`broadcastResolved` に接続（ダイアログ破棄で自動切断）
  - YouTube 未接続 → config の固定IDにフォールバック
- `plugin-main.cpp` の両呼び出し箇所（メニュー・STREAMING_STARTED）で `s_youtube` を渡すように変更

### X 手動投稿機能 修正（2026-06-24）
- `xAutoPostOnStreamStart` を `bool` → `int`（0=オフ, 1=API投稿, 2=手動投稿）に変更
  - 後方互換マイグレーション実装済み（旧 bool キーを読んで int に変換）
- `XPostDock` のチェックボックス → ラジオボタン3択に変更
  （「自動表示しない」「API投稿確認ダイアログを表示」「手動投稿ダイアログを表示」）
- `OBS_FRONTEND_EVENT_STREAMING_STARTED` ハンドラで mode=1 なら `XPostConfirmDialog`、mode=2 なら `XManualPostDialog` を起動
- `XManualPostDialog` に Twitch/YouTube チェックボックスを追加
  - Twitch: `PluginConfig::twitchChannel` から URL 自動構築。設定済みなら有効化
  - YouTube: `PluginConfig::youtubeBroadcastId` が実 ID の場合のみ有効化。"me"/空は無効化（ツールチップで理由説明）

### X 手動投稿機能実装（2026-06-24 完了）
- **新規追加:** `XManualPostDialog`（X API を一切使わない Web Intent 方式）
- テンプレート選択（既存 `XTemplate` を流用）→ 本文・リンクURL・画像を編集 → ブラウザで `x.com/intent/post` を開く
- 画像は `QPixmap` でクリップボードにコピー（貼り付け案内付き）
- テキストは `QUrl::toPercentEncoding` で UTF-8 パーセントエンコード
- メニュー: `ツール → obs-live-hub → X投稿 → X手動投稿`（既存の API テスト・設定メニューとは区分けしてセパレータ挿入）
- **実機確認済み（2026-06-24）**

### X(Twitter) 投稿機能実装（2026-06-24 完了）
- **新規追加:** `XClient`（OAuth 1.0a / BCrypt HMAC-SHA1 / WinHTTP HTTPS POST /2/tweets）
- `PluginConfig` に `XTemplate` struct・x* フィールド（xApiKey / xApiSecret / xAccessToken / xAccessTokenSecret / xAutoPostOnStreamStart / xDefaultTemplateIndex / xTemplates）を追加
- `XAccountSettingsDialog`（API認証情報入力、マスク表示）
- `XTemplateSettingsDialog`（テンプレート管理：追加/削除/並び替え/編集）
- `XPostDock`（OBS ドックパネル、常時表示可能）
- `XPostConfirmDialog`（配信開始時の自動投稿確認）
- `obs_frontend_add_dock_by_id("obs-live-hub-x-post-dock", ...)` でドック登録
- `OBS_FRONTEND_EVENT_STREAMING_STARTED` 時に `xAutoPostOnStreamStart` が true なら確認ダイアログを表示
- **スコープ外（Phase 2）:** 画像添付・リンクURL自動生成・テンプレートプレースホルダー（`imagePath` フィールドは JSON 構造体に確保済み）
- **実機確認済み（2026-06-24）: `POST OK: HTTP 201` 確認**

### VOICEROID（AssistantSeika）TTSエンジン対応（2026-06-22 完了）
- **新規追加:** `VoiceroidClient`（POST /PLAY2/<cid> + JSON + BASIC認証）
- `PluginConfig` に voiceroid 設定フィールドを追加（host/port/cid/username/password/enabled）
- `EngineManager` に voiceroid ping チェック（GET /VERSION）を追加
- `tts.html` に `voiceroid` エンジン分岐を追加（`voiceroid_speak` WebSocket メッセージ）
- `plugin-main.cpp` に `handleVoiceroidSpeakRequest()` を追加
- `TtsSpeechDialog` に「VOICEROID（AssistantSeika）」設定グループを追加（host/port/cid/認証情報）
- **注意:** 接続確認は `GET /VERSION` エンドポイントで実施。今後の実機テストで動作確認が必要

### 棒読みちゃん実動作確認・関連対策（2026-06-22 完了）
- **動作確認:** `speak OK` + 音声再生を確認済み
- `handleBouyomiSpeakRequest` / `BouyomiChanClient::talk()` にログ追加（受信・HTTP送信・成功/失敗を追跡可能に）
- 接続確認を常時 Connected の偽実装 → 実 HTTP ping 方式に修正
- 自動起動機能を実装（読み上げ設定ダイアログ → 棒読みちゃんセクションに「実行ファイルパス + 自動起動チェック」を追加）

### WsServer ゾンビソケット対策（2026-06-21 完了）
- **問題:** OBSクラッシュ後、ポート8765のLISTENソケットがOSレベルで残存し、新しいWsServerへの接続が古いゾンビソケット側に奪われる現象
- **根本原因:** `SO_REUSEADDR` はWindows上で複数ソケットの同時LISTENを許容するため、ゾンビとの「サイレントな共存」が発生していた
- **対策:** `SO_REUSEADDR` → `SO_EXCLUSIVEADDRUSE` に変更。ゾンビソケットが存在する場合は `bind()` が即座に失敗してログに明示される
- **連携:** 接続診断ダイアログで「バインド失敗」状態を表示できるよう `ListenState` 列挙型を追加
- **残課題:** ゾンビソケット自体を解消する手段はなく、PCの再起動が必要な場合がある。今回の改善点は「サイレントな接続横取り → 即座な失敗検知」への変化

---

## 既知の設計上の保留事項（将来検討）

- **OBSクラッシュ→再起動時の音声エンジン再接続ロジック**
  - 既存起動済みプロセスへの再接続の挙動が未設計
- **多言語対応**
  - メニュー/UI文字列のみ対象（HTML は日本語のまま）
- **gh CLI の正式な認証設定**
  - 現状は Windows の資格情報マネージャー経由で動作しているが、次回リリース時に問題が出る可能性あり
  - その場合は `gh auth login --web` を再試行

---

## ファイル運用ルール

- **STATUS.md**: 現在のタスクのみ記載。肥大化したら定期的に整理
- **CLAUDE_LOG.md**: 追記専用の開発履歴（通常は参照不要）
- **CLAUDE.md の「視聴者コメントコマンド全集」セクション**: コマンド追加・変更・削除時は必ず同時に更新すること
