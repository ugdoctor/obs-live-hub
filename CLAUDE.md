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

# 初回・設定変更時
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# ビルド
cmake --build build --config RelWithDebInfo

# 生成物
# build/RelWithDebInfo/plugintemplate-for-obs.dll
```

## プロジェクト構成
```
obs-comment-viewer/
├── CLAUDE.md          # このファイル（仕様・方針）
├── STATUS.md          # 現在のタスク・次にやること
├── CLAUDE_LOG.md      # 開発履歴（追記のみ）
├── CMakeLists.txt     # ビルド設定
├── buildspec.json     # OBSバージョン指定
└── src/
    ├── plugin-main.cpp         # エントリーポイント
    ├── core/                   # コアエンジン
    │   ├── EventBus.hpp        # モジュール間イベント通信
    │   ├── PluginConfig.hpp    # 設定管理
    │   └── PlatformInterface.hpp # プラットフォーム抽象レイヤー
    ├── platforms/              # 各配信プラットフォーム
    │   ├── YouTubePlatform.hpp
    │   └── TwitchPlatform.hpp
    ├── ui/                     # Qt6 UIコンポーネント
    │   ├── CommentDock.hpp     # コメントビューワードック
    │   └── SettingsDialog.hpp  # 設定ダイアログ
    └── modules/                # 拡張モジュール（将来）
        └── (AIFilter, StreamInfo, ExternalBridge...)
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

### Phase 1（現在）：コメントビューワー基盤
- [ ] プロジェクト名・構成のリネーム
- [ ] OBSドックパネルにコメントリスト表示（Qt6）
- [ ] YouTubeLive コメント取得（API or WebSocket）
- [ ] Twitch コメント取得（IRC or EventSub）
- [ ] 基本フィルタリング（NGワード等）
- [ ] 設定ダイアログ（APIキー・接続設定）

### Phase 2（将来）：拡張機能
- [ ] 配信情報表示（視聴者数・経過時間）
- [ ] AIによるコメント処理（要約・感情分析・NG判定）
- [ ] 外部アプリ連携（WebSocket サーバー機能）
- [ ] コメント演出（特定コメントのハイライト等）

## 重要な制約・注意事項
- OBS APIはメインスレッドとの扱いに注意（UI更新はQtのシグナル経由）
- YouTube Data API v3 のクォータ制限に注意
- Twitch IRC接続はOAuthトークンが必要
- DLLはOBSのプラグインフォルダに配置して動作確認
- `CLAUDE_LOG.md` は開発履歴ログのみ。調査・参照不要。読み込まないこと。

## OBSプラグインフォルダ（Windows）
```
C:\Program Files\obs-studio\obs-plugins\64bit\
```
または
```
%APPDATA%\obs-studio\plugins\
```

## 参考リンク
- OBS Plugin API: https://obsproject.com/docs/
- obs-plugintemplate: https://github.com/obsproject/obs-plugintemplate
- YouTube Live Streaming API: https://developers.google.com/youtube/v3/live
- Twitch EventSub: https://dev.twitch.tv/docs/eventsub/