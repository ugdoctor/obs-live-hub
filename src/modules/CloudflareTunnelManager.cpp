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

#include "CloudflareTunnelManager.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>

#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

static const char *CFTAG = "CloudflareTunnel";

#ifdef _WIN32
// 公式Windows(amd64)バイナリの固定ダウンロード先。"latest/download/..."はGitHubが常に
// 最新リリースへ302リダイレクトするエンドポイントのため、バージョン追従の手間がない。
static const wchar_t *CLOUDFLARED_DOWNLOAD_HOST = L"github.com";
static const wchar_t *CLOUDFLARED_DOWNLOAD_PATH =
	L"/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe";
#endif

// クイックトンネル確立時にcloudflaredが標準エラー等へ出力する公開URLを抽出する正規表現。
static const char *TUNNEL_URL_PATTERN = R"(https://[a-zA-Z0-9-]+\.trycloudflare\.com)";

QString CloudflareTunnelManager::exePath()
{
#ifdef _WIN32
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return QString::fromWCharArray(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\tools\\cloudflared.exe";
#else
	return {};
#endif
}

bool CloudflareTunnelManager::isInstalled()
{
	return QFileInfo::exists(exePath());
}

CloudflareTunnelManager::CloudflareTunnelManager(QObject *parent) : QObject(parent)
{
	status_ = isInstalled() ? Status::Stopped : Status::NotInstalled;
}

CloudflareTunnelManager::~CloudflareTunnelManager()
{
	// WAN公開対応: OBS終了時にcloudflaredプロセスが残り続けると、次回起動時に同じ
	// ローカルポートへ古いトンネルが張られたまま・タスクマネージャにゾンビプロセスが
	// 残る等の問題につながるため、デストラクタでも確実に終了させる。
	stopTunnel();
	cancelDownload();
}

void CloudflareTunnelManager::setStatus(Status s)
{
	if (status_ == s)
		return;
	status_ = s;
	emit statusChanged(s);
}

bool CloudflareTunnelManager::isRunning() const
{
	return process_ && process_->state() != QProcess::NotRunning;
}

#ifdef _WIN32
// 実機バグ対策: OBS同梱のQtにはTLSバックエンド（OpenSSL等）が含まれておらず、
// QNetworkAccessManagerでhttps://へ接続すると
// 「QSslSocket::connectToHostEncrypted: TLS initialization failed / No functional TLS
// backend was found」で失敗していた。Windows標準のWinHTTP（TLS処理はSChannelが行うため
// Qt/obs-depsのTLS実装に依存しない）を使う。GoogleOAuth.cpp::doWinHttpPost()と同じ方針。
// 同期的にブロッキングするため、必ずGUIスレッド以外（バックグラウンドスレッド）から
// 呼び出すこと。
static bool winHttpDownloadToFile(const wchar_t *host, const wchar_t *path,
                                   const std::wstring &destPath, std::string &outError,
                                   const std::function<void(qint64, qint64)> &onProgress)
{
	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		outError = "WinHttpOpen failed (err=" + std::to_string(GetLastError()) + ")";
		return false;
	}

	HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		outError = "WinHttpConnect failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
	                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		outError = "WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	// GitHub Releasesの"latest/download/..."はアセット本体（objects.githubusercontent.com等、
	// 別ホスト）へ302リダイレクトする。WinHTTPは既定でも同一プロトコル間のリダイレクトは
	// 自動追従するが、プロトコルが変わるリダイレクトでも確実に追従されるよう
	// WINHTTP_OPTION_REDIRECT_POLICY_ALWAYSを明示的に設定する。
	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
	                  sizeof(redirectPolicy));

	const bool sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
	                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (!sent) {
		outError = "WinHttpSendRequest failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	if (!WinHttpReceiveResponse(hRequest, nullptr)) {
		outError = "WinHttpReceiveResponse failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD statusCode = 0;
	DWORD statusCodeSize = sizeof(statusCode);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                     WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
	                     WINHTTP_NO_HEADER_INDEX);
	if (statusCode != 200) {
		outError = "HTTP status " + std::to_string(statusCode) + "（200を期待）";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD contentLength = 0;
	DWORD contentLengthSize = sizeof(contentLength);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
	                     WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize,
	                     WINHTTP_NO_HEADER_INDEX);

	std::vector<uint8_t> data;
	data.reserve(contentLength > 0 ? contentLength : (1024 * 1024));

	DWORD bytesAvailable = 0;
	while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
		const size_t oldSize = data.size();
		data.resize(oldSize + bytesAvailable);
		DWORD bytesRead = 0;
		if (!WinHttpReadData(hRequest, data.data() + oldSize, bytesAvailable, &bytesRead)) {
			outError = "WinHttpReadData failed (err=" + std::to_string(GetLastError()) + ")";
			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return false;
		}
		data.resize(oldSize + bytesRead);
		if (onProgress)
			onProgress(static_cast<qint64>(data.size()), static_cast<qint64>(contentLength));
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (data.empty()) {
		outError = "ダウンロードしたデータが空でした。";
		return false;
	}

	// 一時ファイルへ書き込んでから置換する（書き込み失敗時に既存ファイルを壊さない。
	// 旧実装（QSaveFile）と同じ意図をWin32 APIで再現する）。
	const std::wstring tmpPath = destPath + L".tmp";
	HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                            FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		outError = "ファイルを開けませんでした (err=" + std::to_string(GetLastError()) + ")";
		return false;
	}
	DWORD written = 0;
	const bool writeOk = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written,
	                                nullptr) &&
	                      written == data.size();
	CloseHandle(hFile);
	if (!writeOk) {
		outError = "ファイルの書き込みに失敗しました";
		DeleteFileW(tmpPath.c_str());
		return false;
	}

	if (!MoveFileExW(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		outError = "ファイルの置換に失敗しました (err=" + std::to_string(GetLastError()) + ")";
		DeleteFileW(tmpPath.c_str());
		return false;
	}

	return true;
}
#endif

void CloudflareTunnelManager::downloadCloudflared()
{
	if (downloading_) {
		obs_log(LOG_WARNING, "[%s] downloadCloudflared: 既にダウンロード中です", CFTAG);
		return;
	}

#ifdef _WIN32
	downloading_ = true;

	const QString path = exePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	const std::wstring destPathW = path.toStdWString();

	obs_log(LOG_INFO, "[%s] cloudflared.exeのダウンロードを開始します（WinHTTP経由）: %s", CFTAG,
		path.toUtf8().constData());

	// ダウンロード処理自体（WinHTTPのブロッキング呼び出し）はバックグラウンドスレッドで
	// 実行し、OBSのUIスレッドをブロックしない。完了・進捗通知は
	// QMetaObject::invokeMethod(this, ...)でこのオブジェクトが属するGUIスレッドへ戻して
	// から行う——thisをコンテキストに指定することで、万一オブジェクトが破棄された後に
	// バックグラウンドスレッドが完了した場合でもQtが安全にキューイング済み呼び出しを
	// 破棄してくれる（Qt 5.10以降のcontext-aware invokeMethodの保証）。
	std::thread([this, destPathW]() {
		std::string error;
		const bool ok = winHttpDownloadToFile(
			CLOUDFLARED_DOWNLOAD_HOST, CLOUDFLARED_DOWNLOAD_PATH, destPathW, error,
			[this](qint64 received, qint64 total) {
				QMetaObject::invokeMethod(
					this, [this, received, total]() { emit downloadProgress(received, total); },
					Qt::QueuedConnection);
			});

		QMetaObject::invokeMethod(
			this,
			[this, ok, error]() {
				downloading_ = false;
				if (ok) {
					obs_log(LOG_INFO, "[%s] cloudflared.exeのダウンロードが完了しました",
						CFTAG);
					setStatus(Status::Stopped);
					emit downloadFinished(true, QString());
				} else {
					obs_log(LOG_WARNING,
						"[%s] cloudflared.exeのダウンロードに失敗しました: %s", CFTAG,
						error.c_str());
					emit downloadFinished(false, QString::fromStdString(error));
				}
			},
			Qt::QueuedConnection);
	}).detach();
#else
	emit downloadFinished(false, "Windows以外では未対応です。");
#endif
}

void CloudflareTunnelManager::cancelDownload()
{
	// ヘッダのコメント参照: 現在の実装は同期的なWinHTTP呼び出しをバックグラウンド
	// スレッドで実行するのみで、実行中のリクエストを安全に中断する手段を持たない。
}

void CloudflareTunnelManager::startTunnel(uint16_t localPort)
{
	if (isRunning()) {
		obs_log(LOG_WARNING, "[%s] startTunnel: 既に起動中です", CFTAG);
		return;
	}
	if (!isInstalled()) {
		obs_log(LOG_WARNING, "[%s] startTunnel: cloudflared.exeが未配置です", CFTAG);
		return;
	}

	publicUrl_.clear();
	outputBuffer_.clear();

	process_ = new QProcess(this);
	process_->setProgram(exePath());
	process_->setArguments(
		{"tunnel", "--url", QStringLiteral("http://127.0.0.1:%1").arg(localPort)});

	QObject::connect(process_, &QProcess::readyReadStandardOutput, this,
	                  &CloudflareTunnelManager::onProcessReadyReadStdout);
	QObject::connect(process_, &QProcess::readyReadStandardError, this,
	                  &CloudflareTunnelManager::onProcessReadyReadStderr);
	QObject::connect(process_, &QProcess::errorOccurred, this,
	                  &CloudflareTunnelManager::onProcessErrorOccurred);
	QObject::connect(process_,
	                  static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
	                  this, &CloudflareTunnelManager::onProcessFinished);

	obs_log(LOG_INFO, "[%s] cloudflaredを起動します: tunnel --url http://127.0.0.1:%u", CFTAG,
		static_cast<unsigned>(localPort));
	setStatus(Status::Starting);
	process_->start();
}

void CloudflareTunnelManager::stopTunnel()
{
	if (!process_)
		return;

	if (process_->state() != QProcess::NotRunning) {
		obs_log(LOG_INFO, "[%s] cloudflaredプロセスを終了します", CFTAG);
		process_->terminate();
		if (!process_->waitForFinished(3000)) {
			obs_log(LOG_WARNING, "[%s] terminate()がタイムアウトしたためkill()します", CFTAG);
			process_->kill();
			process_->waitForFinished(2000);
		}
	}

	process_->deleteLater();
	process_ = nullptr;
	publicUrl_.clear();
	setStatus(isInstalled() ? Status::Stopped : Status::NotInstalled);
}

void CloudflareTunnelManager::onProcessReadyReadStdout()
{
	if (process_)
		handleOutputChunk(QString::fromUtf8(process_->readAllStandardOutput()));
}

void CloudflareTunnelManager::onProcessReadyReadStderr()
{
	if (process_)
		handleOutputChunk(QString::fromUtf8(process_->readAllStandardError()));
}

void CloudflareTunnelManager::handleOutputChunk(const QString &chunk)
{
	// cloudflaredの出力は1回のreadAllStandardError()呼び出しでは行の途中までしか
	// 届かないことがあるため、バッファへ蓄積してから改行単位で切り出す。
	outputBuffer_ += chunk;
	int newlineIdx;
	while ((newlineIdx = outputBuffer_.indexOf('\n')) >= 0) {
		QString line = outputBuffer_.left(newlineIdx).trimmed();
		outputBuffer_.remove(0, newlineIdx + 1);
		if (line.isEmpty())
			continue;

		emit logLine(line);

		if (publicUrl_.isEmpty()) {
			static const QRegularExpression urlRe(QString::fromUtf8(TUNNEL_URL_PATTERN));
			const QRegularExpressionMatch m = urlRe.match(line);
			if (m.hasMatch()) {
				publicUrl_ = m.captured(0);
				obs_log(LOG_INFO, "[%s] トンネルURLを検出しました: %s", CFTAG,
					publicUrl_.toUtf8().constData());
				setStatus(Status::Published);
				emit urlResolved(publicUrl_);
			}
		}
	}
}

void CloudflareTunnelManager::onProcessErrorOccurred(QProcess::ProcessError error)
{
	obs_log(LOG_WARNING, "[%s] cloudflaredプロセスエラー: %d", CFTAG, static_cast<int>(error));
}

void CloudflareTunnelManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	obs_log(LOG_INFO, "[%s] cloudflaredプロセスが終了しました (exitCode=%d, exitStatus=%d)", CFTAG,
		exitCode, static_cast<int>(exitStatus));
	publicUrl_.clear();
	if (process_) {
		process_->deleteLater();
		process_ = nullptr;
	}
	setStatus(isInstalled() ? Status::Stopped : Status::NotInstalled);
}
