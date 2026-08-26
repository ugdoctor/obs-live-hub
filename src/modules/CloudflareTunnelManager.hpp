/*
obs-live-hub
Copyright (C) 2026 ugdoctor

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <cstdint>

// WAN公開対応: Cloudflare Tunnel（cloudflared）の「クイックトンネル」機能を管理する。
// クイックトンネルはCloudflareアカウント登録・DNS設定・ポート開放のいずれも不要で、
// `cloudflared tunnel --url http://127.0.0.1:<port>` を実行するだけで
// `https://<ランダム文字列>.trycloudflare.com` という公開URLが即座に発行される
// （trycloudflare.comは動作確認・一時利用向けの無料エンドポイントで、Cloudflareが
// 恒久的な可用性を保証するものではない点に注意。長期運用には別途Named Tunnel等が必要）。
// cloudflared.exe自体の起動・停止・出力からのURL抽出のみを責務とし、UI表示は
// GlobalServerSettingsDialogが担当する。
class CloudflareTunnelManager : public QObject {
	Q_OBJECT
public:
	enum class Status {
		NotInstalled, // cloudflared.exeが未配置
		Stopped,      // 配置済みだが未起動
		Starting,     // プロセス起動済み・公開URL未確定
		Published     // 公開URL確定済み（実際に外部から到達可能とは限らない。あくまで
		              // cloudflared自身がトンネル確立を報告した状態）
	};
	Q_ENUM(Status)

	explicit CloudflareTunnelManager(QObject *parent = nullptr);
	~CloudflareTunnelManager() override;

	// %APPDATA%\obs-studio\plugins\obs-live-hub\tools\cloudflared.exe
	static QString exePath();
	static bool isInstalled();

	// 公式Windowsバイナリ（GitHub Releases）をダウンロードしexePath()へ保存する。
	// 実機バグ対策: OBSに同梱されるQtにはTLSバックエンド（OpenSSL等）が含まれておらず、
	// QNetworkAccessManagerでhttps://へ接続すると
	// 「QSslSocket::connectToHostEncrypted: TLS initialization failed」で失敗していた。
	// Windows標準のWinHTTP（SChannelがTLSを処理、GoogleOAuth.cpp::doWinHttpPost()と同じ
	// 方針）を使い、Qt/obs-depsのTLS実装に依存しないようにする。ダウンロード自体は
	// バックグラウンドのstd::thread上でブロッキング実行し、完了はQMetaObject::invokeMethod
	// でこのオブジェクトが属するスレッド（GUIスレッド）へ戻してからdownloadFinished()を
	// emitする（Qtオブジェクトの状態はGUIスレッドからのみ書き換える）。
	void downloadCloudflared();
	// 現在の実装ではダウンロードは同期的なWinHTTP呼び出しをバックグラウンドスレッドで
	// 実行するのみで、実行中のリクエストを安全に中断する手段を持たない（cloudflared.exeは
	// 数十MB程度でダウンロードは短時間のため、現時点では未実装のプレースホルダとする）。
	void cancelDownload();

	// cloudflared.exeを`tunnel --url http://127.0.0.1:<localPort>`で起動する。
	// 既に起動中の場合は何もしない（先にstopTunnel()すること）。
	void startTunnel(uint16_t localPort);
	// プロセスを安全に終了する（terminate()→タイムアウトでkill()）。
	void stopTunnel();

	Status status() const { return status_; }
	QString publicUrl() const { return publicUrl_; }
	bool isRunning() const;

signals:
	void downloadProgress(qint64 received, qint64 total);
	void downloadFinished(bool success, const QString &errorMessage);
	void statusChanged(CloudflareTunnelManager::Status status);
	void urlResolved(const QString &url);
	// 生の標準出力/標準エラー行（診断表示用）。
	void logLine(const QString &line);

private slots:
	void onProcessReadyReadStdout();
	void onProcessReadyReadStderr();
	void onProcessErrorOccurred(QProcess::ProcessError error);
	void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
	void setStatus(Status s);
	void handleOutputChunk(const QString &chunk);

	QProcess *process_ = nullptr;
	QString publicUrl_;
	QString outputBuffer_; // 行単位に切り出すためのバッファ（チャンクが行の途中で切れる対策）
	Status status_ = Status::Stopped;
	// downloadCloudflared()の多重起動防止。GUIスレッド上でのみ読み書きする
	// （設定・解除の両方がGUIスレッド側のコードからのみ行われるよう設計しているため、
	// アトミック化は不要）。
	bool downloading_ = false;
};
