## 直前の作業（2026-06-18 更新）

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

### 棒読みちゃん対応（実装済み）
- エンジン: 棒読みちゃん HTTP連携サーバー（デフォルトポート 50080）
- 通信方式: `GET http://{host}:{port}/Talk?text=...&voice=...&volume=...&speed=...&tone=...` のみ（WinHTTP、非同期）
- レスポンスは無視（棒読みちゃん自体が音声を再生するため）
- テキストは UTF-8 URL エンコードで送信
- tts.html は `bouyomi_speak` WebSocket メッセージを C++ に送信するトリガーのみ担当（audio再生なし）
- 棒読みちゃん自体が再生キューを管理するため、tts.html は送信後即座に次のキューへ移行

#### TtsSpeechDialog 棒読みちゃん設定（エンジン選択で表示）
- ホスト（デフォルト: localhost）
- ポート（デフォルト: 50080）
- 声の種類: プリセット一覧 + 直接入力スピンボックス
  | voice | 声 |
  |---|---|
  | -1 | 前回と同じ |
  | 0 | 自動 (デフォルト声) |
  | 1 | 女性1 (AquesTalk) |
  | 2 | 女性2 (AquesTalk) |
  | 3 | 中性  (AquesTalk) |
  | 4 | 男性1 (AquesTalk) |
  | 5 | 中性2 (AquesTalk) |
  | 6 | ロボット (AquesTalk) |
  | 7 | 機械1 (AquesTalk) |
  | 8 | 機械2 (AquesTalk) |
  | 9 | 女性3 (AquesTalk) |
  | 10 | 女性4 (AquesTalk) |
  ※外部追加ソフトにより番号がずれる場合は直接入力で対応

#### [olh] コマンド拡張（棒読みちゃん）
| キー | 説明 | 範囲 |
|---|---|---|
| `engine:bouyomi` | 棒読みちゃんに切り替え | — |
| `bouyomi_volume` | 音量 | -1(現在設定) または 0〜100（上下限設定可） |
| `bouyomi_speed` | 速度 | -1(現在設定) または 50〜300（上下限設定可） |
| `bouyomi_tone` | 音程 | -1(現在設定) または -100〜100（上下限設定可） |

- 上下限は「obs-live-hub → 読み上げ → 棒読みちゃんパラメータ制限」で設定
- BouyomiChanClient: `src/modules/BouyomiChanClient.{hpp,cpp}`
- BouyomiParamLimitDialog: `src/ui/BouyomiParamLimitDialog.{hpp,cpp}`

### VOICEVOX互換エンジン対応（実装済み）
- AivisSpeech / SHAREVOX / LMROID / ITVOICE の4エンジンをサポート
- デフォルトポート: AivisSpeech=10101、SHAREVOX=50025、LMROID=49973、ITVOICE=49540
- OBS読み上げ設定ダイアログにエンジン切り替え・話者選択・自動起動設定を追加（5択）
- エンジンごとにURL・実行ファイルパス・自動起動を独立設定
- AivisSpeech のみ `%LOCALAPPDATA%\Programs\AivisSpeech\...` をデフォルトパスとして自動補完
- VOICEVOX互換エンジン使用時はChromeの `--disable-web-security` で起動必須
- tts.html: `isVoicevoxEngine()` ヘルパーで4エンジンを統一処理

#### [olh] engine コマンド対応エンジン
| 値 | エンジン |
|---|---|
| `webspeech` | Web Speech API（ブラウザ） |
| `aivisspeech` | AivisSpeech（ローカル） |
| `sharevox` | SHAREVOX（ローカル） |
| `lmroid` | LMROID（ローカル） |
| `itvoice` | ITVOICE（ローカル） |

- `[olh] model:モデル名` → `resolve_model_result` の engine フィールドが現在の有効エンジンを返す

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
| `engine` | TTSエンジン切り替え | webspeech / aivisspeech / sharevox / lmroid / itvoice / bouyomi |
| `model` | VOICEVOX互換エンジン音声モデル指定 | 部分一致検索。切り替え時に aivis_* パラメータをリセット |
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
- 表示パネル: 接続状態・TTS設定・クォータ・投票状態・broadcastログ・エフェクト状態・ポイント状態・最新コメント詳細
- エフェクト状態: 並列実行中/キュー待機数とエフェクトログ（playing/queued/discarded）
- ポイント状態: ポイント付与ログ・ポイント使用ログ（コマンドOK/NG・残高）
- 最新コメント詳細: 原文・辞書置換後・テキスト表示設定・音声再生設定・[olh]ユーザー設定
- debug_effect_status/debug_effect_log/debug_point_log/debug_point_use_log はメインbroadcastログには表示しない
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
| オーバーレイ | TTS音声ページを開く | tts.htmlをChrome起動（VOICEVOX互換エンジン時は--disable-web-security付き） |
| 読み上げ | 読み上げ設定 | TTS設定・VOICEVOX互換エンジン管理（AivisSpeech/SHAREVOX/LMROID/ITVOICE） |
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

（なし）

### エフェクト機能（実装済み）

#### ファイル構成
```
%APPDATA%\obs-studio\plugins\obs-live-hub\
├── effect.html          # エフェクト表示ページ（OBSブラウザソースで使用）
└── effects\             # ユーザーが配置するエフェクトファイル
    ├── fire_emoji.json  # effect.json（下記フォーマット）
    └── fire_emoji.gif   # 画像/GIFファイル
```

effect.json フォーマット：
```json
{
  "name": "fire_emoji",
  "trigger": { "type": "emoji", "value": "🔥" },
  "file": "fire_emoji.gif",
  "duration": 3000,
  "position": "center",
  "positionOverride": false,
  "size": "medium",
  "sizeOverride": false,
  "sound": "fire.wav",
  "soundVolume": 1.0,
  "soundVolumeOverride": false
}
```
- trigger.type: "emoji" or "olh_command"
- trigger.value: 絵文字文字列 or "effect:xxx"形式
- position: center/top/bottom/top-left/top-right/bottom-left/bottom-right
- size: small(100px) / medium(200px) / large(300px)
- positionOverride/sizeOverride: falseの場合は全体設定値を使用
- sound: SEファイル名（wav/mp3/ogg対応、省略可）
- soundVolume: 0.0〜1.0（soundVolumeOverride=trueの場合のみ有効）
- soundVolumeOverride: falseの場合はEffectSettingsDialogのSE全体音量を使用

#### OBSツールメニュー
| サブメニュー | 項目 | 動作 |
|---|---|---|
| エフェクト | エフェクト設定 | EffectSettingsDialog（全体設定・SE全体音量・一覧・フォルダを開く） |
| エフェクト | エフェクトページを開く | effect.htmlをブラウザで開く |

EffectSettingsDialog の全体設定項目：
- デフォルト表示位置（position プリセット）
- デフォルトサイズ（small/medium/large）
- 同時表示上限数（スピンボックス、デフォルト3）
- キュー上限数（スピンボックス、デフォルト10）
- SE全体音量スライダー（0.0〜1.0、soundVolumeOverride=false のエフェクトに適用）

#### [olh]コマンド拡張
| キー | 説明 |
|---|---|
| `effect` | 例: `[olh] effect:fire_emoji` → trigger.value="effect:fire_emoji" のエフェクトを発火 |

#### 同時実行制御
- 同時表示上限数（デフォルト3）/ キュー上限数（デフォルト10）をPluginConfigで管理
- 上限到達時はキューに追加、キューも上限到達時は破棄してログ出力

#### WebSocket通知
```json
{"type":"effect_trigger","name":"fire_emoji","file":"effects/fire_emoji.gif","position":"center","size":"medium","duration":3000,"sound":"effects/fire.wav","volume":1.0}
```
- sound: SEファイルのパス（effects/ 相対）。省略時はSEなし
- volume: 実際に再生する音量（soundVolumeOverride=true → soundVolume、false → SE全体音量）

### ポイントシステム（実装済み）

#### ファイル構成
```
%APPDATA%\obs-studio\plugins\obs-live-hub\
├── points.csv               # ユーザーポイント永続化（自動生成）
└── points_actions\          # アクション定義ファイル（1アクション = 1 JSON）
    ├── fire_effect.json
    └── voice_change.json
```

#### アクション定義ファイルのフォーマット
```json
{
  "name": "fire_effect",
  "command": "fire_effect",
  "cost": 50,
  "action": {
    "type": "trigger_effect",
    "value": "fire_emoji"
  }
}
```
```json
{
  "name": "voice_change",
  "command": "voice_change",
  "cost": 100,
  "action": {
    "type": "set_model",
    "value": "にせ"
  }
}
```
- command: `[olh] point_use:{command}` で呼び出す識別子
- cost: 消費ポイント数
- action.type: `trigger_effect`（エフェクト発火）/ `set_model`（AivisSpeechモデル変更）

#### ポイント付与方式
- **コメント型**: コメント受信1回ごとに +N ポイント（デフォルト1）
  - コメントクールダウン秒内の連続コメントはポイントをスキップ（コメント表示は通常通り）
- **視聴時間型**: タイマー間隔ごとに全ユーザーへ +M ポイント（デフォルト5分ごとに+1）
  - 初回発言時点でそのユーザーの追跡を開始し、以後継続付与

#### クールダウン設定（PointSettingsDialog 設定タブ）
| 設定 | デフォルト | 範囲 | 説明 |
|---|---|---|---|
| コメントポイントのクールダウン | 10秒 | 0〜300秒 | 同ユーザーのコメントポイント加算間隔（0=無効） |
| point_use実行のクールダウン | 5秒 | 0〜300秒 | 同ユーザーのpoint_use連打防止（0=無効） |
- クールダウン中に point_use が来た場合: 「{user}さんへ：操作が早すぎます。しばらく待ってから再試行してください。」

#### CSVチェックサムによる改ざん検知
- CSV保存時に同フォルダへ `points.csv.checksum` (SHA-256, hex) を同時保存
- OBS起動時にCSVを読み込む際、チェックサムファイルと照合
  - 一致: 通常通り読み込み
  - 不一致・チェックサムファイルなし: OBS起動完了後にモーダル警告ダイアログを表示
    - 「そのまま読み込む」: 既存データを維持し、チェックサムを再計算して保存
    - 「リセットする」: 全ユーザーのポイントを0にしてCSV・チェックサムを保存

#### CSV フォーマット
```
userId,platform,points
TwitchUser1,twitch,150
YouTubeUser1,youtube,75
```
- 保存タイミング: 加算・消費から3秒以内（デバウンス保存）
- OBS起動時に自動読み込み（チェックサム検証付き）

#### [olh]コマンド拡張
| キー | 書式 | 説明 |
|---|---|---|
| `point_use` | `[olh] point_use:fire_effect` | 指定コマンドのアクションを消費して実行 |
| `point_check` | `[olh] point_check` | 現在ポイントをsystemコメントで返答 |

- point_use でアクション未存在: 「{user}さんへ：そのアクションは存在しません」
- point_use でポイント不足: 「{user}さんへ：ポイントが足りません（現在X、必要Y）」
- point_check 成功: 「{user}さんへ：現在のポイントはXptです」

#### WebSocket通知（point_use_result）
```json
{"type":"point_use_result","ok":true,"userId":"TwitchUser1","command":"fire_effect","remainingPoints":50}
{"type":"point_use_result","ok":false,"userId":"TwitchUser1","command":"fire_effect","remainingPoints":10,"error":"insufficient_points"}
```

#### OBSツールメニュー
| サブメニュー | 項目 | 動作 |
|---|---|---|
| ポイント | ポイント設定 | PointSettingsDialog（設定・アクション一覧・ユーザーポイント一覧） |

PointSettingsDialog のタブ構成:
- **設定タブ**: ポイント機能ON/OFF、コメントポイント付与量、視聴ポイント付与間隔・付与量、コメントクールダウン秒数、point_use実行クールダウン秒数
- **アクション一覧タブ**: points_actionsフォルダのJSON一覧（name/command/cost/action.type）、「フォルダを開く」「再読み込み」ボタン
- **ユーザーポイントタブ**: 全ユーザーのポイント一覧（ポイント降順）、ユーザー名検索、選択ユーザーのポイント手動変更

## TTS複数エンジン対応（視聴者ごと個別設定化）

### 背景
従来は配信者がTTSエンジンを1つだけ選ぶ設計だったが、
視聴者ごとに異なるエンジン（例: Aさん=webspeech、Bさん=AivisSpeech、
Cさん=SHAREVOX）を同時に使えるようにする設計に変更中。

### 完了済み
- **Step 1**: ViewerTtsSettingsクラスを実装。userId+platformごとに
  engine/styleId/bouyomiVoice/rate/pitch/volume/aivis_*系パラメータ/
  bouyomi_*系パラメータを保持し、tts_settings.csvに永続化。
  ビルド・起動確認済み（CSV読み込み・空起動を確認済み）。
- **Step 2**: [olh] engine:xxx, [olh] model:xxx, aivis_speed等,
  webs_rate等, bouyomi_volume等のコマンドが、コマンド送信者個人の
  ViewerTtsSettingsに保存されるよう変更済み。ビルド・起動確認済み。

### 未着手（次回再開時はStep 3から）
- **Step 3**: 複数エンジン（webspeech/aivisspeech/sharevox/lmroid/itvoice/bouyomi）を
  OBS起動時に同時起動し、各エンジンの起動・接続状態を管理する機構
- **Step 4**: コメント受信時に発言者のViewerTtsSettingsをルックアップし、
  WebSocketのcommentメッセージにtts情報（engine/baseUrl/styleId等）を
  埋め込んで送信する処理。あわせてtts.html側を、メッセージに埋め込まれた
  tts情報を使って都度合成する処理に変更する（現状はグローバル設定を見て
  読み上げている）
- **Step 5**: 読み上げ設定ダイアログ（TtsSpeechDialog）を、対応エンジンを
  縦一列に並べて全エンジンの有効/無効・接続状態・設定を一覧できるUIに
  全面改修。デフォルトエンジン選択も追加。

### 重要な注意点（次回再開時に必読）
現時点では視聴者個人のTTS設定はCSVに保存されているが、
実際の読み上げ処理はまだ変更されておらず、引き続き
グローバル設定（PluginConfigのttsEngine/aivisStyleId等）を見て
読み上げを行っている。Step 4が完了するまでは、
「視聴者ごとに異なるエンジンで読み上げる」という本来の目的の機能は
完成していない。

この変更は影響範囲が広く、Claude Code/Claude.aiのトークン消費が
大きいため、開発者の判断で一旦中断した。次回はStep 3から再開する。

---

## 次の予定（v0.2.0リリース完了・v0.3.0に向けて）
1. エフェクト機能・ポイントシステムの動作確認（Developer PowerShell でビルド・テスト）
2. 動作確認後、git commit・push・v0.3.0リリース公開
3. その他バグ修正・機能追加