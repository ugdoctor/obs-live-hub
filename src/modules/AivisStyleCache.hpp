#pragma once
#include <functional>
#include <QString>
#include <QVector>
#include <cstdint>

struct AivisCachedStyle {
	QString speakerName;
	QString styleName;
	int64_t styleId = 0;
};

// メインスレッド専用（スレッドセーフでない）
namespace AivisStyleCache {
	void                     set(const QVector<AivisCachedStyle> &styles);
	const AivisCachedStyle  *findByName(const QString &name); // 部分一致、最初のヒットを返す
	bool                     isEmpty();
	// 1 回だけ /speakers を取得してキャッシュ更新（バックグラウンド）
	void                     fetchAndCacheAsync(const QString &aivisUrl);
	// エンジン起動後用: 1 秒ごとにリトライ、最大 maxSeconds 秒
	void                     fetchAndCacheWithRetry(const QString &aivisUrl, int maxSeconds = 30);
	// /speakers 取得+キャッシュ更新、先頭話者をメインスレッドの cb に通知
	void                     fetchAndCacheAsyncWithResult(
		const QString &url,
		std::function<void(bool ok, int64_t styleId,
		                   const QString &speakerName, const QString &styleName)> cb);
}
