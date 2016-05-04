#pragma once

#include <mutex>
#include <list>
#include <chrono>


class Client;

enum class MobStatus
{
	IDLE,
	MOVING
};

using high_resolution_clock = std::chrono::high_resolution_clock;

struct Mob
{
	int type;
	int id;
	int ingameID;
	int x;
	int y;
	int radius;
	int maxHp;
	int maxMp;
	int hp;
	int mp;

	MobStatus status;
	high_resolution_clock::time_point lastStatusChange;
};

class Map
{
public:
	Map(int id);

	inline bool needsUpdate() { return _needsUpdate; }
	void update();

	void addPlayer(Client* client);

private:
	int _id;
	bool _needsUpdate;

	std::mutex _playersMutex;
	std::list<Client*> _players;
	std::list<Mob*> _mobs;
};
