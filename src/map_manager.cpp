#include "GameServer/map_manager.h"
#include "GameServer/map.h"
#include "CommonServer/threadpool.h"

MapManager* MapManager::_instance = nullptr;

MapManager::MapManager()
{
	for (int id = 0; id < 255; ++id)
	{
		_maps.emplace(id, new Map(id));
	}
}

void MapManager::update()
{
	for (auto&& pair : _maps)
	{
		Map* map = pair.second;
		if (map->needsUpdate())
		{
			gPool->postWork<void>([map]() {
				map->update();
			});
		}
	}
}
