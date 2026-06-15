#pragma once
#include <QString>

// overlay.html の配置場所を検索する。
// 検索順: %APPDATA%\obs-studio\plugins\obs-live-hub\ → obs_module_file → DLL 隣接フォルダ
QString findOverlayHtmlPath();
