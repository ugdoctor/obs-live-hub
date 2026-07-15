# STATUS.md

## 現在の状態

- **v0.3.4 リリース済み（2026-07-15）**
- 主要機能（TTS複数エンジン、棒読みちゃん、視聴者コマンド、ポイント、エフェクト等）は安定動作を確認済み
- 勝敗数カウンター機能（常設ドック・目標モード・画面メモ）は実機検証完了（2026-07-15）
- Twitchスタンプ表示（CDN URL修正込み）・YouTubeカスタムエモート辞書機能は実機デバッグ・動作確認完了（2026-07-15）

---

## 未確認・保留中のタスク（優先度順）

### 0. ポイントシステム課金連携（USD→ポイント自動付与）の実機確認

- **経緯:** 2026-07-14、課金イベント（SC/スーパーステッカー/Bits/サブスク/メンバーシップ）受信時に
  `pointBillingRate`（デフォルト100pt/USD）に基づきポイントを自動付与する機能を実装。ビルド確認のみ済み
- **確認方法:**
  1. `ツール → obs-live-hub → ポイント設定` の「設定」タブで「課金連携を有効にする」がON、レートが意図通りか確認
  2. `サポーター履歴` の「テストデータ注入」を実行 → OBSログに `[PointBilling] Awarded N pt to ...` が出力されることを確認
     （注意：テストデータの通常課金分は`usdAmount`が入っているため付与されるはずだが、メンバーシップ分は
     プラン価格未設定だと `resolvedUsd=0` のため付与されない。「メンバーシッププラン価格設定」で
     「テストメンバーレベル」に価格を設定してから再度確認するとメンバーシップ分の付与も確認できる）
  3. `ポイント設定 → ユーザーポイント` タブでテストユーザーのポイントが加算されていることを確認
  4. 実際のTwitch配信でBits/サブスクを発生させ、バックグラウンドスレッドからの呼び出しでもクラッシュ・
     警告ログが出ないこと（スレッド安全性）を確認

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

### Twitchスタンプ表示・YouTubeカスタムエモート辞書機能：実機デバッグ・動作確認完了（2026-07-15）
- **v0.3.4としてリリース済み**
- Twitchスタンプが一部画像化されない不具合を調査。`CommentEmote::id`は元々`std::string`実装済み
  だったため「IDが数値型」という仮説は誤りと判明。実際の原因はTwitchエモートCDNの
  URLパス誤り（`emotes/v2`→正しくは`emoticons/v2`）で、修正済み
  - `sasasfAori`のような配信者固有カスタムエモートについては、Twitch公式のIRC`emotes`タグに
    一切現れないBetterTTV/FrankerFaceZ等のサードパーティエモートである可能性が高いという
    仮説を提示（Twitch公式APIの範囲では検出不可能なため対応スコープ外の判断）
  - 診断用にC++側（`TwitchPlatform.cpp`、emotesタグの生値とパース件数をOBSログ出力）と
    JS側（`overlay.html`、`console.debug`/`console.error`）にログを追加
- YouTubeカスタムエモート辞書機能（ドラッグ＆ドロップ・プレビュー付き管理画面、`WsServer`の
  簡易HTTP画像配信拡張）を含め、実機デバッグ・動作確認完了

### 勝敗カウンター機能（MatchCounter）実装＋ドック化リファクタリング（2026-07-15）
- **初回実装**: `PluginConfig`に`matchWins`/`matchLosses`/`matchHistory`（最大10件）/`matchTargetWins`/
  `matchTargetWinRate`/外観設定を追加。`MatchCounterDialog`（勝敗操作＋外観設定の一体型ダイアログ）と
  `data/match_counter.html`（WebSocket受信専用ブラウザソース、スロット風リール演出）を新規実装
- **同日中にリファクタリング**: 日常操作（勝敗+1/-1・手入力・目標・メモ）を新規`MatchCounterDock`
  （`obs_frontend_add_dock_by_id`で登録する常設ドック）に移管し、`MatchCounterDialog`は外観設定
  （幅・色・フォント・透明度）専用に縮小
- `PluginConfig`に`matchMemo`（配信画面表示用の1行メモ）/`matchTargetMode`（0=なし,1=勝利数,2=勝率の
  排他モード）を追加
- `data/match_counter.html`: メモ表示欄を目標表示の1行上に追加。`targetMode`に応じて「あと何勝」表示と
  勝率の黒字/赤字色分けを排他的に出し分け（`0`=両方非表示、`1`=あと何勝のみ、`2`=勝率色分けのみ）
- `plugin-main.cpp`: メニューを`ツール → obs-live-hub → 勝敗数カウンター`サブメニューに集約
  （カウンタードックを表示／カウンター外観設定／カウンターページを開く）。ドック表示トグルは
  `QDockWidget::toggleViewAction()`を流用
- `CMakeLists.txt`に`MatchCounterDock.cpp/hpp`を追加
- **実機検証完了（2026-07-15）**: ドック表示/非表示トグル、+1/-1・手入力・目標モード3択・メモ入力、
  外観設定ダイアログの分離、目標モードごとのオーバーレイ出し分け、全リセット、すべて問題なし
- **v0.3.3としてリリース済み（2026-07-15）**

### YouTubeカスタムエモート辞書機能実装（2026-07-15）
- `WsServer`を拡張し、`GET /emotes/<filename>`への簡易HTTPレスポンス（画像バイナリ配信）に
  対応。WebSocketハンドシェイクの手前で分岐し、ディレクトリトラバーサル対策
  （`..`・`/`・`\`を含むファイル名を拒否）とファイルサイズ上限（20MB）を実装
- `YouTubeEmoteSettingsDialog`（新規）: ショートコード↔画像ファイルのマッピングをCSV
  （`youtube_emotes.csv`、`PluginConfig`は使わずTtsDictionaryDialogと同じ設計方針）で管理。
  ドラッグ＆ドロップで画像を自動格納先フォルダ（`youtube_emotes\`）へ`CopyFileW`でコピーし、
  `QUuid`で一意なファイル名を生成。テーブルに画像プレビュー（`QPixmap`）を表示
- `overlay.html`: `renderWithShortcodeEmotes()`を新規実装し、YouTube向けにショートコード
  文字列の最長一致検索による置換に対応（Twitchの位置情報ベース方式とは別ロジック）。
  `renderCommentText()`が`platform`引数を見てTwitch/YouTubeの方式を切り替える
- `plugin-main.cpp`: メニュー「オーバーレイ → YouTubeエモート辞書設定」を追加。
  `youtube_emotes_sync`メッセージを新規WS接続時・辞書保存時にブロードキャスト
- ビルド確認済み（2026-07-15）。Node.jsでショートコード置換ロジックを単体テスト
  （基本置換・最長一致優先・複数出現・非該当高速パス、すべて確認）。**実機未確認**
  （上記「未確認・保留中のタスク」参照）

### Twitchスタンプ（エモート）画像表示機能実装（2026-07-15）
- `CommentEvent`（`PlatformInterface.hpp`）に`emotes`（`std::vector<CommentEmote>`、id/start/end）を追加
- `TwitchPlatform::parseEmotesTag()`を新規実装。IRCv3の`emotes`タグ（例:`25:0-4,6-10/34:12-16`）を
  パースし`CommentEvent::emotes`に格納
- `plugin-main.cpp`に`makeEmotesJson()`を追加、`makeCommentJson()`に`"emotes"`キーを追加。Twitch用
  EventBus購読ハンドラでのみ実データを渡す（YouTubeは常に空）
- `overlay.html`の`renderCommentText()`で`innerHTML`を使わず`createTextNode`/`createElement('img')`
  のみでコメント本文をテキスト・スタンプ画像に安全にトークン化して描画
- **設計判断**: Twitchの`emotes`タグのstart/endは UTF-16コード単位インデックス。C++側では変換せず
  そのまま運び、JS側の`String.slice()`（同じくUTF-16単位）でそのまま使うことで、日本語等の
  マルチバイト文字混在時のズレを回避
- YouTubeのカスタムメンバーエモートは公開APIが画像URLを返さないことを調査済み（`YouTubePlatform.cpp`
  にコメントで記録）。対応不可のため現状のテキスト表示のまま
- Node.jsでトークナイザーのロジック単体テスト実施（複数スタンプ、日本語混在、no-emote高速パス、
  不正レンジの防御的スキップ、すべて期待通り）
- ビルド確認済み（2026-07-15）。**実機未確認**（上記「未確認・保留中のタスク」参照）

### ポイントシステム課金連携機能実装（2026-07-14）
- `PluginConfig::pointBillingEnabled`（デフォルトtrue）/ `pointBillingRate`（デフォルト100.0 pt/USD）を追加
- `PointSettingsDialog`「設定」タブにチェックボックスとレート入力欄を追加
- `PointManager::awardBillingPoints()` を新規実装。`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` でTwitchのバックグラウンドスレッドからの呼び出しでも安全に `points_` を更新（`scheduleSave()` と同じパターン）
- `plugin-main.cpp` の Twitch Bits・サブスク、YouTube SC・スーパーステッカー・メンバーシップの各課金イベントハンドラに `awardBillingPoints()` 呼び出しを追加
- メンバーシップ分は `SupporterLedger::buildPlanUsdMapFromConfig()`（新規、`SupporterHistoryDialog::buildPlanUsdMap()` から共通化）で解決したUSD額を使用
- ビルド確認済み（2026-07-14）。**実機未確認**（上記「未確認・保留中のタスク」参照）

### メンバーシッププラン価格設定：実機検証完了（2026-07-14）
- テストデータ注入 → 500 JPYマッピング設定 → ビューアで $3.25 反映を確認。設計通りの動的遡及反映を確認
- 06/26投入の旧テストデータは対象外だったが、これは別セッションの古いテストデータによるもので設計上の問題ではない

### メンバーシッププラン価格設定機能実装（2026-06-26）
- **新規追加:** `MembershipPlanPriceDialog`（プラン名 → 価格・通貨 のマッピング設定UI）
- メニュー: `ツール → obs-live-hub → メンバーシッププラン価格設定`
- `PluginConfig::membershipPlanPrices` に永続化（`membership_plan_prices` JSON配列キー）
- `SupporterEvent::planName` フィールドを追加。メンバーシップ受信時に `levelName` を保存
- `SupporterLedger::resolveMembershipUsd` / `computeTotalUsd` で表示・集計時に動的USD計算
- `SupporterHistoryDialog` の金額欄・合計欄がプラン価格設定に連動して即時反映
- 過去のメンバーシップイベントにも遡って価格が反映される設計
- `injectTestData()` でメンバーシップイベントに `planName="テストメンバーレベル"` を設定
- ビルド確認済み（2026-06-26）

### サポーター（課金）履歴機能実装（2026-06-26）
- **新規追加:** `SupporterLedger`（課金履歴台帳）+ `SupporterHistoryDialog`（UIビューア）
- 対応イベント: YouTube スーパーチャット・スーパーステッカー・メンバーシップ系（金額なし）、Twitch Bits・サブスクリプション系
- DPAPI（CryptProtectData/CryptUnprotectData）で `supporter_ledger.dat` をユーザーアカウント固有に暗号化保存
- USD変換は固定概算レートテーブル（約40通貨）でオフライン計算
- メンバーシップ系（newSponsorEvent / memberMilestone / giftMembership）は金額情報なしのため件数のみ記録、USD合計に計上しない
- Twitch Bits: 100bits=$1、サブスクはTier1=$4.99 / Tier2=$9.99 / Tier3=$24.99でマッピング
- `ツール → obs-live-hub → サポーター履歴` で開く。配信中は警告ダイアログ付き
- ダイアログ内「テストデータ注入」ボタンでダミーデータを使った暗号化/復号/表示テスト可能
- YouTubePlatform に `superChatReceived` / `superStickerReceived` / `membershipReceived` シグナルを追加
- TwitchPlatform に `bitsReceived` / `subscriptionReceived` シグナルと USERNOTICE パース（`parseUserNotice`）を追加
- ビルド確認済み（2026-06-26）
- **実機確認済み（2026-06-26）**: テストデータ注入（8件）→ OBS再起動後の復号確認 → 配信中警告ダイアログ（Yes/Cancel両挙動）→ supporter_ledger.dat 暗号化目視確認、すべて問題なし

### 初心者向け導入ガイド作成（2026-06-25）
- `GETTING_STARTED.md` を新規作成。ダウンロード〜Twitch接続・TTS・YouTube・X投稿・FAQ を網羅
- `CLAUDE.md` に「初心者向けガイドは `GETTING_STARTED.md` を参照」の一文を追記

### X投稿機能一式（API投稿・手動投稿・テンプレート・リンク連携）（2026-06-24 完了）
**実機確認済み（2026-06-24）**: API投稿・手動投稿・テンプレート・Twitch/YouTubeリンク連携のすべてが動作確認済み。
- **X API 投稿**: `XClient`（OAuth 1.0a / WinHTTP HTTPS POST /2/tweets）、`XPostDock`（OBS ドック）、`XPostConfirmDialog`。`POST OK: HTTP 201` 確認
- **X 手動投稿**: `XManualPostDialog`（Web Intent 方式・X API 不要）。テンプレート選択 → 本文編集 → ブラウザで `x.com/intent/post` を開く。画像はクリップボードコピー
- **テンプレート管理**: `XTemplate` + `XTemplateSettingsDialog`。`includeTwitchLink`/`includeYoutubeLink` フラグでダイアログの初期チェック状態を保存
- **配信開始時の自動投稿モード**: 0=オフ / 1=API確認 / 2=手動投稿。`STREAMING_STARTED` 連動。ドック非表示時はメニュー（`ツール → X投稿 → 配信開始時の自動投稿設定`）から設定可
- **YouTube URL 動的取得**: `YouTubePlatform::broadcastResolved` シグナルでダイアログを開いたまま URL が自動更新。配信開始後 15〜30秒のラグは仕様
- **スコープ外（Phase 2）**: 画像添付・リンクURL自動生成・テンプレートプレースホルダー展開（`imagePath` フィールドは構造体に確保済み）

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
