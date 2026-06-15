#include "StreamSettingsDialog.hpp"
#include "core/PluginConfig.hpp"

#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>

// ============================================================
// WinHTTP helper: GET と PATCH の両方をサポート
// ============================================================

struct StreamHttpResult {
	bool ok = false;
	int statusCode = 0;
	std::string body;
	std::string error;
};

#ifdef _WIN32
static StreamHttpResult doRequest(const std::wstring &method, const std::wstring &host,
				   const std::wstring &path,
				   const std::vector<std::string> &asciiHeaders,
				   const std::string &utf8Body = "")
{
	StreamHttpResult r;

	HINTERNET hSess = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSess) {
		r.error = "WinHttpOpen failed (err=" + std::to_string(GetLastError()) + ")";
		return r;
	}

	HINTERNET hConn = WinHttpConnect(hSess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConn) {
		r.error = "WinHttpConnect failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hSess);
		return r;
	}

	HINTERNET hReq = WinHttpOpenRequest(hConn, method.c_str(), path.c_str(), nullptr,
					     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
					     WINHTTP_FLAG_SECURE);
	if (!hReq) {
		r.error = "WinHttpOpenRequest failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSess);
		return r;
	}

	for (const auto &h : asciiHeaders) {
		std::wstring wh(h.begin(), h.end());
		wh += L"\r\n";
		WinHttpAddRequestHeaders(hReq, wh.c_str(), static_cast<DWORD>(-1),
					 WINHTTP_ADDREQ_FLAG_ADD);
	}

	LPVOID bodyPtr = utf8Body.empty() ? nullptr : (LPVOID)utf8Body.c_str();
	DWORD bodyLen = static_cast<DWORD>(utf8Body.size());

	if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPtr, bodyLen, bodyLen,
				0)) {
		r.error = "WinHttpSendRequest failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hReq);
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSess);
		return r;
	}

	if (!WinHttpReceiveResponse(hReq, nullptr)) {
		r.error =
			"WinHttpReceiveResponse failed (err=" + std::to_string(GetLastError()) + ")";
		WinHttpCloseHandle(hReq);
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSess);
		return r;
	}

	DWORD sc = 0, scSz = sizeof(sc);
	WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &sc, &scSz, WINHTTP_NO_HEADER_INDEX);
	r.statusCode = static_cast<int>(sc);

	DWORD avail = 0;
	while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
		std::string chunk(avail, '\0');
		DWORD read = 0;
		if (!WinHttpReadData(hReq, &chunk[0], avail, &read))
			break;
		r.body.append(chunk, 0, read);
	}

	WinHttpCloseHandle(hReq);
	WinHttpCloseHandle(hConn);
	WinHttpCloseHandle(hSess);
	r.ok = true;
	return r;
}
#else
static StreamHttpResult doRequest(const std::wstring &, const std::wstring &,
				   const std::wstring &,
				   const std::vector<std::string> &,
				   const std::string & = "")
{
	return {false, 0, "", "Windows only"};
}
#endif

// URL パーセントエンコード（クエリ値用）
static std::string urlEncode(const std::string &s)
{
	std::string out;
	out.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", c);
			out += buf;
		}
	}
	return out;
}

// ============================================================
// Twitch ゲーム検索ダイアログ（Q_OBJECT 不要・ラムダで接続）
// ============================================================
class GameSearchDialog : public QDialog {
public:
	explicit GameSearchDialog(std::string token, std::string clientId, QWidget *parent = nullptr)
		: QDialog(parent), token_(std::move(token)), clientId_(std::move(clientId))
	{
		setWindowTitle("カテゴリ検索");
		resize(400, 340);

		searchEdit_ = new QLineEdit(this);
		searchEdit_->setPlaceholderText("ゲーム名を入力...");

		searchBtn_ = new QPushButton("検索", this);
		resultList_ = new QListWidget(this);

		auto *btnBox =
			new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

		auto *row = new QHBoxLayout;
		row->addWidget(searchEdit_, 1);
		row->addWidget(searchBtn_);

		auto *vlay = new QVBoxLayout(this);
		vlay->addLayout(row);
		vlay->addWidget(resultList_, 1);
		vlay->addWidget(btnBox);

		connect(searchBtn_, &QPushButton::clicked, [this]() { runSearch(); });
		connect(searchEdit_, &QLineEdit::returnPressed, [this]() { runSearch(); });
		connect(resultList_, &QListWidget::itemDoubleClicked, [this]() { accept(); });
		connect(btnBox, &QDialogButtonBox::accepted, [this]() { accept(); });
		connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	}

	QString selectedId() const { return selectedId_; }
	QString selectedName() const { return selectedName_; }

	void accept() override
	{
		if (auto *item = resultList_->currentItem()) {
			selectedId_ = item->data(Qt::UserRole).toString();
			selectedName_ = item->text();
			QDialog::accept();
		}
	}

private:
	void runSearch()
	{
		const QString q = searchEdit_->text().trimmed();
		if (q.isEmpty())
			return;

		searchBtn_->setEnabled(false);
		searchBtn_->setText("検索中...");
		resultList_->clear();

		const std::string token = token_;
		const std::string clientId = clientId_;
		const std::string query = urlEncode(q.toStdString());

		QPointer<GameSearchDialog> selfPtr(this);

		std::thread([selfPtr, token, clientId, query]() {
			const std::string pathStr = "/helix/search/categories?query=" + query;
			const std::wstring path(pathStr.begin(), pathStr.end());

			auto r = doRequest(L"GET", L"api.twitch.tv", path,
					   {"Authorization: Bearer " + token,
					    "Client-Id: " + clientId});

			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[selfPtr, r]() {
					if (!selfPtr)
						return;
					selfPtr->searchBtn_->setEnabled(true);
					selfPtr->searchBtn_->setText("検索");

					if (!r.ok || r.statusCode != 200) {
						QMessageBox::warning(
							selfPtr,
							"エラー",
							QString("検索失敗 (HTTP %1)")
								.arg(r.statusCode));
						return;
					}

					const auto doc = QJsonDocument::fromJson(
						QByteArray::fromStdString(r.body));
					const auto data = doc.object()["data"].toArray();
					for (const QJsonValue &v : data) {
						const QJsonObject obj = v.toObject();
						auto *item = new QListWidgetItem(
							obj["name"].toString());
						item->setData(Qt::UserRole,
							      obj["id"].toString());
						selfPtr->resultList_->addItem(item);
					}
					if (selfPtr->resultList_->count() == 0)
						selfPtr->resultList_->addItem(
							new QListWidgetItem("(結果なし)"));
				},
				Qt::QueuedConnection);
		}).detach();
	}

	std::string token_;
	std::string clientId_;
	QLineEdit   *searchEdit_;
	QPushButton *searchBtn_;
	QListWidget *resultList_;
	QString      selectedId_;
	QString      selectedName_;
};

// ============================================================
// 言語・カテゴリテーブル
// ============================================================
static const struct { const char *code; const char *label; } LANG_LIST[] = {
	{"ja",    "日本語 (ja)"},
	{"en",    "English (en)"},
	{"ko",    "한국어 (ko)"},
	{"zh-cn", "中文 (zh-cn)"},
	{"zh-tw", "繁體中文 (zh-tw)"},
	{"de",    "Deutsch (de)"},
	{"fr",    "Français (fr)"},
	{"es",    "Español (es)"},
	{"pt",    "Português (pt)"},
	{"ru",    "Русский (ru)"},
	{"it",    "Italiano (it)"},
};

static const struct { const char *id; const char *label; } YOUTUBE_CATS[] = {
	{"20", "ゲーム (Gaming)"},
	{"24", "エンタメ (Entertainment)"},
	{"28", "科学技術 (Science & Technology)"},
	{"22", "ブログ (People & Blogs)"},
	{"10", "音楽 (Music)"},
	{"17", "スポーツ (Sports)"},
	{"25", "ニュース (News & Politics)"},
	{"26", "ハウツー (Howto & Style)"},
};

// ============================================================
// StreamSettingsDialog 実装
// ============================================================
StreamSettingsDialog::StreamSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 配信一括設定");
	setMinimumWidth(520);

	const auto &cfg = PluginConfig::instance();
	const bool hasTwitch = !cfg.twitchOAuthToken.empty();
	const bool hasYouTube = !cfg.youtubeAccessToken.empty();

	// --- Twitch group ---
	twitchGroup_ = new QGroupBox("Twitch", this);
	twitchGroup_->setEnabled(hasTwitch);
	if (!hasTwitch)
		twitchGroup_->setTitle("Twitch（未認証）");

	twitchTitleEdit_ = new QLineEdit(this);
	twitchTitleEdit_->setMaxLength(140);
	twitchTitleEdit_->setPlaceholderText("配信タイトル（最大140文字）");

	twitchGameEdit_ = new QLineEdit(this);
	twitchGameEdit_->setReadOnly(true);
	twitchGameEdit_->setPlaceholderText("ゲーム未設定（検索ボタンで選択）");

	twitchGameBtn_ = new QPushButton("検索...", this);

	twitchTagsEdit_ = new QLineEdit(this);
	twitchTagsEdit_->setPlaceholderText("タグ1,タグ2,タグ3（カンマ区切り、最大10個）");

	twitchLangCombo_ = new QComboBox(this);
	for (const auto &l : LANG_LIST)
		twitchLangCombo_->addItem(l.label, l.code);

	auto *gameRow = new QHBoxLayout;
	gameRow->addWidget(twitchGameEdit_, 1);
	gameRow->addWidget(twitchGameBtn_);

	auto *twitchForm = new QFormLayout(twitchGroup_);
	twitchForm->addRow("タイトル:", twitchTitleEdit_);
	twitchForm->addRow("カテゴリ:", gameRow);
	twitchForm->addRow("タグ:", twitchTagsEdit_);
	twitchForm->addRow("言語:", twitchLangCombo_);

	// --- YouTube group ---
	youtubeGroup_ = new QGroupBox("YouTube", this);
	youtubeGroup_->setEnabled(hasYouTube);
	if (!hasYouTube)
		youtubeGroup_->setTitle("YouTube（未認証 — 設定ダイアログで連携してください）");

	youtubeTitleEdit_ = new QLineEdit(this);
	youtubeTitleEdit_->setMaxLength(100);
	youtubeTitleEdit_->setPlaceholderText("配信タイトル（最大100文字）");

	youtubeDescEdit_ = new QTextEdit(this);
	youtubeDescEdit_->setPlaceholderText("配信説明文");
	youtubeDescEdit_->setMaximumHeight(100);

	youtubeCatCombo_ = new QComboBox(this);
	for (const auto &c : YOUTUBE_CATS)
		youtubeCatCombo_->addItem(c.label, c.id);

	youtubePrivacyCombo_ = new QComboBox(this);
	youtubePrivacyCombo_->addItem("公開 (public)", "public");
	youtubePrivacyCombo_->addItem("限定公開 (unlisted)", "unlisted");
	youtubePrivacyCombo_->addItem("非公開 (private)", "private");

	auto *youtubeForm = new QFormLayout(youtubeGroup_);
	youtubeForm->addRow("タイトル:", youtubeTitleEdit_);
	youtubeForm->addRow("説明:", youtubeDescEdit_);
	youtubeForm->addRow("カテゴリ:", youtubeCatCombo_);
	youtubeForm->addRow("公開設定:", youtubePrivacyCombo_);

	// --- 全体レイアウト ---
	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *vlay = new QVBoxLayout(this);
	vlay->addWidget(twitchGroup_);
	vlay->addWidget(youtubeGroup_);
	vlay->addWidget(buttonBox_);

	connect(twitchGameBtn_, &QPushButton::clicked, [this]() { onSearchGame(); });
	connect(buttonBox_, &QDialogButtonBox::accepted, [this]() { accept(); });
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromConfig();
}

void StreamSettingsDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();

	twitchTitleEdit_->setText(QString::fromStdString(cfg.streamTwitchTitle));
	twitchGameEdit_->setText(QString::fromStdString(cfg.streamTwitchGameName));
	selectedGameId_ = QString::fromStdString(cfg.streamTwitchGameId);
	twitchTagsEdit_->setText(QString::fromStdString(cfg.streamTwitchTags));

	const QString lang = QString::fromStdString(cfg.streamTwitchLanguage);
	for (int i = 0; i < twitchLangCombo_->count(); ++i) {
		if (twitchLangCombo_->itemData(i).toString() == lang) {
			twitchLangCombo_->setCurrentIndex(i);
			break;
		}
	}

	youtubeTitleEdit_->setText(QString::fromStdString(cfg.streamYoutubeTitle));
	youtubeDescEdit_->setPlainText(QString::fromStdString(cfg.streamYoutubeDescription));

	const QString catId = QString::fromStdString(cfg.streamYoutubeCategoryId);
	for (int i = 0; i < youtubeCatCombo_->count(); ++i) {
		if (youtubeCatCombo_->itemData(i).toString() == catId) {
			youtubeCatCombo_->setCurrentIndex(i);
			break;
		}
	}

	const QString privacy = QString::fromStdString(cfg.streamYoutubePrivacy);
	for (int i = 0; i < youtubePrivacyCombo_->count(); ++i) {
		if (youtubePrivacyCombo_->itemData(i).toString() == privacy) {
			youtubePrivacyCombo_->setCurrentIndex(i);
			break;
		}
	}
}

void StreamSettingsDialog::saveToConfig()
{
	auto &cfg = PluginConfig::instance();

	cfg.streamTwitchTitle    = twitchTitleEdit_->text().toStdString();
	cfg.streamTwitchGameId   = selectedGameId_.toStdString();
	cfg.streamTwitchGameName = twitchGameEdit_->text().toStdString();
	cfg.streamTwitchTags     = twitchTagsEdit_->text().toStdString();
	cfg.streamTwitchLanguage = twitchLangCombo_->currentData().toString().toStdString();

	cfg.streamYoutubeTitle       = youtubeTitleEdit_->text().toStdString();
	cfg.streamYoutubeDescription = youtubeDescEdit_->toPlainText().toStdString();
	cfg.streamYoutubeCategoryId  = youtubeCatCombo_->currentData().toString().toStdString();
	cfg.streamYoutubePrivacy     = youtubePrivacyCombo_->currentData().toString().toStdString();

	cfg.save();
}

void StreamSettingsDialog::accept()
{
	saveToConfig();

	const auto &cfg = PluginConfig::instance();
	const bool doTwitch  = twitchGroup_->isEnabled()  && !cfg.twitchOAuthToken.empty();
	// broadcast_id が未設定（空）の場合は配信中でないとみなしてスキップ
	const bool doYouTube = youtubeGroup_->isEnabled()
		&& !cfg.youtubeAccessToken.empty()
		&& !cfg.youtubeBroadcastId.empty();

	pendingCount_ = (doTwitch ? 1 : 0) + (doYouTube ? 1 : 0);
	errors_.clear();

	if (pendingCount_ == 0) {
		QDialog::accept();
		return;
	}

	auto *okBtn = buttonBox_->button(QDialogButtonBox::Ok);
	okBtn->setEnabled(false);
	okBtn->setText("送信中...");

	if (doTwitch)
		startTwitchUpdate();
	if (doYouTube)
		startYouTubeUpdate();
}

void StreamSettingsDialog::onSearchGame()
{
	const auto &cfg = PluginConfig::instance();
	GameSearchDialog dlg(cfg.twitchOAuthToken, cfg.twitchClientId, this);
	if (dlg.exec() == QDialog::Accepted && !dlg.selectedId().isEmpty()) {
		selectedGameId_ = dlg.selectedId();
		twitchGameEdit_->setText(dlg.selectedName());
	}
}

void StreamSettingsDialog::startTwitchUpdate()
{
	const auto &cfg = PluginConfig::instance();
	const std::string token    = cfg.twitchOAuthToken;
	const std::string clientId = cfg.twitchClientId;

	// JSON を主スレッドで構築
	QJsonObject body;
	body["title"] = twitchTitleEdit_->text().left(140);
	if (!selectedGameId_.isEmpty())
		body["game_id"] = selectedGameId_;
	body["broadcaster_language"] = twitchLangCombo_->currentData().toString();

	QJsonArray tags;
	for (const QString &t : twitchTagsEdit_->text().split(',', Qt::SkipEmptyParts)) {
		const QString trimmed = t.trimmed();
		if (!trimmed.isEmpty())
			tags.append(trimmed);
	}
	body["tags"] = tags;

	const std::string jsonBody =
		QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();

	QPointer<StreamSettingsDialog> selfPtr(this);

	std::thread([selfPtr, token, clientId, jsonBody]() {
		// GET /helix/users → broadcaster_id 取得
		auto r = doRequest(L"GET", L"api.twitch.tv", L"/helix/users",
				   {"Authorization: Bearer " + token, "Client-Id: " + clientId});

		if (!r.ok || r.statusCode != 200) {
			const std::string msg = "Twitch ユーザー取得失敗 (HTTP " +
						std::to_string(r.statusCode) + "): " + r.body;
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[selfPtr, msg]() {
					if (selfPtr)
						selfPtr->onTwitchUpdateDone(
							false, QString::fromStdString(msg));
				},
				Qt::QueuedConnection);
			return;
		}

		const auto userDoc =
			QJsonDocument::fromJson(QByteArray::fromStdString(r.body));
		const auto data = userDoc.object()["data"].toArray();
		if (data.isEmpty()) {
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[selfPtr]() {
					if (selfPtr)
						selfPtr->onTwitchUpdateDone(
							false, "Twitch ユーザーが見つかりません");
				},
				Qt::QueuedConnection);
			return;
		}
		const std::string broadcasterId =
			data.first().toObject()["id"].toString().toStdString();

		// PATCH /helix/channels
		const std::string patchStr = "/helix/channels?broadcaster_id=" + broadcasterId;
		const std::wstring patchPath(patchStr.begin(), patchStr.end());

		auto r2 = doRequest(L"PATCH", L"api.twitch.tv", patchPath,
				    {"Authorization: Bearer " + token, "Client-Id: " + clientId,
				     "Content-Type: application/json"},
				    jsonBody);

		// 204 No Content = 成功
		const bool ok = r2.ok && (r2.statusCode == 204 || r2.statusCode == 200);
		std::string errMsg;
		if (!ok)
			errMsg = "Twitch チャンネル更新失敗 (HTTP " +
				 std::to_string(r2.statusCode) + "): " + r2.body;

		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[selfPtr, ok, errMsg]() {
				if (selfPtr)
					selfPtr->onTwitchUpdateDone(
						ok, QString::fromStdString(errMsg));
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamSettingsDialog::startYouTubeUpdate()
{
	const auto &cfg = PluginConfig::instance();
	const std::string accessToken    = cfg.youtubeAccessToken;
	const std::string configBidStr   = cfg.youtubeBroadcastId;
	const std::string title          = youtubeTitleEdit_->text().left(100).toStdString();
	const std::string desc           = youtubeDescEdit_->toPlainText().toStdString();
	const std::string catId          = youtubeCatCombo_->currentData().toString().toStdString();
	const std::string privacy        = youtubePrivacyCombo_->currentData().toString().toStdString();

	QPointer<StreamSettingsDialog> selfPtr(this);

	std::thread([selfPtr, accessToken, configBidStr, title, desc, catId, privacy]() {
		const std::string authHdr = "Authorization: Bearer " + accessToken;

		// Step 1: ブロードキャストID を確定
		std::string bid = configBidStr;
		if (bid.empty() || bid == "me") {
			auto r = doRequest(
				L"GET", L"www.googleapis.com",
				L"/youtube/v3/liveBroadcasts?broadcastStatus=active&mine=true&part=id",
				{authHdr});
			if (!r.ok || r.statusCode != 200) {
				const std::string msg =
					"YouTube ブロードキャスト検索失敗 (HTTP " +
					std::to_string(r.statusCode) + ")";
				QMetaObject::invokeMethod(
					QCoreApplication::instance(),
					[selfPtr, msg]() {
						if (selfPtr)
							selfPtr->onYouTubeUpdateDone(
								false,
								QString::fromStdString(msg));
					},
					Qt::QueuedConnection);
				return;
			}
			const auto doc =
				QJsonDocument::fromJson(QByteArray::fromStdString(r.body));
			const auto items = doc.object()["items"].toArray();
			if (items.isEmpty()) {
				// 配信中でない場合はエラーにせずスキップ
				QMetaObject::invokeMethod(
					QCoreApplication::instance(),
					[selfPtr]() {
						if (selfPtr)
							selfPtr->onYouTubeUpdateDone(true, {});
					},
					Qt::QueuedConnection);
				return;
			}
			bid = items.first().toObject()["id"].toString().toStdString();
		}

		// Step 2: scheduledStartTime を GET で取得（PATCH に必要）
		std::string scheduledStartTime;
		{
			const std::string pathStr =
				"/youtube/v3/liveBroadcasts?id=" + bid + "&part=snippet";
			const std::wstring path(pathStr.begin(), pathStr.end());
			auto r = doRequest(L"GET", L"www.googleapis.com", path, {authHdr});
			if (r.ok && r.statusCode == 200) {
				const auto doc = QJsonDocument::fromJson(
					QByteArray::fromStdString(r.body));
				const auto items = doc.object()["items"].toArray();
				if (!items.isEmpty())
					scheduledStartTime =
						items.first()
							.toObject()["snippet"]
							.toObject()["scheduledStartTime"]
							.toString()
							.toStdString();
			}
		}

		// Step 3: liveBroadcasts.update（タイトル・説明・公開設定）
		std::string broadcastErr;
		{
			QJsonObject snippet;
			snippet["title"]       = QString::fromStdString(title);
			snippet["description"] = QString::fromStdString(desc);
			if (!scheduledStartTime.empty())
				snippet["scheduledStartTime"] =
					QString::fromStdString(scheduledStartTime);

			QJsonObject status;
			status["privacyStatus"] = QString::fromStdString(privacy);

			QJsonObject patchBody;
			patchBody["id"]      = QString::fromStdString(bid);
			patchBody["snippet"] = snippet;
			patchBody["status"]  = status;

			const std::string jsonBody =
				QJsonDocument(patchBody).toJson(QJsonDocument::Compact).toStdString();

			auto r = doRequest(L"PATCH", L"www.googleapis.com",
					   L"/youtube/v3/liveBroadcasts?part=snippet,status",
					   {authHdr, "Content-Type: application/json"}, jsonBody);

			if (!r.ok || (r.statusCode != 200 && r.statusCode != 204))
				broadcastErr = "liveBroadcasts 更新失敗 (HTTP " +
					       std::to_string(r.statusCode) + "): " + r.body;
		}

		// Step 4: videos.update（カテゴリ）
		std::string videoErr;
		{
			QJsonObject snippet;
			snippet["title"]       = QString::fromStdString(title);
			snippet["description"] = QString::fromStdString(desc);
			snippet["categoryId"]  = QString::fromStdString(catId);

			QJsonObject patchBody;
			patchBody["id"]      = QString::fromStdString(bid);
			patchBody["snippet"] = snippet;

			const std::string jsonBody =
				QJsonDocument(patchBody).toJson(QJsonDocument::Compact).toStdString();

			auto r = doRequest(L"PATCH", L"www.googleapis.com",
					   L"/youtube/v3/videos?part=snippet",
					   {authHdr, "Content-Type: application/json"}, jsonBody);

			if (!r.ok || (r.statusCode != 200 && r.statusCode != 204))
				videoErr = "videos 更新失敗 (HTTP " +
					   std::to_string(r.statusCode) + "): " + r.body;
		}

		std::string combined = broadcastErr;
		if (!videoErr.empty()) {
			if (!combined.empty())
				combined += "\n";
			combined += videoErr;
		}
		const bool ok = combined.empty();

		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[selfPtr, ok, combined]() {
				if (selfPtr)
					selfPtr->onYouTubeUpdateDone(
						ok, QString::fromStdString(combined));
			},
			Qt::QueuedConnection);
	}).detach();
}

void StreamSettingsDialog::onTwitchUpdateDone(bool ok, const QString &error)
{
	if (!ok && !error.isEmpty())
		errors_ << "Twitch: " + error;
	checkAllDone();
}

void StreamSettingsDialog::onYouTubeUpdateDone(bool ok, const QString &error)
{
	if (!ok && !error.isEmpty())
		errors_ << "YouTube: " + error;
	checkAllDone();
}

void StreamSettingsDialog::checkAllDone()
{
	--pendingCount_;
	if (pendingCount_ > 0)
		return;

	if (!errors_.isEmpty()) {
		QMessageBox::warning(this, "配信設定エラー",
				     "一部の設定が更新できませんでした:\n\n" + errors_.join("\n\n"));
		auto *okBtn = buttonBox_->button(QDialogButtonBox::Ok);
		okBtn->setEnabled(true);
		okBtn->setText("OK");
		return;
	}

	QDialog::accept();
}
