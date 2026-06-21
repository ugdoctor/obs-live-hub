#pragma once
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>

class WsServer;

class BrowserDiagDialog : public QDialog {
	Q_OBJECT
public:
	explicit BrowserDiagDialog(WsServer *wsServer, QWidget *parent = nullptr);

private slots:
	void onTimer();
	void onRefresh();
	void onReload();

private:
	void updateStatus();
	void scanSources();
	int  reloadSources(); // リロードしたソース数を返す

	WsServer    *wsServer_;
	QLabel      *serverStatusLabel_;
	QLabel      *clientCountLabel_;
	QListWidget *sourceList_;
	QLabel      *resultLabel_;
	QPushButton *reloadBtn_;
	QTimer      *timer_;
};
