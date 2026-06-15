#pragma once
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
}
