#include "nc_time.hpp"

void nc::Timer::Start()
{
	start_time = std::chrono::steady_clock::now();
	running = true;
}

void nc::Timer::Stop(bool reset)
{
	start_time = {};
	running = false;
}

bool nc::Timer::IsRunning() const
{
	return running;
}

float nc::Timer::Ellapsed() const
{
	if (!running)
		return 0.0f;

	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<float> elapsed = now - start_time;
	return elapsed.count();
}
