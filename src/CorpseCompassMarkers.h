#pragma once

#include <cstddef>

namespace CorpseCompassMarkers
{
	struct InsertResult
	{
		std::size_t added{ 0 };
		std::size_t unresolved{ 0 };
		std::size_t full{ 0 };
		std::size_t failed{ 0 };
	};

	void InstallHook();
	InsertResult AppendTrackedMarkers();
}
