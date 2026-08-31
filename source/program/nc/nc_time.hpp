#pragma once

#include <chrono>

namespace nc
{
	class Timer
	{
	public:
		void Start();
		void Stop(bool reset = false);
		bool IsRunning() const;

		float Ellapsed() const;

	private:
		std::chrono::steady_clock::time_point start_time{};
		bool running = false;
	};
}
