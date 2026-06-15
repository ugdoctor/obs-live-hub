/*
obs-live-hub
Copyright (C) 2026 ugdoctor
*/

#include "AivisStyleCache.hpp"

static QVector<AivisCachedStyle> s_cache;

void AivisStyleCache::set(const QVector<AivisCachedStyle> &styles)
{
	s_cache = styles;
}

const AivisCachedStyle *AivisStyleCache::findByName(const QString &name)
{
	const QString lower = name.toLower();
	for (const auto &s : s_cache) {
		if (s.speakerName.toLower().contains(lower) ||
		    s.styleName.toLower().contains(lower))
			return &s;
	}
	return nullptr;
}

bool AivisStyleCache::isEmpty()
{
	return s_cache.isEmpty();
}
