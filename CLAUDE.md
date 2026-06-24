# OBS Plugin: obs-live-hub

## プロジェクト概要
OBS Studio用の拡張可能なライブ配信支援プラグイン。
コメントビューワーを起点に、AIフィルタリング・配信情報表示・外部ツール連携などへの拡張を想定した設計。

## 技術スタック
- 言語: C++ (C++17)
- ビルドシステム: CMake 4.3.3
- UIフレームワーク: Qt6（OBSに同梱）
- OBS API: obs-studio 31.1.1
- コンパイラ: MSVC 19.51 (Visual Studio 2026)
- 対象OS: Windows（まずはWindows優先）

## ビルド方法
```bash
# Developer PowerShell for VS で実行
cd D:\git\obs_plugin_commentViewer\obs-comment-viewer

# --- プリセット使用（推奨） ---
cmake --preset windows-x64
cmake --build --preset windows-x64
# 生成物: build_x64/RelWithDebInfo/obs-live-hub.dll

# --- 手動指定（代替） ---
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
# 生成物: build/RelWithDebInfo/obs-live-hub.dll
```

## プロジェクト構成
```
obs-comment-viewer/
├── CLAUDE.md               # このファイル（仕様・方針）
├── STATUS.md               # 現在のタスク・次にやること
├── CLAUDE_LOG.md           # 開発履歴（追記のみ）
├── CMakeLists.txt          # ビルド設定
├── CMakePresets.json       # CMake プリセット（windows-x64 等）
├── buildspec.json          # OBS バージョン指定
├── config.json.example     # 設定ファイルサンプル（本番は config.json にリネーム）
├── data/                   # デプロイ HTML リソース（起動時に %APPDATA% へ自動コピー）
│   ├── locale/en-US.ini
│   ├── overlay.html        # コメントオーバーレイ ★実装参照はこちら
│   ├── tts.html            # TTS 読み上げ用
│   ├── effect.html         # エフェクト表示用
│   └── debug.html          # デバッグ用
└── src/
    ├── plugin-main.cpp         # エントリーポイント・WebSocket ハンドラ群
    ├── plugin-main.c           # OBS モジュール宣言スタブ（テンプレート由来）
    ├── core/
    │   ├── EventBus.hpp        # モジュール間 Pub/Sub イベント通信
    │   ├── PluginConfig.hpp/cpp # 全設定フィールド定義・JSON 永続化
    │   └── PlatformInterface.hpp # プラットフォーム抽象インターフェース
    ├── auth/                   # OAuth 認証フロー
    │   ├── GoogleOAuth.hpp/cpp  # Google OAuth 2.0（YouTube 用）
    │   └── TwitchOAuth.hpp/cpp  # Twitch OAuth Implicit Grant
    ├── platforms/              # 配信プラットフォーム実装
    │   ├── TlsSocket.hpp/cpp       # 純粋 TLS/TCP ソケット（Twitch IRC 用）
    │   ├── TwitchPlatform.hpp/cpp  # Twitch IRC 接続・コメント取得
    │   └── YouTubePlatform.hpp/cpp # YouTube Live API ポーリング
    ├── modules/                # 機能モジュール
    │   ├── AivisEngine.hpp/cpp     # VOICEVOX 互換 HTTP API ラッパー
    │   ├── AivisStyleCache.hpp/cpp # 話者一覧キャッシュ（エンジン → スタイル名解決）
    │   ├── BouyomiChanClient.hpp/cpp # 棒読みちゃん連携クライアント
    │   ├── VoiceroidClient.hpp/cpp  # AssistantSeika HTTP API クライアント（VOICEROID連携）
    │   ├── EffectManager.hpp/cpp   # エフェクト設定管理・発火制御
    │   ├── EngineManager.hpp/cpp   # TTS エンジンライフサイクル管理（起動/接続確認）
    │   ├── PointManager.hpp/cpp    # ポイントシステム管理・CSV 永続化
    │   ├── ViewerTtsSettings.hpp/cpp # 視聴者別 TTS 設定・CSV 永続化
    │   └── WsServer.hpp/cpp        # WebSocket サーバー（overlay.html / tts.html 連携）
    ├── ui/                     # Qt6 UI コンポーネント
    │   ├── AivisParamLimitDialog.hpp/cpp  # VOICEVOX 互換パラメータ上下限設定
    │   ├── BouyomiParamLimitDialog.hpp/cpp # 棒読みちゃんパラメータ上下限設定
    │   ├── CommentDock.hpp/cpp            # コメントビューワードック
    │   ├── DebugSettingsDialog.hpp/cpp    # デバッグ設定
    │   ├── EffectSettingsDialog.hpp/cpp   # エフェクト設定
    │   ├── OverlayStyleDialog.hpp/cpp     # オーバーレイ外観カスタマイズ
    │   ├── OverlayUtils.hpp/cpp           # overlay.html パス解決ユーティリティ
    │   ├── PointSettingsDialog.hpp/cpp    # ポイントシステム設定
    │   ├── SettingsDialog.hpp/cpp         # 接続設定・OAuth 認証フロー
    │   ├── StreamSettingsDialog.hpp/cpp   # 配信情報一括設定（タイトル・カテゴリ等）
    │   ├── TtsDictionaryDialog.hpp/cpp    # TTS 読み上げ辞書（CSV 管理）
    │   ├── TtsSpeechDialog.hpp/cpp        # TTS エンジン設定・接続確認
    │   └── VoteManagerDialog.hpp/cpp      # アンケート（投票）管理
    └── overlay/
        └── overlay.html        # ★旧バージョン。実装参照には data/overlay.html を使うこと
```

## アーキテクチャ方針

### プラットフォーム抽象レイヤー
YouTube Live / Twitch など複数プラットフォームを統一インターフェースで扱う。
将来のプラットフォーム追加時にコア部分を変更しない設計。

```cpp
class PlatformInterface {
public:
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual std::string getPlatformName() = 0;
    // コメント受信はEventBus経由
};
```

### イベントバス
モジュール間の疎結合を維持するためのPub/Subパターン。
コメント受信・フィルタリング・UI更新などをイベントで繋ぐ。

### 拡張モジュール
将来追加予定の機能はすべてモジュールとして独立して実装する。
モジュールはEventBusを通じてコアと通信し、コアへの依存を最小化。

## 開発フェーズ

### Phase 1：コメントビューワー基盤（ほぼ完了）
- [x] プロジェクト名・構成のリネーム（obs-live-hub）
- [x] OBS ドックパネルにコメントリスト表示（Qt6 CommentDock）
- [x] YouTube Live コメント取得（Data API v3 ポーリング + OAuth 2.0）
- [x] Twitch コメント取得（IRC over TLS + OAuth Implicit Grant）
- [x] 設定ダイアログ（APIキー・OAuth 認証フロー）
- [ ] 基本フィルタリング（NGワード等）— 未実装

### 実装済み拡張機能

| 機能 | 主要クラス | 備考 |
|---|---|---|
| TTS 読み上げ（複数エンジン同時対応） | `EngineManager` / `AivisEngine` / `BouyomiChanClient` / `VoiceroidClient` | webspeech / aivisspeech / sharevox / lmroid / itvoice / bouyomi / voiceroid |
| 視聴者別 TTS 個人設定 | `ViewerTtsSettings` | CSV 永続化、再起動後も維持 |
| 視聴者コメントコマンド（`[olh]`） | `plugin-main.cpp` / `overlay.html` | 詳細は「視聴者コメントコマンド全集」セクション参照 |
| WebSocket サーバー | `WsServer` | overlay.html / tts.html との双方向通信 |
| エフェクトシステム | `EffectManager` / `EffectSettingsDialog` | `[olh] effect:XXX` トリガー対応 |
| ポイントシステム | `PointManager` / `PointSettingsDialog` | 視聴者ポイント管理・CSV 永続化 |
| アンケート（投票）システム | `VoteManagerDialog` | `!vote` コマンド対応（選択式・自由回答） |
| 配信情報一括設定 | `StreamSettingsDialog` | タイトル・カテゴリ等を Twitch/YouTube へ同時設定 |
| オーバーレイ外観カスタマイズ | `OverlayStyleDialog` | |
| TTS 読み上げ辞書 | `TtsDictionaryDialog` | CSV 管理 |
| TTS パラメータ制限設定 | `AivisParamLimitDialog` / `BouyomiParamLimitDialog` | 配信者がパラメータの上下限を設定 |
| X(Twitter) API 投稿 | `XClient` / `XPostDock` / `XAccountSettingsDialog` / `XTemplateSettingsDialog` / `XApiTestDialog` | OAuth 1.0a、テンプレート管理、配信開始時自動投稿、API テストダイアログ |
| X 手動投稿（Web Intent） | `XManualPostDialog` | X API 認証情報不要、ブラウザで `x.com/intent/post` を開く、画像クリップボードコピー |

### Phase 2（将来）：追加予定
- [ ] NGワードフィルタリング
- [ ] AI によるコメント処理（要約・感情分析・NG判定）
- [ ] 配信情報表示（視聴者数・経過時間等）
- [ ] X投稿：画像添付・メディアアップロード
- [ ] X投稿：リンクURLの自動生成（Twitch/YouTubeの実URLとの連携）
- [ ] X投稿：テンプレートのプレースホルダー自動展開（{streamTitle} 等）

## 重要な制約・注意事項
- OBS APIはメインスレッドとの扱いに注意（UI更新はQtのシグナル経由）
- YouTube Data API v3 のクォータ制限に注意
- Twitch IRC接続はOAuthトークンが必要
- DLLはOBSのプラグインフォルダに配置して動作確認
- `CLAUDE_LOG.md` は開発履歴ログのみ。調査・参照不要。読み込まないこと。
- `git push` / GitHub Release 作成前は必ず `RELEASE_CHECKLIST.md` の全項目を確認すること。チェック結果は「現状追跡されていないので問題なし」「過去のコミット履歴に残っている」「現在も追跡されており要対応」の3分類で報告すること。

## X 手動投稿機能（Web Intent）

### 概要
X API 認証情報を一切使わずに X(Twitter) へ投稿できる機能。
Web Intent URL（`x.com/intent/post?text=...`）をデフォルトブラウザで開き、
ユーザーがブラウザ上で最終確認・投稿ボタンを押すことで投稿する。

### メニュー
`ツール → obs-live-hub → X投稿 → X手動投稿`

### アーキテクチャ
- `XManualPostDialog` — テンプレート選択・本文編集・リンクURL入力・画像選択・ブラウザ起動
- `XTemplateSettingsDialog` — テンプレートごとの `includeTwitchLink` / `includeYoutubeLink` チェックを管理

### 動作フロー
1. テンプレートを選択 → 本文テキスト・画像パス・Twitch/YouTubeリンクのチェック状態が初期値として反映される
2. 必要に応じて本文・チェック状態を微調整
3. リンクを追加する方法（3つを組み合わせ可）:
   - 「追加URL（任意）」欄に手動入力（任意のURL）
   - 「Twitchリンク」チェックボックス: テンプレートの `includeTwitchLink` が初期値。`PluginConfig::twitchChannel` から URL 自動構築。設定済みの場合のみ有効
   - 「YouTubeリンク」チェックボックス: テンプレートの `includeYoutubeLink` が初期値。動画IDが取得済みなら有効（URL表示）。取得中なら「取得中...」表示のまま**チェック可能**（テンプレートでONなら配信開始時から自動チェック済み状態で開く）。YouTube未接続かつ設定IDが"me"/空の場合のみ disabled
4. 画像を選択すると `QPixmap::load()` で読み込み可否を確認
5. 「ブラウザで投稿」押下:
   - YouTubeチェックが入っているがURLがまだ未取得（`youtubeUrl_.isEmpty()`）の場合は投稿をブロックし、「YouTubeのURLを取得中です。少し待ってから再度お試しください。」を橙色で表示
   - 画像が選択されている場合: `QApplication::clipboard()->setPixmap()` でクリップボードにコピー、貼り付け案内を表示
   - 本文 + チェックされた全URL（改行区切り）を `QUrl::toPercentEncoding()` で UTF-8 パーセントエンコード
   - `https://x.com/intent/post?text=<encoded>` を `QDesktopServices::openUrl()` でブラウザに渡す

### テンプレートのリンクチェック設定
`XTemplate` に `includeTwitchLink`（bool）・`includeYoutubeLink`（bool）を追加。
`XTemplateSettingsDialog` の右ペイン（テンプレート編集エリア）に「Twitchリンクを含める」「YouTubeリンクを含める」チェックボックスを追加。
`PluginConfig.cpp` で `include_twitch_link` / `include_youtube_link` キーとして JSON 保存・読込。

### YouTube リンクの動的取得
- `YouTubePlatform` に `currentBroadcastId() const`（getter）と `broadcastResolved(QString)` シグナルを実装済み
- `fetchActiveBroadcast()` が `item.id`（動画ID）を取得→ `resolvedBroadcastId_` に保存 → `broadcastResolved` を emit
- `fetchVideoInfo()` でも同様に `broadcastId_` を `resolvedBroadcastId_` に保存 → `broadcastResolved` を emit
- `connect()` / `disconnect()` で `resolvedBroadcastId_` をクリア
- `XManualPostDialog` はコンストラクタで `YouTubePlatform*` を受け取り、取得済みなら即時表示、未取得なら「取得中...」＋シグナル接続。
  `this` が QObject receiver なのでダイアログ破棄時に Qt が自動切断（明示的な disconnect 不要）
- `broadcastResolved` 受信時はラベルのみ更新し、チェック状態は**絶対に変更しない**（テンプレート由来の初期値を維持）
- 静的フォールバック: `youtubeBroadcastId` が実IDなら YouTube 未接続でも URL を構築
- 診断ログ（`broadcastResolved` ラムダ内）:
  - `[XManualPost] broadcastResolved received: videoId=...  isGuiThread=true/false`
  - `[XManualPost] updating YouTube link label to: https://...  checkbox checked=...`
  - `[XManualPost] YouTube link label updated (checkbox state unchanged: ...)`
  - ログが出ない → シグナル未着（ダイアログ破棄済みか接続未成立）
  - `isGuiThread=false` → スレッド不整合（Qt 接続方式の問題）

### 注意事項
- X API（`XClient`）は一切呼び出さない。X Developer Portal の認証情報不要
- 文字数制限はクライアント側で設けない（ブラウザ側の挙動に委ねる）
- 投稿本文はログに出力される（個人情報・APIキーではないため問題なし）
- テンプレートの `linkPlatform` はX手動投稿ダイアログの「追加URL」欄のプレースホルダーヒントとして使用するのみ（実URLは手動入力またはTwitch/YouTubeチェックボックス経由）

---

## X(Twitter) API 投稿機能

### 概要
配信中に X(Twitter) へ OAuth 1.0a API 経由でテキスト投稿できる機能。ツールメニュー → obs-live-hub → X投稿 から設定。

### X API 認証情報の取得方法
1. [X Developer Portal](https://developer.twitter.com/) でアカウントを作成
2. 「+ Create Project」→「+ Add App」で新しいアプリを作成
3. アプリの「Keys and Tokens」タブで以下を取得:
   - **API Key** (Consumer Key)
   - **API Secret** (Consumer Secret)
   - **Access Token**（「Generate」ボタン）
   - **Access Token Secret**（同上）
4. アプリの「Settings」→「User authentication settings」で **Read and Write** 権限を設定すること（Readのみでは投稿不可）
5. `ツール → obs-live-hub → X投稿 → Xアカウント設定` に入力・保存

### アーキテクチャ
- `XClient` — OAuth 1.0a User Context 署名生成（BCrypt HMAC-SHA1 + CryptBinaryToStringA Base64）+ WinHTTP HTTPS POST `/2/tweets`
- `XAccountSettingsDialog` — API Key/Secret/Token/Token Secret 入力（マスク表示）
- `XTemplateSettingsDialog` — テンプレート管理（一覧表示、追加/削除/並び替え/編集）
- `XPostDock` — 「X投稿」OBS ドックパネル（常時表示可能）
- `XPostConfirmDialog` — 配信開始時の最終確認ダイアログ
- `XApiTestDialog` — 認証確認（GET /2/users/me）・テスト投稿（POST /2/tweets）

### テンプレート仕様
`PluginConfig::xTemplates`（`std::vector<XTemplate>`）に保存。各テンプレートのフィールド:
- `name`: テンプレート名（管理用表示）
- `text`: 投稿本文
- `linkPlatform`: リンク先プラットフォーム（"none" / "twitch" / "youtube" / "other"）
- `imagePath`: 将来の画像添付用（Phase 1 では未使用・空文字） / X手動投稿では初期値として反映

### 配信開始時の自動表示モード
`xAutoPostOnStreamStart`（int）で3択:
- `0` = 自動表示しない
- `1` = `OBS_FRONTEND_EVENT_STREAMING_STARTED` 時に `XPostConfirmDialog`（API投稿）を表示
- `2` = `OBS_FRONTEND_EVENT_STREAMING_STARTED` 時に `XManualPostDialog`（手動投稿）を表示

設定UIは2か所から操作可能（どちらも同じ `PluginConfig::xAutoPostOnStreamStart` を共有）:
- `XPostDock` のラジオボタン3択（X投稿ドックパネル）
- `ツール → obs-live-hub → X投稿 → 配信開始時の自動投稿設定`（メニューから開くインラインダイアログ）
  - ドックが非表示でもメニューから設定変更可能。OK時に `s_xPostDock->refresh()` でドック側のUIも同期する

config.json キー: `x_auto_post_mode`（旧 `x_auto_post_on_stream_start` bool キーからの自動移行あり）。

`STREAMING_STARTED` イベント発火時に現在の autoMode 値をログ出力する:
```
[obs-live-hub] STREAMING_STARTED: xAutoPostMode=N (オフ/API投稿確認/手動投稿)
```

### セキュリティ注意事項
- API キー・シークレット・トークンは OBS の config.json に平文保存（他の認証情報と同様の扱い）
- ログには API 認証情報を**絶対に出力しない**。テキスト内容と HTTP ステータスのみ出力
- `XClient::postTweet` はバックグラウンドスレッドで実行し、UI ブロッキングなし

### 今回のスコープ外（Phase 2 予定）
- 画像添付・メディアアップロード（`imagePath` フィールドは構造体に用意済み）
- リンク URL の自動生成（Twitch/YouTube 配信 URL との連携）
- テンプレートプレースホルダー展開（`{streamTitle}` 等）

## 既知の問題と対策

### WsServer ポート占有問題（ゾンビソケット）

**症状:** OBSクラッシュ後、ポート8765が解放されず、新しいWsServerへの接続が確立しない。
または `netstat -ano | findstr :8765` で複数のLISTENINGエントリが存在し、
片方のPIDが既に終了しているにもかかわらず接続が奪われ続ける。

**原因:** Windowsでは異常終了時（特に libcef.dll クラッシュ等）に子プロセスが
ソケットを保持したままになり、TCPソケットがOSレベルで残存することがある。

**対策実装済み（2026-06-21）:**
- `SO_REUSEADDR` を廃止し `SO_EXCLUSIVEADDRUSE` を使用。
  Windowsの `SO_REUSEADDR` は複数ソケットの同時LISTENを許容するため
  ゾンビとの「サイレントな接続の奪い合い」が発生していた。
  `SO_EXCLUSIVEADDRUSE` はポートが占有中の場合 `bind()` を即座に失敗させ
  問題を可視化する（OBSログに `bind() FAILED: port N が別プロセスに占有されています (WSA=XXXXX)` と出力）。
- `WsServer::ListenState` 列挙型（`NotStarted` / `Listening` / `BindFailed` / `ListenFailed`）
  でバインド状態を保持し、接続診断ダイアログから参照可能。

**診断方法:**
- OBSメニュー「ツール → obs-live-hub 接続診断」で WsServer のバインド状態を確認
- コマンドプロンプトで `netstat -ano | findstr :8765` を実行し、PIDが生きているか確認

### 棒読みちゃんで音声が再生されない場合

`tts.html` → WebSocket → `handleBouyomiSpeakRequest` → `BouyomiChanClient::talk()` → HTTP GET
という経路を OBS ログで確認する。

1. `[BouyomiChanClient] speak request:` が出ない → WebSocket メッセージが届いていない（`tts.html` 側の問題）
2. `[BouyomiChanClient] GET http://...` が出ない → `talk()` が呼ばれていない（ハンドラの問題）
3. `WinHttpSendRequest failed: err=12029` → 棒読みちゃんが未起動（接続拒否）。棒読みちゃん本体を起動するか、読み上げ設定ダイアログで「自動起動」を設定する
4. `WinHttpSendRequest failed: err=XXXXX`（12029 以外）→ ポート設定の相違、またはファイアウォールの可能性
5. `speak OK` が出るのに読み上げられない → 棒読みちゃんアプリ側の設定問題（HTTP連携が有効か確認）

**自動起動機能（2026-06-22 実装）:**
読み上げ設定ダイアログ（ツール → obs-live-hub → 読み上げ設定）の棒読みちゃんセクションで
「実行ファイルパス」を設定し「OBS起動時に自動起動する」をチェックすると、
OBS 起動時に `EngineManager::startAll()` が棒読みちゃんを自動起動する。
起動後は HTTP ping（GET /）で接続確認し、成功すれば EngineStatus が Connected になる。
EngineManager の接続確認ログ: `[EngineManager] bouyomi auto-start: launching ...` / `already running, skipping launch` / `path not set, skipping`

**解消方法:** bind失敗時はPCの再起動が必要な場合がある（OBS単体の再起動では直らないことがある）。

### Google OAuth 同意画面「テスト中」ステータスとリフレッシュトークン有効期限

**症状:** YouTube 連携が 7 日経過後に突然失敗し始める。OBS ログに
`invalid_grant: Token has been expired or revoked.` が出る。

**原因:** Google Cloud Console で OAuth 同意画面を作成した直後は公開ステータスが
**「テスト中（Testing）」** になっている。このステータスでは
**リフレッシュトークンの有効期限が 7 日間** に制限される（通常は無期限）。
7 日間 OBS を起動しないか YouTube 連携が使われないと、リフレッシュトークン自体が
失効し、アクセストークンの自動更新（`refreshAccessTokenAsync`）を試みても
`invalid_grant` エラーになる。

**対応方法（いずれか）:**

1. **推奨（個人利用）:** 7 日以内に一度は OBS を起動して YouTube 連携を使う運用にする。
   - 再認証は `ツール → obs-live-hub → 接続設定 → YouTube タブ → Googleアカウントと連携`
     からいつでも可能。
2. **「アプリを公開」（任意）:** Google Cloud Console の OAuth 同意画面で
   「本番環境（公開）」に変更すると 7 日制限が解除される。
   ただし使用スコープによっては Google の審査プロセスが必要になる場合があり、
   個人利用の範囲では審査コストを考えると必須ではない。

**自動リフレッシュの実装状況（参考）:**
`YouTubePlatform` は以下 3 か所でトークン自動更新を試みる（すでに実装済み）:
- `fetchMessages()`: 有効期限 300 秒前にプロアクティブに更新
- `onBroadcastInfoResult()`: HTTP 401 応答時にリアクティブに更新
- `onMessagesResult()`: 同上

`invalid_grant` の場合は自動更新自体が失敗するため、上記の再認証フローが必要。

### YouTubeリンク取得のタイムラグについて（仕様）

**症状:** 配信開始後、X手動投稿ダイアログの「YouTubeリンク」が「取得中...」のまま
しばらく表示され続ける。

**原因（仕様）:** OBSで配信開始ボタンを押してから、YouTube側のブロードキャストが
実際に `lifeCycleStatus: live` になるまで **15〜30秒程度のラグ** がある。
ログ上の典型的な流れ:
```
[YouTubePlatform] lifeCycleStatus: ready  ← 配信開始直後
（15秒リトライ間隔）
[YouTubePlatform] lifeCycleStatus: live   ← ここで broadcastResolved が emit される
[XManualPost] broadcastResolved received: videoId=...  isGuiThread=true
[XManualPost] updating YouTube link label to: https://...
[XManualPost] YouTube link label updated (checkbox state unchanged: ...)
```

**これは異常ではない。** ダイアログを開いたまま待てば、`broadcastResolved` シグナル経由で
自動的にラベルが更新される（2026-06-24 実機テストで動作確認済み）。

**「ブラウザで投稿」ボタンを押すタイミング:** YouTubeリンクにチェックが入った状態で
URLがまだ「取得中...」の場合、ボタン押下をブロックして案内メッセージを表示する
（URL未解決のままリンクなしで投稿してしまわないための保護）。
ラベルが実際のURLに更新されてから押せばよい。

## OBSプラグインフォルダ（Windows）
```
C:\Program Files\obs-studio\obs-plugins\64bit\
```
または
```
%APPDATA%\obs-studio\plugins\
```

## ドキュメント

初心者向けガイドは `GETTING_STARTED.md` を参照。

## 参考リンク
- OBS Plugin API: https://obsproject.com/docs/
- obs-plugintemplate: https://github.com/obsproject/obs-plugintemplate
- YouTube Live Streaming API: https://developers.google.com/youtube/v3/live
- Twitch EventSub: https://dev.twitch.tv/docs/eventsub/

---

## 視聴者コメントコマンド全集

> **運用ルール**
> 視聴者コメントコマンドを追加・変更・削除した場合は、必ずこのセクションを
> 同時に更新すること。実装とドキュメントの不一致を防ぐため、コマンド処理部分
> （`data/overlay.html` の `parseOlhCommand` / `applyOlhCommand`、
> `src/plugin-main.cpp` の `processEffectOlhCommand` / `processPointOlhCommand`）
> を修正する際はこのセクションの該当箇所も確認・修正する。

### 概要・アーキテクチャ

視聴者がチャットに送る `[olh]` プレフィックスコマンドは 2 か所で処理される。

| 処理場所 | ファイル | 対象コマンド |
|---|---|---|
| フロントエンド | `data/overlay.html` `parseOlhCommand` / `applyOlhCommand` | 表示設定・エンジン/モデル/パラメータ系 |
| バックエンド (C++) | `src/plugin-main.cpp` `processEffectOlhCommand` / `processPointOlhCommand` | エフェクト・ポイント系 |

**共通仕様:**
- コマンドは `,` 区切りで複数指定可能: `[olh] engine:aivisspeech, model:ずんだもん`
- `[olh]` コマンドは TTS では読み上げない（`tts.html` で先頭 `[olh]` を検出してスキップ）
- 表示設定・エンジン/パラメータ系は `overlay.html` が処理し、コメントカードには表示されない
- エフェクト・ポイント系は C++ で処理後もコメントカードとして表示される（`overlay.html` がパースしないため）
- 個人設定は `ViewerTtsSettings`（CSV）に永続化される。OBS を再起動しても維持される

---

### 1. 表示設定系

`overlay.html` のローカルストレージ（`olhUserSettings`）に保存。OKボタン不要、即時反映。

| コマンド | 機能 | 値の形式・範囲 | 備考 |
|---|---|---|---|
| `[olh] reset` | 全個人設定をリセット | なし | ローカルストレージのみクリア。C++ ViewerTtsSettings（エンジン・スタイル・各TTSパラメータ）はリセットされないため、次回コメントのTTS設定は変わらない |
| `[olh] color:XXX` | コメントテキスト色を変更 | CSS色値（名前・`#RRGGBB`・`rgb()`等） | 輝度 0.15 未満は拒否（暗すぎて読めない色）。不正値は無視 |
| `[olh] name:XXX` | ユーザー名表示色を変更 | CSS色値 | 同上 |
| `[olh] panel:XXX` | コメントカード背景色を変更 | CSS色値 | 輝度 0.05〜0.50 のみ有効（暗すぎ・明るすぎを排除） |

---

### 2. TTS エンジン切り替え

`overlay.html` → WebSocket → C++ `handleOlhEngineRequest` → `ViewerTtsSettings` CSV に永続化。

| コマンド | 切り替え先エンジン | 備考 |
|---|---|---|
| `[olh] engine:webspeech` | Web Speech API（ブラウザ内蔵） | `web` も同義 |
| `[olh] engine:aivisspeech` | AivisSpeech (localhost:10101) | `aivis` も同義。切り替え後、先頭話者のstyleIdが自動設定される |
| `[olh] engine:sharevox` | SHAREVOX (localhost:50025) | 同上 |
| `[olh] engine:lmroid` | LMROID (localhost:49973) | 同上 |
| `[olh] engine:itvoice` | ITVOICE (localhost:49540) | 同上 |
| `[olh] engine:bouyomi` | 棒読みちゃん (localhost:50080) | 先頭話者の自動設定なし |
| `[olh] engine:voiceroid` | VOICEROID（AssistantSeika経由、localhost:7180） | 先頭話者の自動設定なし。cid・認証情報は読み上げ設定で配信者が一括設定 |

**注意:** 不正なエンジン名（上記以外）はエラーなしで無視される。コメントがカードとして表示される。

エンジンが有効化されていない場合、`ttsCheckEngineConnection`（読み上げ設定の「エンジン接続チェック」）が ON だと自動的に webspeech へフォールバックする。

---

### 3. モデル（話者/スタイル）切り替え

VOICEVOX互換エンジン（aivisspeech / sharevox / lmroid / itvoice）専用。  
`overlay.html` → WebSocket → C++ `handleResolveModel` → `ViewerTtsSettings` に styleId を永続化。

| コマンド | 機能 | 値の形式 |
|---|---|---|
| `[olh] model:モデル名` | VOICEVOX互換エンジンの話者/スタイルを変更 | 話者名またはスタイル名（最大80文字）。`AivisStyleCache` で名前検索しstyleIdに解決 |

- モデル切り替え成功時、aivis パラメータ（speed/pitch/intonation/volume/emotion）は自動リセットされる
- 一致するモデルが見つからない場合、システムコメントでエラーを表示
- AivisStyleCache が未取得の場合（エンジン起動直後など）もエラーを返す

---

### 4. Web Speech API パラメータ

`overlay.html` でクランプ後、WebSocket → C++ `handleOlhWebSpeechParamsRequest` → `ViewerTtsSettings` に永続化。

| コマンド | 機能 | 範囲 | デフォルト |
|---|---|---|---|
| `[olh] webs_volume:X.X` | 音量 | 0.0〜1.0 | 1.0 |
| `[olh] webs_rate:X.X` | 速度 | 0.7〜1.5 | 1.0 |
| `[olh] webs_pitch:X.X` | ピッチ | 0.7〜1.3 | 1.0 |

- 範囲外の値は自動クランプ（エラーなし）
- webspeech エンジン使用時のみ有効

---

### 5. AivisSpeech（VOICEVOX互換）パラメータ

`overlay.html` で数値チェックのみ実施 → WebSocket → C++ `handleOlhAivisParamsRequest` でクランプ後 `ViewerTtsSettings` に永続化。

**対応エンジン:** aivisspeech / sharevox / lmroid / itvoice（VOICEVOX互換全般）

| コマンド | 機能 | デフォルト上下限 | UI設定ダイアログ |
|---|---|---|---|
| `[olh] aivis_speed:X.X` | 話速（speedScale） | 0.5〜2.0 | 読み上げ > AivisSpeechモデル制限 |
| `[olh] aivis_pitch:X.X` | 音高（pitchScale） | -0.15〜0.15 | 同上 |
| `[olh] aivis_intonation:X.X` | 抑揚（intonationScale） | 0.0〜2.0 | 同上 |
| `[olh] aivis_volume:X.X` | 音量倍率（volumeScale） | 0.0〜2.0 | 同上 |
| `[olh] aivis_emotion:X.X` | 感情表現/テンポダイナミクス（tempoDynamicsScale） | 0.0〜2.0 | 同上 |

- 上下限はダイアログ「**読み上げ > AivisSpeechモデル制限**」で変更可能（実際の設定範囲: 0.01〜10.0）
- 範囲外の値は配信者設定の上下限でクランプ（エラーなし）
- 数値以外（文字列など）を指定した場合、システムコメントでエラーを表示: `XXXさんへ：aivis_speed には数値を指定してください。`

---

### 6. 棒読みちゃんパラメータ

`overlay.html` で整数チェックのみ実施 → WebSocket → C++ `handleOlhBouyomiParamsRequest` でクランプ後 `ViewerTtsSettings` に永続化。

**対応エンジン:** bouyomi のみ

| コマンド | 機能 | デフォルト上下限 | -1 の意味 | UI設定ダイアログ |
|---|---|---|---|---|
| `[olh] bouyomi_volume:N` | 音量 | 0〜100 | 棒読みちゃんの現在設定を使用 | 読み上げ > 棒読みちゃんパラメータ制限 |
| `[olh] bouyomi_speed:N` | 速度 | 50〜300 | 同上 | 同上 |
| `[olh] bouyomi_tone:N` | 音程 | -100〜100 | 同上 | 同上 |

- `-1` は常に許可（配信者の上下限設定に関係なく通過）
- 上下限はダイアログ「**読み上げ > 棒読みちゃんパラメータ制限**」で変更可能（実際の設定範囲: volume 0〜100, speed 50〜300, tone -100〜100）
- 数値以外を指定した場合、システムコメントでエラーを表示

---

### 7. エフェクトシステム

C++ `processEffectOlhCommand` で処理。エフェクト設定（`obs-live-hub > エフェクト > エフェクト設定`）で `olh_command` トリガーを設定済みのエフェクトのみ発火する。

| コマンド | 機能 | 備考 |
|---|---|---|
| `[olh] effect:エフェクト名` | 指定名のエフェクトを再生 | エフェクト設定で `triggerType: olh_command`, `triggerValue: effect:エフェクト名` が設定されていること |

- キー比較は大文字小文字を区別しない（`[OLH] EFFECT:xxx` も有効）
- 一致するエフェクトがなくても無視（エラーなし）
- コメントカードとして表示される

---

### 8. ポイントシステム

C++ `processPointOlhCommand` で処理。ポイントシステムが有効（`pointEnabled = true`）の場合のみ動作。

| コマンド | 機能 | 備考 |
|---|---|---|
| `[olh] point_use:コマンド名` | ポイントアクションを実行 | ポイント設定で登録されたコマンド名を指定。ポイント不足時・クールダウン中はシステムコメントでエラーを表示 |
| `[olh] point_check` | 現在のポイント残高を確認 | システムコメントで `XXXさんへ：現在のポイントはNptです` を表示 |

- キー比較は大文字小文字を区別しない
- コメントカードとして表示される

---

### 9. アンケート投票（`!vote` 系）

`[olh]` プレフィックスではなく `!vote ` プレフィックス。C++ `processVoteComment` で処理。

| コマンド | 機能 | 備考 |
|---|---|---|
| `!vote 選択肢` | アンケートに投票 | アンケート開催中（`s_voteActive = true`）の場合のみ有効。選択式は `A/B/C...`、自由回答は任意文字列 |

- 大文字小文字を区別しない（`!Vote A` も有効）
- 同一ユーザーが再投票した場合は上書き

---

### 実装上の注意事項（既知の制限・想定外の挙動）

1. **`[olh] reset` は C++ 側をリセットしない**  
   `overlay.html` のローカルストレージ（表示設定・webspeech params）のみクリア。エンジン設定・styleId・aivis/bouyomiパラメータ（ViewerTtsSettings CSV）はリセットされない。

2. **不正なエンジン名はコメントとして表示される**  
   `[olh] engine:invalid_name` のように対応エンジン名以外を指定した場合、エラーなしで通常コメントカードとして表示される。

3. **`src/overlay/overlay.html` は旧バージョン**  
   `src/overlay/overlay.html` は古いバージョン（webspeech params が `volume/rate/pitch`）。実際にデプロイされるのは `data/overlay.html`（`webs_volume/webs_rate/webs_pitch`）。コマンド実装は `data/overlay.html` を参照すること。

4. **AivisSpeech パラメータは VOICEVOX互換エンジン全般に適用される**  
   コマンド名が `aivis_*` だが、sharevox / lmroid / itvoice でも同じ VOICEVOX API パラメータとして使用される。