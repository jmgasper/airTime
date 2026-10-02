/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_WORKER_POOL_H
#define AIRTIME_WORKER_POOL_H


#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>


namespace airtime {

/*!	A few threads that split a picture's rows between them. Run() returns
	when every band is done. */
class WorkerPool {
public:
	WorkerPool(int threads = 0)
		:
		fStop(false),
		fGeneration(0),
		fPending(0)
	{
		if (threads <= 0) {
			unsigned cpus = std::thread::hardware_concurrency();
			threads = cpus > 8 ? 8 : cpus > 1 ? (int)cpus : 2;
		}
		for (int i = 0; i < threads - 1; i++)
			fThreads.emplace_back(&WorkerPool::_Work, this, i + 1);
		fCount = threads;
	}

	~WorkerPool()
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fStop = true;
			fWake.notify_all();
		}
		for (std::thread& thread : fThreads)
			thread.join();
	}

	// Calls function(first, last) for bands of [0, rows).
	void Run(int rows, const std::function<void(int, int)>& function)
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fFunction = function;
			fRows = rows;
			fPending = fCount - 1;
			fGeneration++;
			fWake.notify_all();
		}
		_Band(0);
		std::unique_lock<std::mutex> lock(fLock);
		while (fPending > 0)
			fDone.wait(lock);
		fFunction = nullptr;
	}

private:
	void _Band(int index)
	{
		int first = fRows * index / fCount;
		int last = fRows * (index + 1) / fCount;
		if (last > first)
			fFunction(first, last);
	}

	void _Work(int index)
	{
		uint64_t seen = 0;
		for (;;) {
			{
				std::unique_lock<std::mutex> lock(fLock);
				while (!fStop && fGeneration == seen)
					fWake.wait(lock);
				if (fStop)
					return;
				seen = fGeneration;
			}
			_Band(index);
			std::lock_guard<std::mutex> lock(fLock);
			if (--fPending == 0)
				fDone.notify_all();
		}
	}

	std::vector<std::thread>	fThreads;
	int							fCount;
	std::mutex					fLock;
	std::condition_variable		fWake;
	std::condition_variable		fDone;
	bool						fStop;
	uint64_t					fGeneration;
	int							fPending;
	int							fRows;
	std::function<void(int, int)> fFunction;
};

}	// namespace airtime

#endif	// AIRTIME_WORKER_POOL_H
