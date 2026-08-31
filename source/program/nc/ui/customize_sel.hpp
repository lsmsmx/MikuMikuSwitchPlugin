#pragma once

void InstallCustomizeSelHooks();

namespace customize_sel
{
	void ShowWindow();

	inline void init() {
		InstallCustomizeSelHooks();
	}
}
