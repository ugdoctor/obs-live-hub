# obs-live-hub

OBS Studio 用ライブ配信支援プラグイン（Windows 専用）。

Twitch と YouTube Live のコメントを同時表示し、TTS 読み上げ・エフェクト・
ポイントシステム・アンケート・配信設定一括管理などの機能を提供します。

## 動作環境

- Windows 10/11 (64bit)
- OBS Studio 31.x 以降

## 主な機能

- **コメント表示**: Twitch / YouTube Live のコメントをOBSドック＆ブラウザオーバーレイに表示
- **TTS読み上げ**: 6種類のエンジンを同時有効化・視聴者ごとにエンジン/話者/パラメータをカスタマイズ可能
- **視聴者コメントコマンド** (`[olh]`): チャットコマンドでコメント色・TTS設定を変更
- **エフェクトシステム**: コメントコマンドでブラウザソース上のエフェクトを発火
- **ポイントシステム**: 視聴者にポイントを付与し、コマンドで消費できるアクションを設定
- **アンケート機能**: `!vote` コマンドによる選択式・自由回答アンケート
- **配信情報一括設定**: タイトル・カテゴリ等を Twitch / YouTube へ同時送信
- **TTS読み上げ辞書**: 単語の読み方を CSV で管理
- **オーバーレイカスタマイズ**: コメントカードの外観をUI から設定

## 対応プラットフォーム

| プラットフォーム | 接続方式 | 認証 |
|---|---|---|
| Twitch | IRC over TLS | OAuth Implicit Grant |
| YouTube Live | Data API v3 ポーリング | OAuth 2.0 |

## 対応 TTS エンジン

| エンジン | 種別 | デフォルトポート |
|---|---|---|
| Web Speech API | ブラウザ内蔵 | — |
| AivisSpeech | VOICEVOX互換 | localhost:10101 |
| SHAREVOX | VOICEVOX互換 | localhost:50025 |
| LMROID | VOICEVOX互換 | localhost:49973 |
| ITVOICE | VOICEVOX互換 | localhost:49540 |
| 棒読みちゃん | 独自 HTTP API | localhost:50080 |

## インストール方法

1. [Releases](../../releases) から最新の `obs-live-hub.dll` をダウンロード
2. `obs-live-hub.dll` を `C:\Program Files\obs-studio\obs-plugins\64bit\` にコピー
3. `data\` フォルダ内のファイル（`overlay.html` / `tts.html` / `effect.html` / `debug.html`）を
   `%APPDATA%\obs-studio\plugins\obs-live-hub\` にコピー  
   （フォルダが存在しない場合は作成してください）
4. OBS を起動

## 初期設定

`config.json.example` をコピーして `config.json` にリネームし、
OBS のツールメニュー「obs-live-hub 設定」から各項目を設定してください。

### Twitch 設定

1. [Twitch Developer Console](https://dev.twitch.tv/console) でアプリを登録
2. OAuth Redirect URL に `http://localhost` を追加
3. OBS ツールメニュー「obs-live-hub 設定」→「Twitch アカウントと連携」ボタンで認証

### YouTube 設定

1. [Google Cloud Console](https://console.cloud.google.com/) でプロジェクトを作成
2. YouTube Data API v3 を有効化
3. OAuth 2.0 クライアント ID（デスクトップアプリ）を作成
4. OBS ツールメニュー「obs-live-hub 設定」→「Google アカウントと連携」ボタンで認証

## OBS ブラウザソース設定

| 用途 | URL |
|---|---|
| コメント表示オーバーレイ | `%APPDATA%\obs-studio\plugins\obs-live-hub\overlay.html` |
| TTS 音声（音声モニタリングを ON に設定） | `%APPDATA%\obs-studio\plugins\obs-live-hub\tts.html` |
| エフェクト表示 | `%APPDATA%\obs-studio\plugins\obs-live-hub\effect.html` |

## ビルド方法

```
# Developer PowerShell for VS で実行
cmake --preset windows-x64
cmake --build --preset windows-x64
# 生成物: build_x64/RelWithDebInfo/obs-live-hub.dll
```

## ライセンス

GPL-2.0-or-later（[LICENSE](LICENSE) を参照）
