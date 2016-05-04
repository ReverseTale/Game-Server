#pragma once

#include <mutex>
#include <list>
#include <chrono>


class Client;

namespace Net
{
	class Packet;
}

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
	uint32_t ingameID;
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

	inline int id() { return _id; }
	inline bool needsUpdate() { return _needsUpdate; }
	void update();

	void addPlayer(Client* client);
	void broadcastPacket(Net::Packet* packet);

private:
	int _id;
	bool _needsUpdate;

	std::mutex _playersMutex;
	std::list<Client*> _players;
	std::list<Mob*> _mobs;

	std::list<Net::Packet*> _broadcastList;
};
