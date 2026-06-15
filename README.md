# obs-live-hub

OBS Studio用ライブ配信支援プラグイン（Windows専用）

TwitchとYouTube Liveのコメントを同時表示し、
TTS読み上げ・アンケート・配信設定一括管理などの機能を提供します。

## 動作環境
- Windows 10/11 (64bit)
- OBS Studio 32.x

## 主な機能
- Twitch / YouTube Live コメント同時表示
- TwitchとYouTubeへの同時配信サポート（obs-multi-rtmp連携）
- コメント読み上げTTS（Web Speech API）
- TTS読み上げ辞書（CSV管理）
- 視聴者コマンド（[olh]）でコメント色・音声カスタマイズ
- アンケート機能（選択肢形式・自由記述形式）
- 配信情報一括設定（タイトル・カテゴリ等）
- オーバーレイ外観カスタマイズ

## インストール方法

1. [Releases](../../releases) から最新の `obs-live-hub.dll` をダウンロード
2. `obs-live-hub.dll` を `C:\Program Files\obs-studio\obs-plugins\64bit\` にコピー
3. `data\` フォルダ内の `overlay.html` と `tts.html` を
   `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-hub\` にコピー
4. OBSを起動

## 初期設定

### 設定ファイル
`config.json.example` をコピーして `config.json` にリネームし、
OBSのツールメニュー「obs-live-hub 設定」から各項目を設定してください。

### Twitch設定
1. [Twitch Developer Console](https://dev.twitch.tv/console) でアプリを登録
2. OAuth Redirect URLに `http://localhost` を追加
3. OBSツールメニュー「obs-live-hub 設定」→「Twitchアカウントと連携」

### YouTube設定
1. [Google Cloud Console](https://console.cloud.google.com/) でプロジェクトを作成
2. YouTube Data API v3 を有効化
3. OAuth 2.0クライアントID（デスクトップアプリ）を作成
4. OBSツールメニュー「obs-live-hub 設定」→「Googleアカウントと連携」

## OBSブラウザソース設定
- コメント表示用: `%APPDATA%\obs-studio\plugins\obs-live-hub\overlay.html`
- TTS音声用: `%APPDATA%\obs-studio\plugins\obs-live-hub\tts.html`
  （音声モニタリングをONに設定）

## ビルド方法
```
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
```

## ライセンス
MIT License
