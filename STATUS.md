# STATUS.md

## 現在のタスク: TTS複数エンジン対応 Step4b

### 直近の進捗
- WebSocket接続問題は解決済み（PC再起動でゾンビソケット解消、詳細はCLAUDE_LOG.md参照）
- Step4a/4bの動作確認実施、オーバーレイ・TTS読み上げ自体は正常動作確認済み

### 現在進行中のバグ修正
[olh] engine:xxx コマンドで個人設定したエンジンが反映されない問題
- 原因判明: buildCommentTtsJson内のisEngineConnected()チェックが
  "aivisspeech"を未接続と誤判定し、強制的にwebspeechへ上書きしていた
  （plugin-main.cpp:172-173）
- 対応方針: 設定ダイアログにON/OFFチェックボックスを追加し、
  ユーザー側で接続チェックの有効/無効を選べるようにする
  → Claude Codeに実装依頼済み、ビルド結果待ち

### tts.html fetch エラー調査結果

エラー: `AivisSpeech error: TypeError: Failed to fetch`
（ソース位置 `http://absolute/C:/.../tts.html:161` は Chrome が `--disable-web-security` で開いたファイルのURL表現であり、fetch先のURLではない）

#### Q1. URL 組み立て（tts.html L.122, L.128-131）

```javascript
// speakWithAivis 内
var url = baseUrl || aivisUrl;          // L.122
var sid = (styleId !== undefined && styleId !== null) ? styleId : aivisStyleId;

fetch(url + '/audio_query?text=...' + '&speaker=' + sid, { method: 'POST' })
```

`url + '/audio_query?...'` の単純な文字列連結。`baseUrl` が空文字(falsy)なら `aivisUrl` にフォールバックする構造。

#### Q2. baseUrl の受け取り（tts.html L.175）

```javascript
var effectiveBaseUrl = info.baseUrl || aivisUrl;   // info = comment の tts フィールド
```

C++ 側 `engineBaseUrl("aivisspeech")` = `cfg.aivisUrl` = `"http://localhost:10101"` が JSON に埋め込まれるため、`info.baseUrl = "http://localhost:10101"` (truthy) となり、`effectiveBaseUrl = "http://localhost:10101"` に正常解決される。**URL構築にバグなし。**

参考: `engine = "webspeech"` 時は `engineBaseUrl` が空文字を返し `info.baseUrl = ""`(falsy) になるが、その場合 `isVoicevoxEngine(effectiveEngine)` が false なので `speakWithAivis` 自体が呼ばれない（問題なし）。

#### Q3. styleId=0 のガード有無

`speakWithAivis` に入る前・入った後ともに `styleId=0` を弾くガードは存在しない。`sid = 0` はそのまま `speaker=0` として AivisSpeech に渡される。

- AivisSpeech が起動しておらず応答しない → `TypeError: Failed to fetch`（ネットワーク到達失敗）
- AivisSpeech が起動しているが `speaker=0` が無効 → `Error: audio_query failed: 422`（サーバーエラー）

`styleId=0` が `TypeError: Failed to fetch` を引き起こすことはない。

#### Q4. 発生条件の切り分け

`TypeError: Failed to fetch` の唯一の原因は **AivisSpeech が `http://localhost:10101` で起動していない・応答しない** こと。

| 状況 | 発生するエラー |
|---|---|
| AivisSpeech 未起動 | `TypeError: Failed to fetch`（ネットワーク接続失敗） |
| AivisSpeech 起動中・`speaker=0` が無効 | `Error: audio_query failed: 422` |
| AivisSpeech 起動中・`speaker=0` が有効 | エラーなし（正常に読み上げ） |

`ttsCheckEngineConnection=false` にする前は `isEngineConnected("aivisspeech")` が false → engine を "webspeech" に上書き → `speakWithAivis` が一切呼ばれなかったため、このエラーが表面化しなかった。チェックをOFFにしたことで `speakWithAivis` が実際に呼ばれるようになり、エンジン未起動の状態が露呈した。

**エンジン切り替え直後のみ発生するわけではなく、AivisSpeech が起動していなければ毎回発生する。**

---

### AivisEngine::start() が呼ばれない問題 — 根本原因と修正（2026-06-18）

#### 根本原因

`TtsSpeechDialog::saveToConfig()` が `cfg.ttsEngine = "aivisspeech"` を保存する際、
`cfg.aivisspeechEnabled` を `true` にしていなかった。

- `cfg.save()` は `aivisspeech_enabled: false`（デフォルト値のまま）をJSONに書き込む
- 次回起動時、`PluginConfig::load()` はキーが存在するため移行ロジックをスキップし、`aivisspeechEnabled = false` のままにする
- `EngineManager::startAll()` の `if (!e.enabled) continue;` でaivisspeechをスキップ
- → EngineManagerにaivisspeechのログが一切出ない、`isEngineConnected("aivisspeech")` がfalseを返す

#### 修正内容

`src/ui/TtsSpeechDialog.cpp` の `saveToConfig()` 末尾（`cfg.save()` 直前）に追加:

```cpp
cfg.aivisspeechEnabled = (cfg.ttsEngine == "aivisspeech");
cfg.sharevoxEnabled    = (cfg.ttsEngine == "sharevox");
cfg.lmroidEnabled      = (cfg.ttsEngine == "lmroid");
cfg.itvoiceEnabled     = (cfg.ttsEngine == "itvoice");
cfg.bouyomiEnabled     = (cfg.ttsEngine == "bouyomi");
```

これにより、ダイアログ保存時にグローバルエンジン選択と `*Enabled` フラグが常に同期される。

#### CORSエラーについて

`[obs-browser: '音声ブラウザ']` のCORSエラーは、AivisSpeechが `--allow_origin "*"` なしで
手動起動されているために発生。AivisSpeechを正しく起動する方法:

1. 設定ダイアログの「起動」ボタン → `AivisEngine::start()` が `--allow_origin "*"` 付きで起動
2. 設定ダイアログで「OBS起動時に自動起動」をON + エンジンパスを設定 → `EngineManager::startAll()` が自動起動
3. 手動起動する場合は `--allow_origin "*"` フラグを自分で付ける必要がある

OBSブラウザソースは `Access-Control-Allow-Origin: *` レスポンスヘッダーを尊重するため、
AivisSpeechが `--allow_origin "*"` で起動していればCORSエラーは解消される。

#### ビルド・デプロイ結果

- ビルド: 成功（2026-06-18）
- デプロイ先: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub.dll`

---

### TTSエンジン「有効化」と「デフォルト」分離 UI実装（2026-06-18）

#### 変更内容

`src/ui/TtsSpeechDialog.hpp` / `.cpp`

- `QComboBox *engineCombo_` を廃止
- 新UIとして `engineListGroup_` (QGroupBox + QGridLayout) を追加:
  - 列1: 「有効化」QCheckBox（webspeechは常時有効でdisabled）
  - 列2: 「デフォルト」QRadioButton（エンジン名付き、有効化時のみenabled）
- スロット: `onEngineChanged(int)` → `onDefaultEngineChanged(int)` + `onEngineEnabledToggled(int, bool)` に分割
- `loadFromConfig()`: 各エンジンの `*Enabled` フラグからチェックボックスを初期化、`ttsEngine` からラジオを選択
- `saveToConfig()`: `cfg.*Enabled` をチェックボックス状態から保存（グローバル選択に同期する5行を廃止）

#### 設計上の変更

| 旧 | 新 |
|---|---|
| ttsEngine選択 → そのエンジンのみ*Enabled=true、他はfalse | 各エンジンを独立して有効化可能 |
| [olh] engine:xxx でグローバル以外は常に起動されていない | 有効化されたエンジンはすべてEngineManagerが管理 |
| engineCombo_ で1つ選択 | 有効化チェック＋デフォルトラジオで分離管理 |

#### ビルド・デプロイ結果

- ビルド: 成功（2026-06-18）
- デプロイ先: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub.dll`

---

### 次にやること

1. OBSを再起動して動作確認:
   - 「obs-live-hub 読み上げ設定」を開き、AivisSpeechの「有効化」チェックをON、
     「デフォルト」ラジオをAivisSpeechに設定して保存
   - OBSログに `[EngineManager] aivisspeech: Starting` が出ることを確認
   - 必要なら他エンジン（sharevox等）も「有効化」だけONにして同時起動を確認
2. AivisSpeechをダイアログの「起動」ボタン（または自動起動設定）で起動し直す
   （--allow_origin "*" フラグ付き起動が必要）
3. 複数エンジン有効化後、視聴者ごとに `[olh] engine:xxx` を送って
   それぞれのエンジンで読み上げされることを確認
4. ttsCheckEngineConnection を true に戻してテスト
5. デバッグ用ログ（[WsServer] [DBG] tick #N等）の削除をClaude Codeに依頼する
   （本番運用前に必須）
