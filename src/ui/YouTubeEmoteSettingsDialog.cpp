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

#include "YouTubeEmoteSettingsDialog.hpp"
#include "core/PluginConfig.hpp"

#include <algorithm>

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

constexpr int kPreviewSize = 28;

// 対応する画像拡張子（小文字）
bool isSupportedImageExt(const QString &ext)
{
	static const QStringList kExts = {"png", "jpg", "jpeg", "gif", "webp", "bmp"};
	return kExts.contains(ext.toLower());
}

// クォート対応 CSV 行パーサー（RFC 4180 簡易版、TtsDictionaryDialogと同様）
QStringList parseCsvLine(const QString &line)
{
	QStringList fields;
	QString field;
	bool inQuotes = false;
	for (int i = 0; i < line.size(); ++i) {
		const QChar c = line[i];
		if (inQuotes) {
			if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
				field += '"';
				++i;
			} else if (c == '"') {
				inQuotes = false;
			} else {
				field += c;
			}
		} else {
			if (c == '"') {
				inQuotes = true;
			} else if (c == ',') {
				fields.append(field);
				field.clear();
			} else {
				field += c;
			}
		}
	}
	fields.append(field);
	return fields;
}

QString csvField(const QString &s)
{
	if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r'))
		return '"' + QString(s).replace('"', QStringLiteral("\"\"")) + '"';
	return s;
}

std::string jsonEscapeUtf8(const QString &s)
{
	const QByteArray bytes = s.toUtf8();
	std::string r;
	r.reserve(static_cast<size_t>(bytes.size()) + 8);
	for (const unsigned char c : bytes) {
		if (c == '"')
			r += "\\\"";
		else if (c == '\\')
			r += "\\\\";
		else if (c == '\n')
			r += "\\n";
		else if (c == '\r') {
			// skip
		} else {
			r += static_cast<char>(c);
		}
	}
	return r;
}

} // namespace

// ─── 静的パブリックメソッド ──────────────────────────────────────────────────

QString YouTubeEmoteSettingsDialog::imagesDir()
{
#ifdef _WIN32
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return QString::fromWCharArray(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\youtube_emotes";
#else
	return {};
#endif
}

QString YouTubeEmoteSettingsDialog::csvPath()
{
#ifdef _WIN32
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return QString::fromWCharArray(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\youtube_emotes.csv";
#else
	return {};
#endif
}

std::string YouTubeEmoteSettingsDialog::makeSyncJson()
{
	const int port = PluginConfig::instance().wsPort;
	std::string json = "{\"type\":\"youtube_emotes_sync\",\"entries\":[";
	bool first = true;

	QFile file(csvPath());
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&file);
		in.setEncoding(QStringConverter::Utf8);
		while (!in.atEnd()) {
			const QString line = in.readLine();
			if (line.trimmed().isEmpty())
				continue;
			const QStringList fields = parseCsvLine(line);
			if (fields.size() < 2 || fields[0].isEmpty() || fields[1].isEmpty())
				continue;
			if (!first)
				json += ',';
			first = false;
			const QString url = QString("http://127.0.0.1:%1/emotes/%2")
						     .arg(port)
						     .arg(fields[1]);
			json += "{\"code\":\"" + jsonEscapeUtf8(fields[0]) + "\",\"url\":\"" +
				jsonEscapeUtf8(url) + "\"}";
		}
	}

	json += "]}";
	return json;
}

// ─── コンストラクタ ──────────────────────────────────────────────────────────

YouTubeEmoteSettingsDialog::YouTubeEmoteSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("YouTubeエモート辞書設定");
	resize(560, 460);
	setAcceptDrops(true);

	auto *dropHint = new QLabel(
		"⬇ ここに画像ファイル（PNG/JPEG/GIF等）をドラッグ＆ドロップすると自動的に登録されます ⬇",
		this);
	dropHint->setAlignment(Qt::AlignCenter);
	dropHint->setWordWrap(true);
	dropHint->setStyleSheet(
		"padding: 10px; border: 2px dashed #888; border-radius: 6px; color: #aaa;");

	table_ = new QTableWidget(0, 3, this);
	table_->setHorizontalHeaderLabels({"文字コード（ショートコード）", "画像プレビュー", "ファイル名"});
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
	table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	table_->setColumnWidth(1, kPreviewSize + 16);
	table_->verticalHeader()->setDefaultSectionSize(kPreviewSize + 8);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setAcceptDrops(true); // テーブル上へのドロップもダイアログ側で受け取る

	addFilesBtn_ = new QPushButton("画像ファイルを選んで追加...", this);
	delBtn_      = new QPushButton("選択行を削除", this);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttonBox_->button(QDialogButtonBox::Ok)->setText("保存して閉じる");

	auto *btnRow = new QHBoxLayout;
	btnRow->addWidget(addFilesBtn_);
	btnRow->addWidget(delBtn_);
	btnRow->addStretch();

	auto *vlay = new QVBoxLayout(this);
	vlay->addWidget(dropHint);
	vlay->addWidget(table_, 1);
	vlay->addLayout(btnRow);
	vlay->addWidget(buttonBox_);

	connect(addFilesBtn_, &QPushButton::clicked, this,
	        &YouTubeEmoteSettingsDialog::onAddFilesClicked);
	connect(delBtn_, &QPushButton::clicked, this, &YouTubeEmoteSettingsDialog::onDeleteRow);
	connect(buttonBox_, &QDialogButtonBox::accepted, this,
	        &YouTubeEmoteSettingsDialog::accept);
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QDir().mkpath(imagesDir());
	loadFromCsv();
}

// ─── ドラッグ＆ドロップ ──────────────────────────────────────────────────────

void YouTubeEmoteSettingsDialog::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void YouTubeEmoteSettingsDialog::dropEvent(QDropEvent *event)
{
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls()) {
		if (url.isLocalFile())
			paths << url.toLocalFile();
	}
	if (!paths.isEmpty()) {
		importImageFiles(paths);
		event->acceptProposedAction();
	}
}

// ─── プライベートメソッド ────────────────────────────────────────────────────

QString YouTubeEmoteSettingsDialog::suggestCodeFromFileName(const QString &fileName)
{
	const QString base = QFileInfo(fileName).completeBaseName();
	return ":" + base + ":";
}

void YouTubeEmoteSettingsDialog::addTableRow(const QString &code, const QString &fileName)
{
	const int row = table_->rowCount();
	table_->insertRow(row);

	table_->setItem(row, 0, new QTableWidgetItem(code));

	auto *previewLabel = new QLabel(table_);
	previewLabel->setAlignment(Qt::AlignCenter);
	QPixmap pix(imagesDir() + "/" + fileName);
	if (!pix.isNull()) {
		previewLabel->setPixmap(pix.scaled(kPreviewSize, kPreviewSize, Qt::KeepAspectRatio,
		                                    Qt::SmoothTransformation));
	} else {
		previewLabel->setText("？");
		previewLabel->setToolTip("画像ファイルが見つかりません: " + fileName);
	}
	table_->setCellWidget(row, 1, previewLabel);

	auto *fileItem = new QTableWidgetItem(fileName);
	fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
	table_->setItem(row, 2, fileItem);
}

void YouTubeEmoteSettingsDialog::importImageFiles(const QStringList &paths)
{
	QDir().mkpath(imagesDir());

	QStringList skipped;
	int addedCount = 0;
	int lastAddedRow = -1;

	for (const QString &srcPath : paths) {
		const QFileInfo info(srcPath);
		if (!info.isFile() || !isSupportedImageExt(info.suffix())) {
			skipped << info.fileName();
			continue;
		}

		const QString uniqueName =
			QUuid::createUuid().toString(QUuid::WithoutBraces) + "." +
			info.suffix().toLower();
		const QString destPath = imagesDir() + "/" + uniqueName;

#ifdef _WIN32
		const bool ok = CopyFileW(reinterpret_cast<LPCWSTR>(srcPath.utf16()),
		                          reinterpret_cast<LPCWSTR>(destPath.utf16()), FALSE) != 0;
#else
		const bool ok = QFile::copy(srcPath, destPath);
#endif
		if (!ok) {
			skipped << info.fileName();
			continue;
		}

		addTableRow(suggestCodeFromFileName(info.fileName()), uniqueName);
		lastAddedRow = table_->rowCount() - 1;
		++addedCount;
	}

	if (lastAddedRow >= 0) {
		table_->scrollToBottom();
		table_->setCurrentCell(lastAddedRow, 0);
		if (table_->item(lastAddedRow, 0))
			table_->editItem(table_->item(lastAddedRow, 0));
	}

	if (!skipped.isEmpty()) {
		QMessageBox::warning(
			this, "一部のファイルを追加できませんでした",
			QString("以下のファイルは画像として認識できないか、コピーに失敗したため"
			        "スキップされました:\n\n%1")
				.arg(skipped.join('\n')));
	}
	Q_UNUSED(addedCount);
}

void YouTubeEmoteSettingsDialog::loadFromCsv()
{
	table_->setRowCount(0);
	QFile file(csvPath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QTextStream in(&file);
	in.setEncoding(QStringConverter::Utf8);
	while (!in.atEnd()) {
		const QString line = in.readLine();
		if (line.trimmed().isEmpty())
			continue;
		const QStringList fields = parseCsvLine(line);
		if (fields.size() >= 2 && !fields[0].isEmpty() && !fields[1].isEmpty())
			addTableRow(fields[0], fields[1]);
	}
}

bool YouTubeEmoteSettingsDialog::saveToCsv() const
{
	const QString path = csvPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		return false;
	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	for (int row = 0; row < table_->rowCount(); ++row) {
		const auto *codeItem = table_->item(row, 0);
		const auto *fileItem = table_->item(row, 2);
		if (!codeItem || !fileItem || codeItem->text().isEmpty())
			continue;
		out << csvField(codeItem->text()) << ',' << csvField(fileItem->text()) << '\n';
	}
	return true;
}

// ─── スロット ────────────────────────────────────────────────────────────────

void YouTubeEmoteSettingsDialog::accept()
{
	if (!saveToCsv()) {
		QMessageBox::warning(this, "保存失敗",
		                     "設定ファイルへの書き込みに失敗しました:\n" + csvPath());
		return;
	}
	QDialog::accept();
}

void YouTubeEmoteSettingsDialog::onDeleteRow()
{
	// 選択行を下から削除（行番号のズレを防ぐ）し、対応する画像ファイルも削除する
	QList<int> rows;
	for (const auto &r : table_->selectedRanges())
		for (int i = r.topRow(); i <= r.bottomRow(); ++i)
			if (!rows.contains(i))
				rows.append(i);
	std::sort(rows.begin(), rows.end(), std::greater<int>());

	for (int row : rows) {
		const auto *fileItem = table_->item(row, 2);
		if (fileItem && !fileItem->text().isEmpty())
			QFile::remove(imagesDir() + "/" + fileItem->text());
		table_->removeRow(row);
	}
}

void YouTubeEmoteSettingsDialog::onAddFilesClicked()
{
	const QStringList paths = QFileDialog::getOpenFileNames(
		this, "追加する画像ファイルを選択", {},
		"画像ファイル (*.png *.jpg *.jpeg *.gif *.webp *.bmp);;すべてのファイル (*)");
	if (!paths.isEmpty())
		importImageFiles(paths);
}
