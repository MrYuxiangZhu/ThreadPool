#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <stdexcept>
#include <functional>

namespace std
{

class threadpool
{
public:
	threadpool(size_t thread_num) noexcept;

	~threadpool() noexcept;
	
	template <typename _Func, typename ..._Args>
	auto submit(_Func&& func, _Args&&... args) -> future<typename std::result_of<_Func(_Args...)>::type>;
private:
	using _MTask = std::function<void()>;
	std::mutex _TaskLock;
	std::condition_variable _TaskCV;
	std::atomic<bool> _TaskStop;
	std::atomic<int> _ThreadNum;
	std::queue<_MTask> _QueueTasks;
	std::vector<std::thread> _ThreadWorkers;
};

threadpool::threadpool(size_t thread_num) noexcept : _TaskStop(false)
{
	for (size_t i = 0; i < thread_num; ++i)
	{
		_ThreadWorkers.emplace_back(
			[this]
			{
				while (1)
				{
					_MTask task;

					{
						std::unique_lock<std::mutex> lock(_TaskLock);
						_TaskCV.wait(lock, [this] { return _TaskStop || !_QueueTasks.empty(); });
						if (_TaskStop && _QueueTasks.empty())
						{
							return;
						}
						task = std::move(_QueueTasks.front());
						_QueueTasks.pop();
					}

					task();
				}
			}
		);
	}
}

threadpool::~threadpool() noexcept
{
	{
		std::unique_lock<std::mutex> lock(_TaskLock);
		_TaskStop = true;
	}

	_TaskCV.notify_all();

	for (auto& thread : _ThreadWorkers)
	{
		thread.join();
	}
}

template <typename _Func, typename ..._Args>
auto threadpool::submit(_Func&& func, _Args&&... args) -> future<typename std::result_of<_Func(_Args...)>::type>
{
	if (this->_TaskStop)
	{
		throw runtime_error("submit on ThreadPool is stopped.");
	}

	using ret_type = typename std::result_of<_Func(_Args...)>::type;
	/*create task pointer*/
	std::shared_ptr<std::packaged_task<ret_type()>> ptask = std::make_shared<std::packaged_task<ret_type()>>(std::bind(std::forward<_Func>(func), std::forward<_Args>(args)...));
	std::future<ret_type> ret_future = ptask->get_future();

	{
		std::unique_lock<std::mutex> lock(_TaskLock);
		_QueueTasks.emplace([ptask]() { (*ptask)(); });
	}

	/*wake up thread*/
	_TaskCV.notify_one();

	return ret_future;
}

}
