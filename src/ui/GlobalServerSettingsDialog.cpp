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

#include "GlobalServerSettingsDialog.hpp"
#include "../core/PluginConfig.hpp"
#include "../modules/WsServer.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

GlobalServerSettingsDialog::GlobalServerSettingsDialog(CloudflareTunnelManager *tunnel, WsServer *wsServer,
                                                         QWidget *parent)
	: QDialog(parent), tunnel_(tunnel), wsServer_(wsServer)
{
	setWindowTitle("グローバルサーバー設定 (WAN公開)");
	setMinimumWidth(440);

	auto *warnLabel = new QLabel(
		"⚠️ Cloudflare Tunnel（クイックトンネル）を使い、ポート開放なしでVRM Stageを"
		"インターネットへ公開します。公開URLを知る誰でも接続を試行できるため、"
		"「ホスト設定」のセキュリティレベル・招待コードと併用してください。"
		"trycloudflare.comは一時利用向けの無料エンドポイントで、Cloudflareが"
		"恒久的な可用性を保証するものではありません。",
		this);
	warnLabel->setWordWrap(true);
	warnLabel->setStyleSheet("color: #cc8844;");

	statusLabel_ = new QLabel(this);
	statusLabel_->setTextFormat(Qt::RichText);

	downloadProgress_ = new QProgressBar(this);
	downloadProgress_->setRange(0, 100);
	downloadProgress_->setVisible(false);

	setupBtn_ = new QPushButton("Cloudflare Tunnel を自動セットアップ", this);
	startStopBtn_ = new QPushButton(this);

	auto *btnRow = new QHBoxLayout();
	btnRow->addWidget(setupBtn_);
	btnRow->addWidget(startStopBtn_);

	urlEdit_ = new QLineEdit(this);
	urlEdit_->setReadOnly(true);
	urlEdit_->setPlaceholderText("トンネル起動後、ここに公開URLが表示されます");

	copyUrlBtn_ = new QPushButton("公開URLをコピー", this);
	copyInviteLinkBtn_ = new QPushButton("ゲスト用招待リンクをコピー", this);
	openControllerBtn_ = new QPushButton("Controllerをブラウザで開く", this);

	auto *urlBtnRow = new QHBoxLayout();
	urlBtnRow->addWidget(copyUrlBtn_);
	urlBtnRow->addWidget(copyInviteLinkBtn_);

	hintLabel_ = new QLabel(
		"ゲストには「ゲスト用招待リンク」＋ホスト画面に表示される招待コードの両方を"
		"共有してください（リンクを開いてvrm_stage.htmlを表示 → 招待コードを入力）。",
		this);
	hintLabel_->setWordWrap(true);
	hintLabel_->setStyleSheet("opacity: 0.75; font-size: small;");

	// OBSの前回終了時のゾンビプロセス等でポート28765/39540が占有されたまま
	// サーバーが起動できない場合の非常手段。「controllerをブラウザで開く」ボタンの
	// すぐ下に、誤操作を避けるため独立した行として配置する。
	resolveConflictBtn_ = new QPushButton("⚠️ ポート競合の強制解消 (ゾンビプロセスを終了)", this);
	resolveConflictBtn_->setToolTip(
		"ポート28765(TCP)・39540(UDP)を使用している、このOBSプロセス以外の全プロセスを"
		"強制終了します。実行後はOBSの再起動が必要です。");

	auto *closeBtn = new QPushButton("閉じる", this);
	auto *bottomRow = new QHBoxLayout();
	bottomRow->addWidget(openControllerBtn_);
	bottomRow->addStretch();
	bottomRow->addWidget(closeBtn);

	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(8);
	layout->addWidget(warnLabel);
	layout->addWidget(statusLabel_);
	layout->addWidget(downloadProgress_);
	layout->addLayout(btnRow);
	layout->addWidget(urlEdit_);
	layout->addLayout(urlBtnRow);
	layout->addWidget(hintLabel_);
	layout->addWidget(resolveConflictBtn_);
	layout->addLayout(bottomRow);

	QObject::connect(setupBtn_, &QPushButton::clicked, this, &GlobalServerSettingsDialog::onSetupClicked);
	QObject::connect(startStopBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onStartStopClicked);
	QObject::connect(copyUrlBtn_, &QPushButton::clicked, this, &GlobalServerSettingsDialog::onCopyUrlClicked);
	QObject::connect(copyInviteLinkBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onCopyInviteLinkClicked);
	QObject::connect(openControllerBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onOpenControllerClicked);
	QObject::connect(resolveConflictBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onResolveConflictClicked);
	QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

	if (tunnel_) {
		QObject::connect(tunnel_, &CloudflareTunnelManager::statusChanged, this,
		                  &GlobalServerSettingsDialog::onTunnelStatusChanged);
		QObject::connect(tunnel_, &CloudflareTunnelManager::urlResolved, this,
		                  &GlobalServerSettingsDialog::onTunnelUrlResolved);
		QObject::connect(tunnel_, &CloudflareTunnelManager::downloadProgress, this,
		                  &GlobalServerSettingsDialog::onDownloadProgress);
		QObject::connect(tunnel_, &CloudflareTunnelManager::downloadFinished, this,
		                  &GlobalServerSettingsDialog::onDownloadFinished);
	}

	refreshUi();
}

void GlobalServerSettingsDialog::refreshUi()
{
	if (!tunnel_) {
		statusLabel_->setText("<span style='color:#cc4444;'>● CloudflareTunnelManager未初期化</span>");
		setupBtn_->setEnabled(false);
		startStopBtn_->setEnabled(false);
		return;
	}

	const auto status = tunnel_->status();
	QString stateHtml;
	switch (status) {
	case CloudflareTunnelManager::Status::NotInstalled:
		stateHtml = "<span style='color:#cc8844; font-weight:bold;'>● 未インストール</span>";
		break;
	case CloudflareTunnelManager::Status::Stopped:
		stateHtml = "<span style='color:#aaaaaa; font-weight:bold;'>● 停止中</span>";
		break;
	case CloudflareTunnelManager::Status::Starting:
		stateHtml = "<span style='color:#ffcc44; font-weight:bold;'>● 起動中（URL取得待ち）</span>";
		break;
	case CloudflareTunnelManager::Status::Published:
		stateHtml = "<span style='color:#44cc44; font-weight:bold;'>● 公開中</span>";
		break;
	}
	QString html = "トンネル状態: " + stateHtml;
	if (status == CloudflareTunnelManager::Status::Published && !tunnel_->publicUrl().isEmpty()) {
		html += QStringLiteral(" (URL: %1)").arg(tunnel_->publicUrl().toHtmlEscaped());
	} else if (!lastStatusMessage_.isEmpty()) {
		html += QStringLiteral(" <span style='color:#cc4444;'>(%1)</span>")
				.arg(lastStatusMessage_.toHtmlEscaped());
	}
	statusLabel_->setText(html);

	setupBtn_->setVisible(status == CloudflareTunnelManager::Status::NotInstalled);

	// 手動でいつでも安全に停止/開始できるよう、状態に応じてボタンのenable/disableを
	// 切り替える。起動処理中（Starting）は多重起動防止のため開始側を無効化しつつ、
	// 停止操作自体はいつでも受け付ける（terminate()→kill()のフォールバックで安全に
	// 終了できるため、起動中でも停止ボタンは有効のままにする）。
	const bool running = tunnel_->isRunning();
	startStopBtn_->setText(running ? "トンネルを停止" : "トンネルを開始");
	startStopBtn_->setEnabled(status != CloudflareTunnelManager::Status::NotInstalled);

	const QString url = tunnel_->publicUrl();
	urlEdit_->setText(url);
	copyUrlBtn_->setEnabled(!url.isEmpty());
	copyInviteLinkBtn_->setEnabled(!url.isEmpty());
}

void GlobalServerSettingsDialog::onSetupClicked()
{
	if (!tunnel_)
		return;
	setupBtn_->setEnabled(false);
	downloadProgress_->setVisible(true);
	downloadProgress_->setValue(0);
	statusLabel_->setText("cloudflared.exeをダウンロード中…");
	tunnel_->downloadCloudflared();
}

void GlobalServerSettingsDialog::onStartStopClicked()
{
	// クリックそのものが届いているか・その時点の状態を必ず記録する
	// （「ボタンを押しても反応せずログも出ない」場合の切り分け用）。
	obs_log(LOG_INFO,
		"[CloudflareTunnel] onStartStopClicked() called, current status: %d, tunnel_=%p wsServer_=%p",
		tunnel_ ? static_cast<int>(tunnel_->getStatus()) : -1, static_cast<void *>(tunnel_),
		static_cast<void *>(wsServer_));

	// tunnel_（CloudflareTunnelManagerそのもの）が無ければ何もできないため中断するが、
	// wsServer_はここでは必須にしない。WsServer::start()がポート競合等で失敗すると
	// obs_module_load()側でs_wsServerがnullptrのまま残り、このダイアログにもnullptrが
	// 渡ってくる（後述のフォールバックで対応するため、単なる配線ミスではない）。
	if (!tunnel_) {
		obs_log(LOG_WARNING, "[CloudflareTunnel] onStartStopClicked(): tunnel_が未設定のため中断します");
		return;
	}

	// ボタン連打によるstartTunnel()/stopTunnel()の多重呼び出しを防ぐため、
	// 処理中は一時的に無効化する。statusChanged受信時（onTunnelStatusChanged）で
	// 確実に再有効化されるが、その経路が万一発火しなくてもボタンが固まった
	// ままにならないよう、この関数の最後でもrefreshUi()により再評価する。
	startStopBtn_->setEnabled(false);

	// Status::Starting/Publishedを「起動中」とみなして停止側へ、
	// Status::Stopped/NotInstalledを「未起動」とみなして開始側へ分岐する
	// （プロセスの生死ではなくトンネルの状態そのもので判定する）。
	const auto status = tunnel_->getStatus();
	const bool running = (status == CloudflareTunnelManager::Status::Starting ||
			      status == CloudflareTunnelManager::Status::Published);

	if (running) {
		obs_log(LOG_INFO, "[CloudflareTunnel] onStartStopClicked(): stopTunnel()を呼び出します");
		tunnel_->stopTunnel();
	} else {
		// wsServer_が生きていればその実ポートを、無ければ設定済みのWebSocketポート
		// （PluginConfig::instance().wsPort、既定28765）へフォールバックする
		// （WsServer::start()がポート競合等で失敗しs_wsServerがnullptrのままでも、
		// トンネル自体は開始を試行できるようにするため）。
		uint16_t port;
		if (wsServer_) {
			port = wsServer_->port();
		} else {
			port = static_cast<uint16_t>(PluginConfig::instance().wsPort);
			obs_log(LOG_WARNING,
				"[CloudflareTunnel] onStartStopClicked(): wsServer_が未設定のため設定ポート%uへ"
				"フォールバックします",
				static_cast<unsigned>(port));
		}
		obs_log(LOG_INFO, "[CloudflareTunnel] onStartStopClicked(): startTunnel(port=%u)を呼び出します",
			static_cast<unsigned>(port));
		tunnel_->startTunnel(port);
	}
	refreshUi();
}

void GlobalServerSettingsDialog::onCopyUrlClicked()
{
	if (!tunnel_ || tunnel_->publicUrl().isEmpty())
		return;
	QApplication::clipboard()->setText(tunnel_->publicUrl());
}

void GlobalServerSettingsDialog::onCopyInviteLinkClicked()
{
	if (!tunnel_ || tunnel_->publicUrl().isEmpty())
		return;
	// ゲスト用リンク: mode/tokenを含めない素のvrm_stage.html（ゲストは招待コードを
	// 手入力して参加する。tokenはController特権の証明であり、ゲストへ渡してはならない）。
	const QString link = tunnel_->publicUrl() + "/vrm_stage.html";
	QApplication::clipboard()->setText(link);
}

void GlobalServerSettingsDialog::onOpenControllerClicked()
{
	obs_log(LOG_INFO, "[CloudflareTunnel] onOpenControllerClicked() called");

	// wsServer_がnullptr（WsServer::start()がポート競合等で失敗した状態）でも
	// 処理を中断せず、設定済みポート（既定28765）へフォールバックする。
	// この場合controllerSecretToken()は取得できないためtokenなしのURLになる
	// （WsServer自体が起動していない以上、そもそも接続はできないが、要求通り
	// URL生成・ブラウザ起動自体は試行する）。
	const auto &cfg = PluginConfig::instance();
	const uint16_t port = wsServer_ ? wsServer_->port() : static_cast<uint16_t>(cfg.wsPort);
	const std::string token = wsServer_ ? wsServer_->controllerSecretToken() : std::string();
	if (!wsServer_) {
		obs_log(LOG_WARNING,
			"[CloudflareTunnel] onOpenControllerClicked(): wsServer_が未設定のため設定ポート%uへ"
			"フォールバックします（tokenなし）",
			static_cast<unsigned>(port));
	}

	// トンネル稼働中（公開URL確定済み）ならトンネルURLを、そうでなければ
	// ローカルURLをベースにControllerのURLを組み立てる。
	QString urlString;
	if (tunnel_ && tunnel_->status() == CloudflareTunnelManager::Status::Published &&
	    !tunnel_->publicUrl().isEmpty()) {
		urlString = tunnel_->publicUrl() + "/vrm_stage.html?mode=controller";
	} else {
		urlString = QStringLiteral("http://localhost:%1/vrm_stage.html?mode=controller").arg(port);
	}
	if (!token.empty())
		urlString += QStringLiteral("&token=%1").arg(QString::fromStdString(token));

	obs_log(LOG_INFO, "[CloudflareTunnel] Opening controller URL: %s", urlString.toUtf8().constData());

#ifdef _WIN32
	// QDesktopServices::openUrl()のみだと環境（既定ブラウザ未設定・URLハンドラ未登録等）に
	// よっては無反応になることがあるため、ShellExecuteWを優先して使う。
	ShellExecuteW(nullptr, L"open", urlString.toStdWString().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	QDesktopServices::openUrl(QUrl(urlString));
#endif
}

void GlobalServerSettingsDialog::onResolveConflictClicked()
{
	obs_log(LOG_INFO, "[CloudflareTunnel] onResolveConflictClicked() called");

	// 実際に使用中の設定ポート（既定値: ws=28765, vmc=39540）を使う。
	// ハードコードにすると将来設定ポートを変更した際に無関係なポートを対象にして
	// しまうため、PluginConfig側の値をそのまま使う。
	const auto &cfg = PluginConfig::instance();
	const int wsPort = cfg.wsPort;
	const int vmcPort = cfg.vmcPort;

	// 他プロセスを強制終了する破壊的操作のため、実行前に一度確認する
	// （要求文には無いが、ボタンひとつで他プロセスをkillする機能である以上、
	// 誤クリックによる意図しない実行を避けるための最低限の安全策として追加）。
	const auto confirm = QMessageBox::warning(
		this, "ポート競合の強制解消",
		QStringLiteral("ポート%1(TCP)・%2(UDP)を使用している、このOBSプロセス以外の全プロセスを"
			       "強制終了します。これらのポートを他の目的で使っている別プロセスがあれば、"
			       "それも巻き込んで終了する可能性があります。実行しますか？")
			.arg(wsPort)
			.arg(vmcPort),
		QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
	if (confirm != QMessageBox::Yes) {
		obs_log(LOG_INFO, "[CloudflareTunnel] onResolveConflictClicked(): ユーザーがキャンセルしました");
		return;
	}

#ifdef _WIN32
	// 現在のOBSプロセス自身を誤ってkillしないよう、自PIDを除外する。
	const qint64 currentPid = QCoreApplication::applicationPid();
	obs_log(LOG_INFO,
		"[CloudflareTunnel] onResolveConflictClicked(): PID=%lld を除外してport %d(TCP)/%d(UDP)"
		"を保持しているプロセスを終了します",
		static_cast<long long>(currentPid), wsPort, vmcPort);

	// PowerShellスクリプト本体はQProcess::execute(program, arguments)の引数リストとして渡す
	// （"powershell -Command \"...\""という1本の文字列をQProcess::execute(QString)へ渡す方式は、
	// 空白区切りで引数分割されるためクォート・パイプを含むスクリプトが正しく渡らない）。
	const QString killOthersOnPortTemplate =
		QStringLiteral("%1 -LocalPort %2 -ErrorAction SilentlyContinue | "
			       "Select-Object -ExpandProperty OwningProcess | "
			       "Sort-Object -Unique | "
			       "Where-Object { $_ -ne %3 } | "
			       "ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }");

	const QString psTcp =
		killOthersOnPortTemplate.arg(QStringLiteral("Get-NetTCPConnection"), QString::number(wsPort))
			.arg(currentPid);
	const QString psUdp =
		killOthersOnPortTemplate.arg(QStringLiteral("Get-NetUDPEndpoint"), QString::number(vmcPort))
			.arg(currentPid);

	const QStringList psArgsTcp = {"-NoProfile", "-NonInteractive", "-Command", psTcp};
	const QStringList psArgsUdp = {"-NoProfile", "-NonInteractive", "-Command", psUdp};

	const int tcpExit = QProcess::execute("powershell.exe", psArgsTcp);
	const int udpExit = QProcess::execute("powershell.exe", psArgsUdp);
	obs_log(LOG_INFO,
		"[CloudflareTunnel] onResolveConflictClicked(): powershell exit codes tcp=%d udp=%d", tcpExit,
		udpExit);
#endif

	QMessageBox::information(this, "ポート競合の強制解消",
				  "ポートの解放処理を実行しました。通信サーバーを再起動するため、"
				  "OBS Studioを一度終了し、再起動してください。");
}

void GlobalServerSettingsDialog::onTunnelStatusChanged(CloudflareTunnelManager::Status status,
                                                          const QString &message)
{
	// Published時のmessageは公開URL（refreshUi()側でpublicUrl()から改めて表示するため
	// ここでは保持しない）。それ以外（プロセスエラー・異常終了等）のみエラー表示用に保持する。
	lastStatusMessage_ = (status == CloudflareTunnelManager::Status::Published) ? QString() : message;

	// onStartStopClicked()がstartTunnel()/stopTunnel()呼び出し直前に
	// setEnabled(false)したボタンを、状態変化が届いた時点で確実に再有効化する
	// （refreshUi()側のロジックにも依存するが、ここで明示しておくことで
	// 万一の実装変更でボタンが無効のまま固まる事態を避ける）。
	startStopBtn_->setEnabled(true);

	refreshUi();
}

void GlobalServerSettingsDialog::onTunnelUrlResolved(const QString &)
{
	refreshUi();
}

void GlobalServerSettingsDialog::onDownloadProgress(qint64 received, qint64 total)
{
	if (total > 0)
		downloadProgress_->setValue(static_cast<int>(received * 100 / total));
}

void GlobalServerSettingsDialog::onDownloadFinished(bool success, const QString &errorMessage)
{
	downloadProgress_->setVisible(false);
	setupBtn_->setEnabled(true);
	if (!success) {
		QMessageBox::warning(this, "セットアップ失敗",
		                      "cloudflared.exeのダウンロードに失敗しました。\n" + errorMessage);
	}
	refreshUi();
}
