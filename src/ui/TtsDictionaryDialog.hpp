#pragma once
#include <string>

#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>

class TtsDictionaryDialog : public QDialog {
	Q_OBJECT
public:
	explicit TtsDictionaryDialog(QWidget *parent = nullptr);

	// デフォルト CSV パスを返す
	static QString csvPath();
	// CSV を読んで tts_dict JSON を生成する（plugin-main から呼ぶ）
	static std::string makeDictJson();

private slots:
	void accept() override;
	void onAddRow();
	void onDeleteRow();
	void onMoveUp();
	void onMoveDown();
	void onImportCsv();
	void onExportCsv();

private:
	void loadFromCsv(const QString &path);
	bool saveToCsv(const QString &path) const;
	void addTableRow(const QString &from = {}, const QString &to = {});

	QTableWidget     *table_;
	QPushButton      *addBtn_;
	QPushButton      *delBtn_;
	QPushButton      *upBtn_;
	QPushButton      *downBtn_;
	QPushButton      *importBtn_;
	QPushButton      *exportBtn_;
	QDialogButtonBox *buttonBox_;
};
