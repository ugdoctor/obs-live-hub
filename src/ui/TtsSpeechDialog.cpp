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
#include "modules/EngineManager.hpp"

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
	checkEngineConnectionCheck_ = new QCheckBox(
		"エンジン接続チェックを有効にする（OFFにすると未接続エンジンでも強制的に使用）", this);

	// ── TTSエンジン選択 (有効化 + デフォルト) ──
	engineListGroup_ = new QGroupBox("TTSエンジン", this);
	auto *engineGrid = new QGridLayout(engineListGroup_);
	engineGrid->setSpacing(6);

	auto *hdrEnabled = new QLabel("有効化", engineListGroup_);
	auto *hdrDefault = new QLabel("デフォルト", engineListGroup_);
	auto *hdrStatus  = new QLabel("接続状態", engineListGroup_);
	hdrEnabled->setAlignment(Qt::AlignCenter);
	hdrDefault->setAlignment(Qt::AlignCenter);
	engineGrid->addWidget(hdrEnabled, 0, 0, Qt::AlignCenter);
	engineGrid->addWidget(hdrDefault, 0, 1);
	engineGrid->addWidget(hdrStatus,  0, 2);
	engineGrid->setColumnStretch(1, 1); // エンジン名列を伸縮させる

	defaultGroup_ = new QButtonGroup(this);

	for (int i = 0; i < kEngineCount; ++i) {
		engineEnabledCheck_[i] = new QCheckBox(engineListGroup_);
		engineDefaultRadio_[i] = new QRadioButton(kEngines[i].label, engineListGroup_);
		engineStatusLabel_[i]  = new QLabel("―", engineListGroup_);
		engineStatusLabel_[i]->setMinimumWidth(90);
		defaultGroup_->addButton(engineDefaultRadio_[i], i);

		engineGrid->addWidget(engineEnabledCheck_[i], i + 1, 0, Qt::AlignCenter);
		engineGrid->addWidget(engineDefaultRadio_[i], i + 1, 1);
		engineGrid->addWidget(engineStatusLabel_[i],  i + 1, 2);

		if (i == 0) { // webspeech: 常時有効、チェックを外せない
			engineEnabledCheck_[i]->setChecked(true);
			engineEnabledCheck_[i]->setEnabled(false);
			engineStatusLabel_[i]->setText("常時利用可");
			engineStatusLabel_[i]->setStyleSheet("color: #00aa44;");
		}
		engineDefaultRadio_[i]->setEnabled(i == 0);
	}

	recheckBtn_ = new QPushButton("接続状態を再確認", engineListGroup_);
	engineGrid->addWidget(recheckBtn_, kEngineCount + 1, 0, 1, 3);

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

	aivisEngineStatusLabel_ = new QLabel("● 停止中", aivisGroup_);
	aivisEngineStatusLabel_->setStyleSheet("color: #cc3333; font-weight: bold;");

	startEngineBtn_ = new QPushButton("起動", aivisGroup_);
	stopEngineBtn_  = new QPushButton("停止", aivisGroup_);

	auto *ctrlRow = new QWidget(aivisGroup_);
	auto *ctrlH   = new QHBoxLayout(ctrlRow);
	ctrlH->setContentsMargins(0, 0, 0, 0);
	ctrlH->addWidget(aivisEngineStatusLabel_, 1);
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

	// 実行ファイルパス
	bouyomiExePathEdit_ = new QLineEdit(bouyomiGroup_);
	bouyomiExePathEdit_->setPlaceholderText(
		"例: C:\\BouyomiChan\\BouyomiChan.exe（自動起動する場合に設定）");
	browseBouyomiExeBtn_ = new QPushButton("参照...", bouyomiGroup_);
	browseBouyomiExeBtn_->setFixedWidth(64);

	auto *bouyomiPathRow = new QWidget(bouyomiGroup_);
	auto *bouyomiPathH   = new QHBoxLayout(bouyomiPathRow);
	bouyomiPathH->setContentsMargins(0, 0, 0, 0);
	bouyomiPathH->addWidget(bouyomiExePathEdit_, 1);
	bouyomiPathH->addWidget(browseBouyomiExeBtn_);

	bouyomiAutoStartCheck_ =
		new QCheckBox("OBS起動時に棒読みちゃんを自動起動する", bouyomiGroup_);

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
		"<small>※声番号は棒読みちゃんの「声質」タブの並び順（追加ソフトで番号がずれる場合あり）<br>"
		"※接続失敗（停止中）が続く場合は棒読みちゃんの HTTP 連携が有効か確認してください</small>",
		bouyomiGroup_);
	bouyomiNote->setWordWrap(true);

	auto *bouyomiForm = new QFormLayout(bouyomiGroup_);
	bouyomiForm->setSpacing(6);
	bouyomiForm->addRow("実行ファイル:", bouyomiPathRow);
	bouyomiForm->addRow("",              bouyomiAutoStartCheck_);
	bouyomiForm->addRow(new QFrame(bouyomiGroup_));
	bouyomiForm->addRow("ホスト:",       bouyomiHostEdit_);
	bouyomiForm->addRow("ポート:",       bouyomiPortSpin_);
	bouyomiForm->addRow("声の種類:",     voiceRow);
	bouyomiForm->addRow("",              bouyomiNote);

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
	form->addRow("",            checkEngineConnectionCheck_);
	form->addRow(engineListGroup_);
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

	QObject::connect(defaultGroup_, &QButtonGroup::idClicked,
	                 this, &TtsSpeechDialog::onDefaultEngineChanged);

	for (int i = 1; i < kEngineCount; ++i) { // webspeech(0) は常時有効なので除外
		QObject::connect(engineEnabledCheck_[i], &QCheckBox::toggled, this,
		                 [this, i](bool checked) { onEngineEnabledToggled(i, checked); });
	}

	QObject::connect(recheckBtn_, &QPushButton::clicked,
	                 this, &TtsSpeechDialog::onRecheckClicked);

	QObject::connect(browseEngineBtn_, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			this, "エンジン実行ファイルを選択",
			enginePathEdit_->text(), "実行ファイル (*.exe)");
		if (!path.isEmpty())
			enginePathEdit_->setText(path);
	});

	QObject::connect(browseBouyomiExeBtn_, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			this, "棒読みちゃん 実行ファイルを選択",
			bouyomiExePathEdit_->text(), "実行ファイル (*.exe)");
		if (!path.isEmpty())
			bouyomiExePathEdit_->setText(path);
	});

	QObject::connect(bouyomiAutoStartCheck_, &QCheckBox::toggled,
	                 this, [this](bool checked) {
		bouyomiExePathEdit_->setEnabled(checked);
		browseBouyomiExeBtn_->setEnabled(checked);
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
	connect(statusTimer, &QTimer::timeout, this, [this]() {
		updateEngineStatus();
		refreshEngineStatuses();
	});
	statusTimer->start(500);

	loadFromConfig();
}

// ──────────────────────────────────────────────────────────────
// スロット実装
// ──────────────────────────────────────────────────────────────
void TtsSpeechDialog::onDefaultEngineChanged(int engineIdx)
{
	const bool voicevox  = (engineIdx >= 1 && engineIdx <= 4);
	const bool isBouyomi = (engineIdx == 5);
	aivisGroup_->setVisible(voicevox);
	bouyomiGroup_->setVisible(isBouyomi);

	if (isBouyomi) {
		const auto &cfg = PluginConfig::instance();
		bouyomiExePathEdit_->setText(QString::fromStdString(cfg.bouyomiExePath));
		bouyomiAutoStartCheck_->setChecked(cfg.bouyomiAutoStart);
		bouyomiExePathEdit_->setEnabled(cfg.bouyomiAutoStart);
		browseBouyomiExeBtn_->setEnabled(cfg.bouyomiAutoStart);
		bouyomiHostEdit_->setText(QString::fromStdString(cfg.bouyomiHost));
		bouyomiPortSpin_->setValue(cfg.bouyomiPort);
		bouyomiVoiceSpin_->setValue(cfg.bouyomiVoice);
		adjustSize();
		return;
	}

	if (!voicevox) {
		adjustSize();
		return;
	}

	// グループタイトル・プレースホルダーを更新
	const auto &e = kEngines[engineIdx];
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

	if (engineIdx == 1) { // aivisspeech
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
	} else if (engineIdx == 2) { // sharevox
		loadEngineValues(cfg.sharevoxUrl, cfg.sharevoxEnginePath, cfg.sharevoxAutoStart);
	} else if (engineIdx == 3) { // lmroid
		loadEngineValues(cfg.lmroidUrl, cfg.lmroidEnginePath, cfg.lmroidAutoStart);
	} else if (engineIdx == 4) { // itvoice
		loadEngineValues(cfg.itvoiceUrl, cfg.itvoiceEnginePath, cfg.itvoiceAutoStart);
	}

	// 話者リストをクリア（エンジン変更時は再取得が必要）
	speakerCombo_->clear();
	styleCombo_->clear();
	speakers_.clear();

	adjustSize();
}

void TtsSpeechDialog::onEngineEnabledToggled(int engineIdx, bool enabled)
{
	engineDefaultRadio_[engineIdx]->setEnabled(enabled);

	if (!enabled && engineDefaultRadio_[engineIdx]->isChecked()) {
		// デフォルトに設定されていたエンジンが無効化された → webspeech に切り替え
		engineDefaultRadio_[0]->setChecked(true);
		onDefaultEngineChanged(0);
	}
}

void TtsSpeechDialog::updateEngineStatus()
{
	// 「起動」「停止」ボタンの有効/無効のみ担当
	// ラベル更新は refreshEngineStatuses() が担当
	const bool running = AivisEngine::isRunning();
	startEngineBtn_->setEnabled(!running);
	stopEngineBtn_->setEnabled(running);
}

void TtsSpeechDialog::refreshEngineStatuses()
{
	const auto statuses = EngineManager::getAllStatuses();

	for (int i = 1; i < kEngineCount; ++i) { // webspeech(0) は常時利用可で固定
		if (!engineEnabledCheck_[i]->isChecked()) {
			engineStatusLabel_[i]->setText("―");
			engineStatusLabel_[i]->setStyleSheet("color: gray;");
			continue;
		}

		const auto it = statuses.find(std::string(kEngines[i].id));
		if (it == statuses.end()) {
			engineStatusLabel_[i]->setText("未確認");
			engineStatusLabel_[i]->setStyleSheet("color: gray;");
			continue;
		}

		switch (it->second.state) {
		case EngineManager::EngineState::NotStarted:
			engineStatusLabel_[i]->setText("未確認");
			engineStatusLabel_[i]->setStyleSheet("color: gray;");
			break;
		case EngineManager::EngineState::Starting:
			engineStatusLabel_[i]->setText("確認中...");
			engineStatusLabel_[i]->setStyleSheet("color: #cc8800;");
			break;
		case EngineManager::EngineState::Connected:
			if (it->second.speakerCount > 0)
				engineStatusLabel_[i]->setText(
					QString("接続中 (%1話者)").arg(it->second.speakerCount));
			else
				engineStatusLabel_[i]->setText("接続中");
			engineStatusLabel_[i]->setStyleSheet("color: #00aa44;");
			break;
		case EngineManager::EngineState::Error:
			engineStatusLabel_[i]->setText("停止中");
			engineStatusLabel_[i]->setStyleSheet("color: #cc3333;");
			break;
		}
	}

	// 下部パネルの「エンジン状態」ラベルも同期する
	if (aivisGroup_->isVisible()) {
		const int idx = defaultGroup_->checkedId();
		if (idx >= 1 && idx <= 4) {
			const auto it = statuses.find(std::string(kEngines[idx].id));
			if (it == statuses.end()) {
				aivisEngineStatusLabel_->setText("● 未確認");
				aivisEngineStatusLabel_->setStyleSheet("color: gray; font-weight: bold;");
			} else {
				switch (it->second.state) {
				case EngineManager::EngineState::Starting:
					aivisEngineStatusLabel_->setText("● 確認中...");
					aivisEngineStatusLabel_->setStyleSheet("color: #cc8800; font-weight: bold;");
					break;
				case EngineManager::EngineState::Connected:
					aivisEngineStatusLabel_->setText("● 接続中");
					aivisEngineStatusLabel_->setStyleSheet("color: #00cc44; font-weight: bold;");
					break;
				case EngineManager::EngineState::NotStarted:
				case EngineManager::EngineState::Error:
					aivisEngineStatusLabel_->setText("● 停止中");
					aivisEngineStatusLabel_->setStyleSheet("color: #cc3333; font-weight: bold;");
					break;
				}
			}
		}
	}
}

void TtsSpeechDialog::onRecheckClicked()
{
	// 画面上のチェック状態と PluginConfig がズレていると refreshAll() が誤動作するため、
	// 先に現在の UI 状態を保存してから接続確認を行う。
	saveToConfig();
	recheckBtn_->setEnabled(false);
	EngineManager::refreshAll();
	refreshEngineStatuses(); // 即座に "確認中..." 表示
	// HTTP チェックは最大 3 秒のタイムアウト。5 秒後にボタンを再有効化
	QTimer::singleShot(5000, this, [this]() {
		recheckBtn_->setEnabled(true);
		refreshEngineStatuses();
	});
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
	checkEngineConnectionCheck_->setChecked(cfg.ttsCheckEngineConnection);

	// 有効化チェックボックス（シグナルをブロックして一括設定し、後で手動同期する）
	{
		QSignalBlocker b1(engineEnabledCheck_[1]);
		QSignalBlocker b2(engineEnabledCheck_[2]);
		QSignalBlocker b3(engineEnabledCheck_[3]);
		QSignalBlocker b4(engineEnabledCheck_[4]);
		QSignalBlocker b5(engineEnabledCheck_[5]);
		engineEnabledCheck_[1]->setChecked(cfg.aivisspeechEnabled);
		engineEnabledCheck_[2]->setChecked(cfg.sharevoxEnabled);
		engineEnabledCheck_[3]->setChecked(cfg.lmroidEnabled);
		engineEnabledCheck_[4]->setChecked(cfg.itvoiceEnabled);
		engineEnabledCheck_[5]->setChecked(cfg.bouyomiEnabled);
	}
	for (int i = 0; i < kEngineCount; ++i)
		engineDefaultRadio_[i]->setEnabled(engineEnabledCheck_[i]->isChecked());

	// デフォルトエンジンのラジオボタン選択
	int defaultIdx = 0;
	if      (cfg.ttsEngine == "aivisspeech") defaultIdx = 1;
	else if (cfg.ttsEngine == "sharevox")    defaultIdx = 2;
	else if (cfg.ttsEngine == "lmroid")      defaultIdx = 3;
	else if (cfg.ttsEngine == "itvoice")     defaultIdx = 4;
	else if (cfg.ttsEngine == "bouyomi")     defaultIdx = 5;
	// デフォルトエンジンが無効化されていたら webspeech にフォールバック
	if (!engineDefaultRadio_[defaultIdx]->isEnabled())
		defaultIdx = 0;
	engineDefaultRadio_[defaultIdx]->setChecked(true);
	onDefaultEngineChanged(defaultIdx); // URL/path/autostart をロードしてパネル表示切り替え

	// 保存済み話者名をプレースホルダーとして表示
	if (!cfg.aivisSpeakerName.empty()) {
		const QString hint = QString("（保存済み: %1 / %2）")
		                         .arg(QString::fromStdString(cfg.aivisSpeakerName),
		                              QString::fromStdString(cfg.aivisStyleName));
		speakerCombo_->addItem(hint);
	}

	updateEngineStatus();
	EngineManager::refreshAll();
	refreshEngineStatuses();
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
	cfg.ttsTwitch                = twitchCheck_->isChecked();
	cfg.ttsYoutube               = youtubeCheck_->isChecked();
	cfg.ttsCheckEngineConnection = checkEngineConnectionCheck_->isChecked();

	const int    idx  = defaultGroup_->checkedId();
	const QString url  = aivisUrlEdit_->text().trimmed();
	const QString path = enginePathEdit_->text().trimmed();
	const bool   autoS = autoStartCheck_->isChecked();

	if (idx == 5) { // bouyomi
		cfg.ttsEngine        = "bouyomi";
		cfg.bouyomiExePath   = bouyomiExePathEdit_->text().trimmed().toStdString();
		cfg.bouyomiAutoStart = bouyomiAutoStartCheck_->isChecked();
		cfg.bouyomiHost      = bouyomiHostEdit_->text().trimmed().toStdString();
		if (cfg.bouyomiHost.empty()) cfg.bouyomiHost = "localhost";
		cfg.bouyomiPort  = bouyomiPortSpin_->value();
		cfg.bouyomiVoice = bouyomiVoiceSpin_->value();
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

	// 有効化フラグをチェックボックスの状態から設定（webspeech は常時有効なのでフラグ不要）
	cfg.aivisspeechEnabled = engineEnabledCheck_[1]->isChecked();
	cfg.sharevoxEnabled    = engineEnabledCheck_[2]->isChecked();
	cfg.lmroidEnabled      = engineEnabledCheck_[3]->isChecked();
	cfg.itvoiceEnabled     = engineEnabledCheck_[4]->isChecked();
	cfg.bouyomiEnabled     = engineEnabledCheck_[5]->isChecked();
	cfg.save();
}

void TtsSpeechDialog::accept()
{
	saveToConfig();
	QDialog::accept();
}
