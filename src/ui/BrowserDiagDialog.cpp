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

#include "BrowserDiagDialog.hpp"
#include "../modules/WsServer.hpp"

#include <obs-frontend-api.h> // obs.h / proc.h / calldata.h を包含
#include <obs-module.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

// ─── 対象 HTML ファイル（特定度の高い順：overlap を防ぐため） ─────────
static const char *TARGET_FILES[] = {
	"conversation_overlay.html",
	"overlay.html",
	"tts.html",
	"debug.html",
	"effect.html",
	nullptr
};

// ─── URL が対象ファイルを参照しているか判定（境界チェック付き） ──────
static bool urlMatchesTarget(const char *url, const char *target)
{
	if (!url || !*url || !target || !*target)
		return false;
	const char *p = strstr(url, target);
	if (!p)
		return false;
	// 直前がパス区切りでなければ別ファイル名の一部
	if (p != url && p[-1] != '/' && p[-1] != '\\')
		return false;
	// 直後が終端 / クエリ / フラグメントでなければ拡張子の一部
	const char *after = p + strlen(target);
	return *after == '\0' || *after == '?' || *after == '#';
}

// ─── ブラウザソースのリロード ────────────────────────────────────────
static bool refreshBrowserSource(obs_source_t *source)
{
	// OBS browser 標準: proc_handler に登録された "refresh_nocache" を呼ぶ
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	if (ph) {
		calldata_t cd = {};
		calldata_init(&cd);
		const bool ok = proc_handler_call(ph, "refresh_nocache", &cd);
		calldata_free(&cd);
		if (ok)
			return true;
	}

	// フォールバック: URL を一度クリア → 再設定してリロードをトリガー
	obs_data_t *settings = obs_source_get_settings(source);
	const char *rawUrl   = obs_data_get_string(settings, "url");
	if (!rawUrl || !*rawUrl) {
		obs_data_release(settings);
		return false;
	}
	const std::string savedUrl = rawUrl;
	obs_data_release(settings);

	obs_data_t *upd = obs_data_create();
	obs_data_set_string(upd, "url", "");
	obs_source_update(source, upd);
	obs_data_set_string(upd, "url", savedUrl.c_str());
	obs_source_update(source, upd);
	obs_data_release(upd);
	return true;
}

// ─── シーンアイテム列挙：スキャン用 ─────────────────────────────────
struct ScanCtx {
	const char *sceneName;
	std::set<obs_source_t *> *seen;
	std::map<std::string, std::vector<std::string>> *found; // fileName→シーン名一覧
};

static bool scanItemCb(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<ScanCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return true;
	const char *srcId = obs_source_get_id(src);
	if (!srcId || strcmp(srcId, "browser_source") != 0)
		return true;
	if (ctx->seen->count(src))
		return true; // 重複排除

	obs_data_t *settings = obs_source_get_settings(src);
	const char *url      = obs_data_get_string(settings, "url");

	for (int i = 0; TARGET_FILES[i]; ++i) {
		if (urlMatchesTarget(url, TARGET_FILES[i])) {
			(*ctx->found)[TARGET_FILES[i]].push_back(ctx->sceneName);
			ctx->seen->insert(src);
			break; // 1ソース = 1ファイル
		}
	}
	obs_data_release(settings);
	return true;
}

// ─── シーンアイテム列挙：リロード用 ─────────────────────────────────
struct ReloadCtx {
	std::set<obs_source_t *> *seen;
	int *successCount;
};

static bool reloadItemCb(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<ReloadCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return true;
	const char *srcId = obs_source_get_id(src);
	if (!srcId || strcmp(srcId, "browser_source") != 0)
		return true;
	if (ctx->seen->count(src))
		return true; // 重複排除

	obs_data_t *settings = obs_source_get_settings(src);
	const char *url      = obs_data_get_string(settings, "url");

	bool isTarget = false;
	for (int i = 0; TARGET_FILES[i] && !isTarget; ++i)
		isTarget = urlMatchesTarget(url, TARGET_FILES[i]);
	obs_data_release(settings);

	if (!isTarget)
		return true;

	ctx->seen->insert(src);
	if (refreshBrowserSource(src))
		++(*ctx->successCount);
	return true;
}

// ─── ダイアログ ──────────────────────────────────────────────────────

BrowserDiagDialog::BrowserDiagDialog(WsServer *wsServer, QWidget *parent)
	: QDialog(parent), wsServer_(wsServer)
{
	setWindowTitle("obs-live-hub 接続診断");
	setMinimumWidth(440);

	// ── サーバー状態行 ──
	serverStatusLabel_ = new QLabel(this);
	serverStatusLabel_->setTextFormat(Qt::RichText);

	// ── クライアント数行 ──
	clientCountLabel_  = new QLabel(this);
	auto *refreshBtn   = new QPushButton("更新", this);
	refreshBtn->setFixedWidth(60);

	auto *countRow = new QHBoxLayout();
	countRow->addWidget(clientCountLabel_);
	countRow->addStretch();
	countRow->addWidget(refreshBtn);

	// ── ブラウザソース一覧 ──
	auto *srcGroup  = new QGroupBox("対象ブラウザソース（全シーン横断）", this);
	auto *srcLayout = new QVBoxLayout(srcGroup);
	sourceList_ = new QListWidget(this);
	sourceList_->setSelectionMode(QAbstractItemView::NoSelection);
	sourceList_->setFocusPolicy(Qt::NoFocus);
	sourceList_->setFixedHeight(150);
	srcLayout->addWidget(sourceList_);

	// ── 結果ラベル ──
	resultLabel_ = new QLabel(this);
	resultLabel_->setWordWrap(true);
	resultLabel_->setMinimumHeight(20);

	// ── ボタン行 ──
	reloadBtn_       = new QPushButton("再読み込み", this);
	auto *closeBtn   = new QPushButton("閉じる", this);

	auto *btnRow = new QHBoxLayout();
	btnRow->addWidget(reloadBtn_);
	btnRow->addStretch();
	btnRow->addWidget(closeBtn);

	// ── メインレイアウト ──
	auto *layout = new QVBoxLayout(this);
	layout->setSpacing(8);
	layout->addWidget(serverStatusLabel_);
	layout->addLayout(countRow);
	layout->addSpacing(2);
	layout->addWidget(srcGroup);
	layout->addWidget(resultLabel_);
	layout->addLayout(btnRow);

	QObject::connect(refreshBtn, &QPushButton::clicked,
	                 this, &BrowserDiagDialog::onRefresh);
	QObject::connect(reloadBtn_, &QPushButton::clicked,
	                 this, &BrowserDiagDialog::onReload);
	QObject::connect(closeBtn,   &QPushButton::clicked,
	                 this, &QDialog::reject);

	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	QObject::connect(timer_, &QTimer::timeout,
	                 this, &BrowserDiagDialog::onTimer);
	timer_->start();

	updateStatus();
	scanSources();
}

void BrowserDiagDialog::onTimer()
{
	updateStatus();
}

void BrowserDiagDialog::onRefresh()
{
	resultLabel_->clear();
	updateStatus();
	scanSources();
}

void BrowserDiagDialog::onReload()
{
	const int n = reloadSources();
	if (n > 0) {
		resultLabel_->setStyleSheet("color: #44cc44;");
		resultLabel_->setText(
			QString("リロード完了: %1 件のブラウザソースを再読み込みしました。").arg(n));
	} else {
		resultLabel_->setStyleSheet("color: #cc4444;");
		resultLabel_->setText("リロード対象のブラウザソースが見つかりませんでした。");
	}
	// リロード後に接続数を更新
	updateStatus();
}

void BrowserDiagDialog::updateStatus()
{
	if (!wsServer_) {
		serverStatusLabel_->setText("WsServer: 未初期化");
		clientCountLabel_->setText("接続クライアント数: —");
		reloadBtn_->setEnabled(false);
		return;
	}

	const int port = static_cast<int>(wsServer_->port());

	QString stateHtml;
	if (wsServer_->isRunning()) {
		stateHtml = "<span style='color:#44cc44; font-weight:bold;'>● 稼働中</span>";
	} else {
		switch (wsServer_->listenState()) {
		case WsServer::ListenState::BindFailed:
			stateHtml =
				"<span style='color:#cc4444; font-weight:bold;'>● バインド失敗</span>"
				"<br><span style='color:#cc8844; font-size:small;'>"
				"ポートが別プロセスに占有されています。"
				"OBS ログを確認するか、PCを再起動してください。</span>";
			break;
		case WsServer::ListenState::ListenFailed:
			stateHtml = "<span style='color:#cc4444; font-weight:bold;'>● listen 失敗</span>"
				    "<br><span style='color:#cc8844; font-size:small;'>"
				    "OBS ログで WSA エラーコードを確認してください。</span>";
			break;
		default:
			stateHtml = "<span style='color:#cc4444; font-weight:bold;'>● 停止</span>";
			break;
		}
	}

	serverStatusLabel_->setText(
		QString("WsServer ポート: %1　　%2").arg(port).arg(stateHtml));

	const int count = wsServer_->clientCount();
	clientCountLabel_->setText(
		QString("接続クライアント数: %1 件").arg(count));
}

void BrowserDiagDialog::scanSources()
{
	sourceList_->clear();

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	std::map<std::string, std::vector<std::string>> found;
	std::set<obs_source_t *> seen;

	for (size_t si = 0; si < scenes.sources.num; ++si) {
		obs_source_t *sceneSrc = scenes.sources.array[si];
		obs_scene_t  *scene    = obs_scene_from_source(sceneSrc);
		if (!scene)
			continue;
		const char *sname = obs_source_get_name(sceneSrc);
		ScanCtx ctx{sname, &seen, &found};
		obs_scene_enum_items(scene, scanItemCb, &ctx);
	}
	obs_frontend_source_list_free(&scenes);

	// 全対象ファイルの結果を表示
	for (int i = 0; TARGET_FILES[i]; ++i) {
		const std::string tgt = TARGET_FILES[i];
		auto it = found.find(tgt);
		if (it != found.end() && !it->second.empty()) {
			for (const auto &sceneName : it->second) {
				auto *item = new QListWidgetItem(
					QString("✔  [%1]  %2")
					.arg(QString::fromStdString(sceneName))
					.arg(QString::fromStdString(tgt)));
				item->setForeground(QColor(0x44, 0xcc, 0x44));
				sourceList_->addItem(item);
			}
		} else {
			auto *item = new QListWidgetItem(
				QString("✗  未検出: %1").arg(QString::fromStdString(tgt)));
			item->setForeground(QColor(0xcc, 0x66, 0x44));
			sourceList_->addItem(item);
		}
	}

	const int total = static_cast<int>(seen.size());
	reloadBtn_->setText(
		total > 0
			? QString("再読み込み（検出 %1 件をリロード）").arg(total)
			: "再読み込み（対象なし）");
	reloadBtn_->setEnabled(total > 0);
}

int BrowserDiagDialog::reloadSources()
{
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	int successCount = 0;
	std::set<obs_source_t *> seen;

	for (size_t si = 0; si < scenes.sources.num; ++si) {
		obs_source_t *sceneSrc = scenes.sources.array[si];
		obs_scene_t  *scene    = obs_scene_from_source(sceneSrc);
		if (!scene)
			continue;
		ReloadCtx ctx{&seen, &successCount};
		obs_scene_enum_items(scene, reloadItemCb, &ctx);
	}
	obs_frontend_source_list_free(&scenes);
	return successCount;
}
