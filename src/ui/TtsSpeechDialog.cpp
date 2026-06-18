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

#include "TtsSpeechDialog.hpp"
#include "core/PluginConfig.hpp"
#include "modules/AivisEngine.hpp"
#include "modules/AivisStyleCache.hpp"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <thread>

// ──────────────────────────────────────────────────────────────
// WinHTTP 同期 GET（std::thread 内で呼び出す）
// ──────────────────────────────────────────────────────────────
#ifdef _WIN32
struct UrlParts {
	std::wstring  host;
	INTERNET_PORT port    = 80;
	bool          isHttps = false;
};

static UrlParts parseAivisUrl(const QString &baseUrl)
{
	UrlParts parts;
	QString  url = baseUrl.trimmed();
	if (url.startsWith("https://", Qt::CaseInsensitive)) {
		parts.isHttps = true;
		url           = url.mid(8);
		parts.port    = 443;
	} else if (url.startsWith("http://", Qt::CaseInsensitive)) {
		url        = url.mid(7);
		parts.port = 80;
	}
	const int slash = url.indexOf('/');
	if (slash >= 0)
		url = url.left(slash);
	const int colon = url.indexOf(':');
	if (colon >= 0) {
		parts.host = url.left(colon).toStdWString();
		parts.port = static_cast<INTERNET_PORT>(url.mid(colon + 1).toInt());
	} else {
		parts.host = url.toStdWString();
	}
	return parts;
}

static bool winHttpGetJson(const QString &baseUrl, const wchar_t *path, QByteArray &outBody)
{
	const UrlParts p = parseAivisUrl(baseUrl);
	if (p.host.empty())
		return false;

	HINTERNET hSession = WinHttpOpen(L"obs-live-hub/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
	                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		return false;

	HINTERNET hConnect = WinHttpConnect(hSession, p.host.c_str(), p.port, 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest =
		WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
	                           WINHTTP_DEFAULT_ACCEPT_TYPES, p.isHttps ? WINHTTP_FLAG_SECURE : 0);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const bool ok =
		WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
	                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(hRequest, nullptr) != FALSE;

	if (ok) {
		char  buf[4096];
		DWORD bytesRead = 0;
		while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0)
			outBody.append(buf, static_cast<int>(bytesRead));
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok;
}
#endif // _WIN32

// ──────────────────────────────────────────────────────────────
// エンジン選択インデックスと設定キーの対応
//   0: webspeech
//   1: aivisspeech
//   2: sharevox
//   3: lmroid
//   4: itvoice
// ──────────────────────────────────────────────────────────────
static const struct {
	const char *id;
	const char *label;
	const char *defaultUrl;
	const char *pathPlaceholder;
} kEngines[] = {
	{ "webspeech",   "Web Speech API（ブラウザ）",  "",                       "" },
	{ "aivisspeech", "AivisSpeech（ローカル）",      "http://localhost:10101", "AivisSpeech-Engine\\run.exe のパス" },
	{ "sharevox",    "SHAREVOX（ローカル）",          "http://localhost:50025", "SHAREVOX\\run.exe のパス（任意）" },
	{ "lmroid",      "LMROID（ローカル）",            "http://localhost:49973", "LMROID\\run.exe のパス（任意）" },
	{ "itvoice",     "ITVOICE（ローカル）",           "http://localhost:49540", "ITVOICE\\run.exe のパス（任意）" },
	{ "bouyomi",     "棒読みちゃん（ローカル）",      "",                       "" },
};

// 棒読みちゃん標準 voice 番号一覧（AquesTalk 同梱声）
static const struct { int num; const char *label; } kBouyomiVoices[] = {
	{  0, "0: 自動 (デフォルト声)" },
	{  1, "1: 女性1 (AquesTalk)" },
	{  2, "2: 女性2 (AquesTalk)" },
	{  3, "3: 中性  (AquesTalk)" },
	{  4, "4: 男性1 (AquesTalk)" },
	{  5, "5: 中性2 (AquesTalk)" },
	{  6, "6: ロボット (AquesTalk)" },
	{  7, "7: 機械1 (AquesTalk)" },
	{  8, "8: 機械2 (AquesTalk)" },
	{  9, "9: 女性3 (AquesTalk)" },
	{ 10, "10: 女性4 (AquesTalk)" },
	{ -1, "-1: 前回と同じ" },
};

// ──────────────────────────────────────────────────────────────
// TtsSpeechDialog
// ──────────────────────────────────────────────────────────────
TtsSpeechDialog::TtsSpeechDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 読み上げ設定");
	setMinimumWidth(480);

	// ── 共通 TTS 設定 ──
	enabledCheck_ = new QCheckBox("読み上げを有効にする", this);

	volumeSlider_ = new QSlider(Qt::Horizontal, this);
	volumeSlider_->setRange(0, 100);
	volumeLabel_ = new QLabel(this);
	volumeLabel_->setMinimumWidth(40);

	rateSlider_ = new QSlider(Qt::Horizontal, this);
	rateSlider_->setRange(50, 200);
	rateLabel_ = new QLabel(this);
	rateLabel_->setMinimumWidth(40);

	pitchSlider_ = new QSlider(Qt::Horizontal, this);
	pitchSlider_->setRange(0, 200);
	pitchLabel_ = new QLabel(this);
	pitchLabel_->setMinimumWidth(40);

	readUsernameCheck_ = new QCheckBox("ユーザー名を読み上げる", this);

	maxLengthSpin_ = new QSpinBox(this);
	maxLengthSpin_->setRange(0, 500);
	maxLengthSpin_->setSuffix(" 文字");
	maxLengthSpin_->setSpecialValueText("無制限");

	twitchCheck_  = new QCheckBox("Twitch コメントを読み上げる", this);
	youtubeCheck_ = new QCheckBox("YouTube コメントを読み上げる", this);

	// ── TTSエンジン選択 ──
	engineCombo_ = new QComboBox(this);
	for (const auto &e : kEngines)
		engineCombo_->addItem(e.label);

	// ── VOICEVOX互換エンジン設定グループ ──
	aivisGroup_ = new QGroupBox("VOICEVOX互換エンジン設定", this);

	// ── エンジン制御 ──
	enginePathEdit_  = new QLineEdit(aivisGroup_);
	browseEngineBtn_ = new QPushButton("参照...", aivisGroup_);
	browseEngineBtn_->setFixedWidth(64);

	auto *pathRow = new QWidget(aivisGroup_);
	auto *pathH   = new QHBoxLayout(pathRow);
	pathH->setContentsMargins(0, 0, 0, 0);
	pathH->addWidget(enginePathEdit_, 1);
	pathH->addWidget(browseEngineBtn_);

	engineStatusLabel_ = new QLabel("● 停止中", aivisGroup_);
	engineStatusLabel_->setStyleSheet("color: #cc3333; font-weight: bold;");

	startEngineBtn_ = new QPushButton("起動", aivisGroup_);
	stopEngineBtn_  = new QPushButton("停止", aivisGroup_);

	auto *ctrlRow = new QWidget(aivisGroup_);
	auto *ctrlH   = new QHBoxLayout(ctrlRow);
	ctrlH->setContentsMargins(0, 0, 0, 0);
	ctrlH->addWidget(engineStatusLabel_, 1);
	ctrlH->addWidget(startEngineBtn_);
	ctrlH->addWidget(stopEngineBtn_);

	autoStartCheck_ = new QCheckBox("OBS起動時に自動的にエンジンを起動する", aivisGroup_);

	// ── 接続・音声設定 ──
	aivisUrlEdit_ = new QLineEdit(aivisGroup_);

	speakerCombo_ = new QComboBox(aivisGroup_);
	styleCombo_   = new QComboBox(aivisGroup_);
	refreshBtn_   = new QPushButton("話者を更新", aivisGroup_);

	auto *speakerRow = new QWidget(aivisGroup_);
	auto *speakerH   = new QHBoxLayout(speakerRow);
	speakerH->setContentsMargins(0, 0, 0, 0);
	speakerH->addWidget(speakerCombo_, 1);
	speakerH->addWidget(refreshBtn_);

	auto *voicevoxNote = new QLabel(
		"<small>※VOICEVOX互換エンジン使用時はTTS音声ページをChromeで開いてください<br>"
		"※手動起動の場合は <code>--allow_origin \"*\"</code> または "
		"<code>--cors_policy_mode all</code> オプションを付けて起動してください</small>",
		aivisGroup_);
	voicevoxNote->setWordWrap(true);

	auto *aivisForm = new QFormLayout(aivisGroup_);
	aivisForm->setSpacing(6);
	aivisForm->addRow("エンジンパス:", pathRow);
	aivisForm->addRow("エンジン状態:", ctrlRow);
	aivisForm->addRow("",             autoStartCheck_);
	aivisForm->addRow(new QFrame(aivisGroup_));
	aivisForm->addRow("URL:",         aivisUrlEdit_);
	aivisForm->addRow("話者:",        speakerRow);
	aivisForm->addRow("スタイル:",    styleCombo_);
	aivisForm->addRow("",             voicevoxNote);

	// ── 棒読みちゃん設定グループ ──
	bouyomiGroup_ = new QGroupBox("棒読みちゃん設定", this);

	bouyomiHostEdit_ = new QLineEdit(bouyomiGroup_);
	bouyomiHostEdit_->setPlaceholderText("localhost");

	bouyomiPortSpin_ = new QSpinBox(bouyomiGroup_);
	bouyomiPortSpin_->setRange(1, 65535);
	bouyomiPortSpin_->setValue(50080);

	bouyomiVoiceCombo_ = new QComboBox(bouyomiGroup_);
	for (const auto &v : kBouyomiVoices)
		bouyomiVoiceCombo_->addItem(v.label);
	bouyomiVoiceCombo_->addItem("その他 (直接入力)");

	bouyomiVoiceSpin_ = new QSpinBox(bouyomiGroup_);
	bouyomiVoiceSpin_->setRange(-1, 10000);
	bouyomiVoiceSpin_->setSpecialValueText("-1 (前回と同じ)");
	bouyomiVoiceSpin_->setPrefix("voice: ");

	auto *voiceRow = new QWidget(bouyomiGroup_);
	auto *voiceH   = new QHBoxLayout(voiceRow);
	voiceH->setContentsMargins(0, 0, 0, 0);
	voiceH->addWidget(bouyomiVoiceCombo_, 1);
	voiceH->addWidget(bouyomiVoiceSpin_);

	auto *bouyomiNote = new QLabel(
		"<small>※声番号は棒読みちゃんの「声質」タブの並び順（追加ソフトで番号がずれる場合あり）</small>",
		bouyomiGroup_);
	bouyomiNote->setWordWrap(true);

	auto *bouyomiForm = new QFormLayout(bouyomiGroup_);
	bouyomiForm->setSpacing(6);
	bouyomiForm->addRow("ホスト:",   bouyomiHostEdit_);
	bouyomiForm->addRow("ポート:",   bouyomiPortSpin_);
	bouyomiForm->addRow("声の種類:", voiceRow);
	bouyomiForm->addRow("",          bouyomiNote);

	// コンボ変更 → スピンボックスに反映
	QObject::connect(bouyomiVoiceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, [this](int idx) {
		// 最後の項目は「その他」なので kBouyomiVoices の範囲外
		const int n = static_cast<int>(sizeof(kBouyomiVoices) / sizeof(kBouyomiVoices[0]));
		if (idx >= 0 && idx < n) {
			QSignalBlocker bl(bouyomiVoiceSpin_);
			bouyomiVoiceSpin_->setValue(kBouyomiVoices[idx].num);
		}
	});

	// スピンボックス変更 → 対応するコンボ項目を選択（なければ「その他」）
	QObject::connect(bouyomiVoiceSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
	                 this, [this](int v) {
		const int n = static_cast<int>(sizeof(kBouyomiVoices) / sizeof(kBouyomiVoices[0]));
		for (int i = 0; i < n; ++i) {
			if (kBouyomiVoices[i].num == v) {
				QSignalBlocker bl(bouyomiVoiceCombo_);
				bouyomiVoiceCombo_->setCurrentIndex(i);
				return;
			}
		}
		QSignalBlocker bl(bouyomiVoiceCombo_);
		bouyomiVoiceCombo_->setCurrentIndex(n); // 「その他」
	});

	// ── ボタンボックス ──
	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto makeSliderRow = [this](QSlider *sl, QLabel *lb) -> QWidget * {
		auto *w = new QWidget(this);
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(sl, 1);
		h->addWidget(lb);
		return w;
	};

	auto *form = new QFormLayout(this);
	form->setSpacing(8);
	form->addRow("",            enabledCheck_);
	form->addRow("音量:",        makeSliderRow(volumeSlider_, volumeLabel_));
	form->addRow("速度:",        makeSliderRow(rateSlider_,   rateLabel_));
	form->addRow("ピッチ:",      makeSliderRow(pitchSlider_,  pitchLabel_));
	form->addRow("",            readUsernameCheck_);
	form->addRow("最大文字数:",  maxLengthSpin_);
	form->addRow("",            twitchCheck_);
	form->addRow("",            youtubeCheck_);
	form->addRow("TTSエンジン:", engineCombo_);
	form->addRow(aivisGroup_);
	form->addRow(bouyomiGroup_);
	form->addRow(buttonBox_);

	// ── シグナル接続 ──
	QObject::connect(volumeSlider_, &QSlider::valueChanged, volumeLabel_, [this](int v) {
		volumeLabel_->setText(QString::number(v) + "%");
	});
	QObject::connect(rateSlider_, &QSlider::valueChanged, rateLabel_, [this](int v) {
		rateLabel_->setText(QString::number(v / 100.0, 'f', 2) + "x");
	});
	QObject::connect(pitchSlider_, &QSlider::valueChanged, pitchLabel_, [this](int v) {
		pitchLabel_->setText(QString::number(v / 100.0, 'f', 2));
	});

	QObject::connect(engineCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, &TtsSpeechDialog::onEngineChanged);

	QObject::connect(browseEngineBtn_, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			this, "エンジン実行ファイルを選択",
			enginePathEdit_->text(), "実行ファイル (*.exe)");
		if (!path.isEmpty())
			enginePathEdit_->setText(path);
	});

	QObject::connect(startEngineBtn_, &QPushButton::clicked, this, [this]() {
		const QString path = enginePathEdit_->text().trimmed();
		if (path.isEmpty()) {
			QMessageBox::warning(this, "エラー",
			                     "エンジンのパスが設定されていません。");
			return;
		}
		AivisEngine::start(path);
		updateEngineStatus();
	});

	QObject::connect(stopEngineBtn_, &QPushButton::clicked, this, [this]() {
		AivisEngine::stop();
		updateEngineStatus();
	});

	QObject::connect(refreshBtn_, &QPushButton::clicked,
	                 this, &TtsSpeechDialog::onRefreshSpeakersClicked);
	QObject::connect(speakerCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 this, &TtsSpeechDialog::onSpeakerChanged);

	connect(buttonBox_, &QDialogButtonBox::accepted, this, &TtsSpeechDialog::accept);
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *statusTimer = new QTimer(this);
	connect(statusTimer, &QTimer::timeout, this, &TtsSpeechDialog::updateEngineStatus);
	statusTimer->start(500);

	loadFromConfig();
}

// ──────────────────────────────────────────────────────────────
// スロット実装
// ──────────────────────────────────────────────────────────────
void TtsSpeechDialog::onEngineChanged(int index)
{
	const bool voicevox  = (index >= 1 && index <= 4);
	const bool isBouyomi = (index == 5);
	aivisGroup_->setVisible(voicevox);
	bouyomiGroup_->setVisible(isBouyomi);

	if (isBouyomi) {
		const auto &cfg = PluginConfig::instance();
		bouyomiHostEdit_->setText(QString::fromStdString(cfg.bouyomiHost));
		bouyomiPortSpin_->setValue(cfg.bouyomiPort);
		// voice スピンを設定すると valueChanged → combo も連動する
		bouyomiVoiceSpin_->setValue(cfg.bouyomiVoice);
		adjustSize();
		return;
	}

	if (!voicevox) {
		adjustSize();
		return;
	}

	// グループタイトル・プレースホルダーを更新
	const auto &e = kEngines[index];
	aivisGroup_->setTitle(QString(e.label));
	enginePathEdit_->setPlaceholderText(e.pathPlaceholder);
	aivisUrlEdit_->setPlaceholderText(e.defaultUrl);

	// 設定値をエンジンに合わせてロード
	const auto &cfg = PluginConfig::instance();

	auto loadEngineValues = [&](const std::string &url, const std::string &path, bool autoS) {
		aivisUrlEdit_->setText(QString::fromStdString(url));
		enginePathEdit_->setText(QString::fromStdString(path));
		autoStartCheck_->setChecked(autoS);
	};

	if (index == 1) { // aivisspeech
		QString enginePath = QString::fromStdString(cfg.aivisEnginePath);
#ifdef _WIN32
		if (enginePath.isEmpty()) {
			DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
			if (needed > 0) {
				std::wstring localAppData(needed - 1, L'\0');
				GetEnvironmentVariableW(L"LOCALAPPDATA", &localAppData[0], needed);
				const QString candidate =
					QString::fromStdWString(localAppData) +
					"\\Programs\\AivisSpeech\\AivisSpeech-Engine\\run.exe";
				if (QFile::exists(candidate))
					enginePath = candidate;
			}
		}
#endif
		aivisUrlEdit_->setText(QString::fromStdString(cfg.aivisUrl));
		enginePathEdit_->setText(enginePath);
		autoStartCheck_->setChecked(cfg.aivisAutoStart);
	} else if (index == 2) { // sharevox
		loadEngineValues(cfg.sharevoxUrl, cfg.sharevoxEnginePath, cfg.sharevoxAutoStart);
	} else if (index == 3) { // lmroid
		loadEngineValues(cfg.lmroidUrl, cfg.lmroidEnginePath, cfg.lmroidAutoStart);
	} else if (index == 4) { // itvoice
		loadEngineValues(cfg.itvoiceUrl, cfg.itvoiceEnginePath, cfg.itvoiceAutoStart);
	}

	// 話者リストをクリア（エンジン変更時は再取得が必要）
	speakerCombo_->clear();
	styleCombo_->clear();
	speakers_.clear();

	adjustSize();
}

void TtsSpeechDialog::updateEngineStatus()
{
	if (AivisEngine::isRunning()) {
		engineStatusLabel_->setText("● 起動中");
		engineStatusLabel_->setStyleSheet("color: #00cc44; font-weight: bold;");
		startEngineBtn_->setEnabled(false);
		stopEngineBtn_->setEnabled(true);
	} else {
		engineStatusLabel_->setText("● 停止中");
		engineStatusLabel_->setStyleSheet("color: #cc3333; font-weight: bold;");
		startEngineBtn_->setEnabled(true);
		stopEngineBtn_->setEnabled(false);
	}
}

void TtsSpeechDialog::onRefreshSpeakersClicked()
{
	refreshBtn_->setEnabled(false);
	speakerCombo_->clear();
	styleCombo_->clear();

	const QString url = aivisUrlEdit_->text().trimmed();
	QPointer<TtsSpeechDialog> self = this;

	std::thread([self, url]() {
		QVector<AivisSpeakerInfo> speakers;
		bool ok = false;

#ifdef _WIN32
		QByteArray body;
		ok = winHttpGetJson(url, L"/speakers", body);
		if (ok) {
			QJsonParseError err;
			const auto doc = QJsonDocument::fromJson(body, &err);
			ok = doc.isArray();
			if (ok) {
				for (const auto &v : doc.array()) {
					const auto obj = v.toObject();
					AivisSpeakerInfo sp;
					sp.name = obj["name"].toString();
					sp.uuid = obj["speaker_uuid"].toString();
					for (const auto &sv : obj["styles"].toArray()) {
						const auto sobj = sv.toObject();
						AivisStyleInfo st;
						st.name = sobj["name"].toString();
						st.id   = sobj["id"].toInteger();
						sp.styles.append(st);
					}
					speakers.append(sp);
				}
			}
		}
#endif

		QMetaObject::invokeMethod(qApp, [self, ok, speakers]() {
			if (!self)
				return;
			if (!ok) {
				self->speakerCombo_->addItem(
					"取得失敗（エンジンが起動中か確認してください）");
				self->refreshBtn_->setEnabled(true);
				return;
			}
			self->onSpeakersLoaded(speakers);
		}, Qt::QueuedConnection);
	}).detach();
}

void TtsSpeechDialog::onSpeakersLoaded(QVector<AivisSpeakerInfo> speakers)
{
	speakers_ = std::move(speakers);
	speakerCombo_->clear();
	styleCombo_->clear();
	refreshBtn_->setEnabled(true);

	// AivisStyleCache を更新（[olh] model: コマンドの名前解決用）
	QVector<AivisCachedStyle> cacheEntries;
	for (const auto &sp : speakers_) {
		for (const auto &st : sp.styles) {
			AivisCachedStyle entry;
			entry.speakerName = sp.name;
			entry.styleName   = st.name;
			entry.styleId     = st.id;
			cacheEntries.append(entry);
		}
	}
	AivisStyleCache::set(cacheEntries);

	for (const auto &sp : speakers_)
		speakerCombo_->addItem(sp.name);

	// 保存済みの話者・スタイルを選択状態に復元
	const auto  &cfg       = PluginConfig::instance();
	const QString savedUuid = QString::fromStdString(cfg.aivisSpeakerUuid);
	for (int i = 0; i < speakers_.size(); ++i) {
		if (speakers_[i].uuid == savedUuid) {
			speakerCombo_->setCurrentIndex(i);
			onSpeakerChanged(i);
			const auto &sp = speakers_[i];
			for (int j = 0; j < sp.styles.size(); ++j) {
				if (sp.styles[j].id == cfg.aivisStyleId) {
					styleCombo_->setCurrentIndex(j);
					break;
				}
			}
			return;
		}
	}
	if (!speakers_.isEmpty())
		onSpeakerChanged(0);
}

void TtsSpeechDialog::onSpeakerChanged(int index)
{
	styleCombo_->clear();
	if (index < 0 || index >= speakers_.size())
		return;
	for (const auto &st : speakers_[index].styles)
		styleCombo_->addItem(st.name);
}

// ──────────────────────────────────────────────────────────────
// 設定の読み書き
// ──────────────────────────────────────────────────────────────
void TtsSpeechDialog::loadFromConfig()
{
	const auto &cfg = PluginConfig::instance();
	enabledCheck_->setChecked(cfg.ttsEnabled);
	volumeSlider_->setValue(static_cast<int>(cfg.ttsVolume * 100.0f + 0.5f));
	rateSlider_->setValue(static_cast<int>(cfg.ttsRate * 100.0f + 0.5f));
	pitchSlider_->setValue(static_cast<int>(cfg.ttsPitch * 100.0f + 0.5f));
	readUsernameCheck_->setChecked(cfg.ttsReadUsername);
	maxLengthSpin_->setValue(cfg.ttsMaxLength);
	twitchCheck_->setChecked(cfg.ttsTwitch);
	youtubeCheck_->setChecked(cfg.ttsYoutube);

	// エンジン選択（onEngineChanged が URL/パス/autostart をロードする）
	int engineIdx = 0;
	if      (cfg.ttsEngine == "aivisspeech") engineIdx = 1;
	else if (cfg.ttsEngine == "sharevox")    engineIdx = 2;
	else if (cfg.ttsEngine == "lmroid")      engineIdx = 3;
	else if (cfg.ttsEngine == "itvoice")     engineIdx = 4;
	else if (cfg.ttsEngine == "bouyomi")     engineIdx = 5;
	engineCombo_->setCurrentIndex(engineIdx);
	// setCurrentIndex が onEngineChanged をトリガーし URL/path/autostart がセットされる

	// 保存済み話者名をプレースホルダーとして表示
	if (!cfg.aivisSpeakerName.empty()) {
		const QString hint = QString("（保存済み: %1 / %2）")
		                         .arg(QString::fromStdString(cfg.aivisSpeakerName),
		                              QString::fromStdString(cfg.aivisStyleName));
		speakerCombo_->addItem(hint);
	}

	updateEngineStatus();
}

void TtsSpeechDialog::saveToConfig()
{
	auto &cfg         = PluginConfig::instance();
	cfg.ttsEnabled      = enabledCheck_->isChecked();
	cfg.ttsVolume       = volumeSlider_->value() / 100.0f;
	cfg.ttsRate         = rateSlider_->value() / 100.0f;
	cfg.ttsPitch        = pitchSlider_->value() / 100.0f;
	cfg.ttsReadUsername = readUsernameCheck_->isChecked();
	cfg.ttsMaxLength    = maxLengthSpin_->value();
	cfg.ttsTwitch       = twitchCheck_->isChecked();
	cfg.ttsYoutube      = youtubeCheck_->isChecked();

	const int    idx  = engineCombo_->currentIndex();
	const QString url  = aivisUrlEdit_->text().trimmed();
	const QString path = enginePathEdit_->text().trimmed();
	const bool   autoS = autoStartCheck_->isChecked();

	if (idx == 5) { // bouyomi
		cfg.ttsEngine      = "bouyomi";
		cfg.bouyomiHost    = bouyomiHostEdit_->text().trimmed().toStdString();
		if (cfg.bouyomiHost.empty()) cfg.bouyomiHost = "localhost";
		cfg.bouyomiPort    = bouyomiPortSpin_->value();
		cfg.bouyomiVoice   = bouyomiVoiceSpin_->value();
	} else if (idx == 0) {
		cfg.ttsEngine = "webspeech";
	} else {
		cfg.ttsEngine = kEngines[idx].id;
		if (idx == 1) {
			cfg.aivisUrl        = url.toStdString();
			cfg.aivisEnginePath = path.toStdString();
			cfg.aivisAutoStart  = autoS;
		} else if (idx == 2) {
			cfg.sharevoxUrl         = url.toStdString();
			cfg.sharevoxEnginePath  = path.toStdString();
			cfg.sharevoxAutoStart   = autoS;
		} else if (idx == 3) {
			cfg.lmroidUrl         = url.toStdString();
			cfg.lmroidEnginePath  = path.toStdString();
			cfg.lmroidAutoStart   = autoS;
		} else if (idx == 4) {
			cfg.itvoiceUrl         = url.toStdString();
			cfg.itvoiceEnginePath  = path.toStdString();
			cfg.itvoiceAutoStart   = autoS;
		}

		// 話者・スタイルを保存（全VOICEVOX互換エンジン共通フィールド）
		const int spIdx = speakerCombo_->currentIndex();
		if (spIdx >= 0 && spIdx < speakers_.size()) {
			const auto &sp       = speakers_[spIdx];
			cfg.aivisSpeakerUuid = sp.uuid.toStdString();
			cfg.aivisSpeakerName = sp.name.toStdString();

			const int stIdx = styleCombo_->currentIndex();
			if (stIdx >= 0 && stIdx < sp.styles.size()) {
				cfg.aivisStyleId   = sp.styles[stIdx].id;
				cfg.aivisStyleName = sp.styles[stIdx].name.toStdString();
			}
		}
	}
	cfg.save();
}

void TtsSpeechDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
