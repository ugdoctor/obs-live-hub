#pragma once
#include <functional>
#include <mutex>
#include <unordered_map>

// スレッドセーフなPub/Subイベントバス。
// UIへの通知はQtのキューを経由させること（Qt::QueuedConnection）。
template<typename EventT>
class EventBus {
public:
	using Handler = std::function<void(const EventT &)>;
	using SubId = size_t;

	SubId subscribe(Handler handler)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		SubId id = nextId_++;
		handlers_[id] = std::move(handler);
		return id;
	}

	void unsubscribe(SubId id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		handlers_.erase(id);
	}

	void publish(const EventT &event)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto &[id, handler] : handlers_)
			handler(event);
	}

private:
	std::unordered_map<SubId, Handler> handlers_;
	std::mutex mutex_;
	SubId nextId_ = 0;
};
