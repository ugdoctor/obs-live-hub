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

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>

#include "../modules/CloudflareTunnelManager.hpp"

class WsServer;

// WAN公開対応: VRM Stageのマルチアバター同期機能をポート開放なしで外部公開するための
// 設定ダイアログ。CloudflareTunnelManagerのセットアップ・起動/停止・公開URLの確認を
// 1画面で行えるようにする（ツール→obs-live-hub→VRM Stage→グローバルサーバー設定 (WAN公開)）。
class GlobalServerSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit GlobalServerSettingsDialog(CloudflareTunnelManager *tunnel, WsServer *wsServer,
	                                     QWidget *parent = nullptr);

private slots:
	void onSetupClicked();
	void onStartStopClicked();
	void onCopyUrlClicked();
	void onCopyInviteLinkClicked();
	void onOpenControllerClicked();

	void onTunnelStatusChanged(CloudflareTunnelManager::Status status);
	void onTunnelUrlResolved(const QString &url);
	void onDownloadProgress(qint64 received, qint64 total);
	void onDownloadFinished(bool success, const QString &errorMessage);

private:
	void refreshUi();

	CloudflareTunnelManager *tunnel_;
	WsServer *wsServer_;

	QLabel *statusLabel_;
	QProgressBar *downloadProgress_;
	QPushButton *setupBtn_;
	QPushButton *startStopBtn_;
	QLineEdit *urlEdit_;
	QPushButton *copyUrlBtn_;
	QPushButton *copyInviteLinkBtn_;
	QPushButton *openControllerBtn_;
	QLabel *hintLabel_;
};
