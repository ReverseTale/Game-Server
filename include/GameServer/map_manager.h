#pragma once

#include <map>

class Map;

class MapManager
{
public:
	static MapManager* get()
	{
		if (!_instance)
		{
			_instance = new MapManager();
		}

		return _instance;
	}

	void update();

	inline Map* map(int id) { return _maps[id]; }

private:
	MapManager();

private:
	static MapManager* _instance;
	std::map<int, Map*> _maps;
};