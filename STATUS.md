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
