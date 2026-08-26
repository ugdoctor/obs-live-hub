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

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMessageBox>
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
	layout->addLayout(bottomRow);

	QObject::connect(setupBtn_, &QPushButton::clicked, this, &GlobalServerSettingsDialog::onSetupClicked);
	QObject::connect(startStopBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onStartStopClicked);
	QObject::connect(copyUrlBtn_, &QPushButton::clicked, this, &GlobalServerSettingsDialog::onCopyUrlClicked);
	QObject::connect(copyInviteLinkBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onCopyInviteLinkClicked);
	QObject::connect(openControllerBtn_, &QPushButton::clicked, this,
	                  &GlobalServerSettingsDialog::onOpenControllerClicked);
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
	statusLabel_->setText("トンネル状態: " + stateHtml);

	setupBtn_->setVisible(status == CloudflareTunnelManager::Status::NotInstalled);

	const bool running = tunnel_->isRunning();
	startStopBtn_->setText(running ? "トンネル停止" : "トンネル開始");
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
	if (!tunnel_ || !wsServer_)
		return;
	if (tunnel_->isRunning()) {
		tunnel_->stopTunnel();
	} else {
		tunnel_->startTunnel(wsServer_->port());
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
#ifdef _WIN32
	if (!wsServer_)
		return;
	const auto &cfg = PluginConfig::instance();
	const std::string token = wsServer_->controllerSecretToken();
	const std::string url = "http://127.0.0.1:" + std::to_string(cfg.wsPort) +
				 "/vrm_stage.html?mode=controller&token=" + token;
	ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void GlobalServerSettingsDialog::onTunnelStatusChanged(CloudflareTunnelManager::Status)
{
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
