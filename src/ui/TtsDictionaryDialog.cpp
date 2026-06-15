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

#include "TtsDictionaryDialog.hpp"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QStringConverter>
#include <QTextStream>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ─── ローカルユーティリティ ──────────────────────────────────────────────────

// クォート対応 CSV 行パーサー（RFC 4180 簡易版）
static QStringList parseCsvLine(const QString &line)
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

// カンマ・ダブルクォート・改行を含む場合はクォートして出力
static QString csvField(const QString &s)
{
	if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r'))
		return '"' + QString(s).replace('"', QStringLiteral("\"\"")) + '"';
	return s;
}

// JSON 文字列エスケープ（UTF-8 バイト列で処理）
static std::string jsonEscape(const QString &s)
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

// ─── 静的パブリックメソッド ──────────────────────────────────────────────────

QString TtsDictionaryDialog::csvPath()
{
#ifdef _WIN32
	wchar_t appdata[MAX_PATH] = {};
	GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	return QString::fromWCharArray(appdata) +
	       "\\obs-studio\\plugins\\obs-live-hub\\tts_dictionary.csv";
#else
	return {};
#endif
}

std::string TtsDictionaryDialog::makeDictJson()
{
	const QString path = csvPath();
	std::string json = "{\"type\":\"tts_dict\",\"entries\":[";
	bool first = true;

	QFile file(path);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&file);
		in.setEncoding(QStringConverter::Utf8);
		while (!in.atEnd()) {
			const QString line = in.readLine();
			if (line.trimmed().isEmpty())
				continue;
			const QStringList fields = parseCsvLine(line);
			if (fields.size() < 2 || fields[0].isEmpty())
				continue;
			if (!first)
				json += ',';
			first = false;
			json += "{\"from\":\"" + jsonEscape(fields[0]) + "\",\"to\":\"" +
			        jsonEscape(fields[1]) + "\"}";
		}
	}

	json += "]}";
	return json;
}

// ─── コンストラクタ ──────────────────────────────────────────────────────────

TtsDictionaryDialog::TtsDictionaryDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("obs-live-hub 読み上げ辞書");
	resize(540, 440);

	// テーブル
	table_ = new QTableWidget(0, 2, this);
	table_->setHorizontalHeaderLabels({"検索文字列", "読み替え文字列"});
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->verticalHeader()->setDefaultSectionSize(26);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setDragEnabled(true);
	table_->setAcceptDrops(true);
	table_->setDragDropMode(QAbstractItemView::InternalMove);
	table_->setDropIndicatorShown(true);

	// ボタン
	addBtn_    = new QPushButton("行を追加", this);
	delBtn_    = new QPushButton("選択行を削除", this);
	upBtn_     = new QPushButton("↑", this);
	downBtn_   = new QPushButton("↓", this);
	importBtn_ = new QPushButton("CSVインポート", this);
	exportBtn_ = new QPushButton("CSVエクスポート", this);
	upBtn_->setFixedWidth(36);
	downBtn_->setFixedWidth(36);

	buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *btnRow = new QHBoxLayout;
	btnRow->addWidget(addBtn_);
	btnRow->addWidget(delBtn_);
	btnRow->addWidget(upBtn_);
	btnRow->addWidget(downBtn_);
	btnRow->addStretch();
	btnRow->addWidget(importBtn_);
	btnRow->addWidget(exportBtn_);

	auto *vlay = new QVBoxLayout(this);
	vlay->addLayout(btnRow);
	vlay->addWidget(table_, 1);
	vlay->addWidget(buttonBox_);

	connect(addBtn_,    &QPushButton::clicked, this, &TtsDictionaryDialog::onAddRow);
	connect(delBtn_,    &QPushButton::clicked, this, &TtsDictionaryDialog::onDeleteRow);
	connect(upBtn_,     &QPushButton::clicked, this, &TtsDictionaryDialog::onMoveUp);
	connect(downBtn_,   &QPushButton::clicked, this, &TtsDictionaryDialog::onMoveDown);
	connect(importBtn_, &QPushButton::clicked, this, &TtsDictionaryDialog::onImportCsv);
	connect(exportBtn_, &QPushButton::clicked, this, &TtsDictionaryDialog::onExportCsv);
	connect(buttonBox_, &QDialogButtonBox::accepted, this, &TtsDictionaryDialog::accept);
	connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

	loadFromCsv(csvPath());
}

// ─── プライベートメソッド ────────────────────────────────────────────────────

void TtsDictionaryDialog::addTableRow(const QString &from, const QString &to)
{
	const int row = table_->rowCount();
	table_->insertRow(row);
	table_->setItem(row, 0, new QTableWidgetItem(from));
	table_->setItem(row, 1, new QTableWidgetItem(to));
}

void TtsDictionaryDialog::loadFromCsv(const QString &path)
{
	table_->setRowCount(0);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QTextStream in(&file);
	in.setEncoding(QStringConverter::Utf8);
	while (!in.atEnd()) {
		const QString line = in.readLine();
		if (line.trimmed().isEmpty())
			continue;
		const QStringList fields = parseCsvLine(line);
		if (fields.size() >= 2)
			addTableRow(fields[0], fields[1]);
	}
}

bool TtsDictionaryDialog::saveToCsv(const QString &path) const
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		return false;
	QTextStream out(&file);
	out.setEncoding(QStringConverter::Utf8);
	for (int row = 0; row < table_->rowCount(); ++row) {
		const auto *itemFrom = table_->item(row, 0);
		const auto *itemTo   = table_->item(row, 1);
		if (!itemFrom || !itemTo)
			continue;
		out << csvField(itemFrom->text()) << ',' << csvField(itemTo->text()) << '\n';
	}
	return true;
}

// ─── スロット ────────────────────────────────────────────────────────────────

void TtsDictionaryDialog::accept()
{
	saveToCsv(csvPath());
	QDialog::accept();
}

void TtsDictionaryDialog::onAddRow()
{
	addTableRow();
	table_->scrollToBottom();
	const int row = table_->rowCount() - 1;
	table_->setCurrentCell(row, 0);
	if (table_->item(row, 0))
		table_->editItem(table_->item(row, 0));
}

void TtsDictionaryDialog::onDeleteRow()
{
	// 選択行を下から削除（行番号のズレを防ぐ）
	QList<int> rows;
	for (const auto &r : table_->selectedRanges())
		for (int i = r.topRow(); i <= r.bottomRow(); ++i)
			if (!rows.contains(i))
				rows.append(i);
	std::sort(rows.begin(), rows.end(), std::greater<int>());
	for (int r : rows)
		table_->removeRow(r);
}

void TtsDictionaryDialog::onMoveUp()
{
	const int row = table_->currentRow();
	if (row <= 0)
		return;
	for (int col = 0; col < 2; ++col) {
		auto *a = table_->takeItem(row - 1, col);
		auto *b = table_->takeItem(row, col);
		table_->setItem(row - 1, col, b);
		table_->setItem(row, col, a);
	}
	table_->setCurrentCell(row - 1, table_->currentColumn());
}

void TtsDictionaryDialog::onMoveDown()
{
	const int row = table_->currentRow();
	if (row < 0 || row >= table_->rowCount() - 1)
		return;
	for (int col = 0; col < 2; ++col) {
		auto *a = table_->takeItem(row, col);
		auto *b = table_->takeItem(row + 1, col);
		table_->setItem(row, col, b);
		table_->setItem(row + 1, col, a);
	}
	table_->setCurrentCell(row + 1, table_->currentColumn());
}

void TtsDictionaryDialog::onImportCsv()
{
	const QString path = QFileDialog::getOpenFileName(
		this, "CSVファイルを選択", {},
		"CSV ファイル (*.csv);;すべてのファイル (*)");
	if (path.isEmpty())
		return;
	loadFromCsv(path);
}

void TtsDictionaryDialog::onExportCsv()
{
	const QString path = QFileDialog::getSaveFileName(
		this, "CSVファイルとして保存", "tts_dictionary.csv",
		"CSV ファイル (*.csv);;すべてのファイル (*)");
	if (path.isEmpty())
		return;
	if (!saveToCsv(path))
		QMessageBox::warning(this, "エクスポート失敗",
				     "ファイルへの書き込みに失敗しました:\n" + path);
}
