#pragma once

#include "GameServer/common.h"

#include <mutex>
#include <list>
#include <vector>


class Client;
class Mob;

namespace Net
{
	class Packet;
}

struct Broadcaster
{
	Net::Packet* packet;
	std::vector<Client*> exceptions;
};

class Map
{
public:
	Map(int id);

	inline int id() { return _id; }
	inline bool needsUpdate() { return _needsUpdate; }
	void update();

	void addPlayer(Client* client);
	void broadcastPacket(Net::Packet* packet, std::vector<Client*>&& exceptions = {});

private:
	int _id;
	bool _needsUpdate;

	std::mutex _playersMutex;
	std::list<Client*> _players;
	std::list<Mob*> _mobs;

	std::list<Broadcaster*> _broadcastList;
};
