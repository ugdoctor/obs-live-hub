#pragma once
#include <map>
#include <string>

namespace EngineManager {

enum class EngineState {
	NotStarted,
	Starting,
	Connected,
	Error
};

struct EngineStatus {
	EngineState state        = EngineState::NotStarted;
	int         speakerCount = 0;
	std::string errorMsg;
};

// 有効化されたエンジンを全て起動・接続確認する（OBS 起動時に呼ぶ）
void startAll();

// 起動した全エンジンプロセスを終了する（OBS 終了時に呼ぶ）
void stopAll();

// エンジン名 → 状態マップを返す（UI 参照用）
std::map<std::string, EngineStatus> getAllStatuses();

} // namespace EngineManager
