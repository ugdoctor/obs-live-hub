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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "OverlayUtils.hpp"

#include <QFileInfo>
#include <obs-module.h>
#include <plugin-support.h>

// overlay.html のパスを解決する。
// 候補1: %APPDATA%\obs-studio\plugins\obs-live-hub\（優先・ユーザー権限で編集可能）
// 候補2: OBS 標準データパス (data/obs-plugins/obs-live-hub/)
// 候補3: DLL 隣接フォルダ  (obs-plugins/64bit/obs-live-hub/)
QString findOverlayHtmlPath()
{
#ifdef _WIN32
	// 1. %APPDATA%\obs-studio\plugins\obs-live-hub\overlay.html
	wchar_t appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0) {
		const QString appdataPath = QString::fromWCharArray(appdata) +
					    "/obs-studio/plugins/obs-live-hub/overlay.html";
		if (QFileInfo::exists(appdataPath)) {
			obs_log(LOG_INFO, "[OverlayUtils] overlay.html found in %%APPDATA%%: %s",
				appdataPath.toUtf8().constData());
			return appdataPath;
		}
	}
#endif

	// 2. obs_module_file() — OBS データディレクトリを検索
	char *p = obs_module_file("overlay.html");
	if (p) {
		const QString path = QString::fromUtf8(p);
		bfree(p);
		if (QFileInfo::exists(path)) {
			obs_log(LOG_INFO, "[OverlayUtils] overlay.html found via obs_module_file: %s",
				path.toUtf8().constData());
			return path;
		}
		obs_log(LOG_INFO, "[OverlayUtils] obs_module_file returned '%s' but file not found",
			path.toUtf8().constData());
	}

#ifdef _WIN32
	// 3. GetModuleFileNameW でこの DLL のパスを取得し、
	//    同階層の {module_name}/overlay.html を探す。
	HMODULE hMod = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			       reinterpret_cast<LPCWSTR>(&findOverlayHtmlPath), &hMod) &&
	    hMod) {
		wchar_t buf[MAX_PATH] = {};
		if (GetModuleFileNameW(hMod, buf, MAX_PATH)) {
			const QFileInfo dll(QString::fromWCharArray(buf));
			const QString cand = dll.absolutePath() + "/" +
					     dll.completeBaseName() + "/overlay.html";
			obs_log(LOG_INFO, "[OverlayUtils] DLL-relative candidate: %s",
				cand.toUtf8().constData());
			if (QFileInfo::exists(cand))
				return cand;
		}
	}
#endif

	return {};
}
