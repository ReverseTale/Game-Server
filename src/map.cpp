#include "GameServer/map.h"
#include "GameServer/client.h"
#include "CommonServer/database.h"
#include "GameServer/world_handler.h"
#include "GameServer/mob.h"

#include <Game/packet.h>


using namespace Net;


Map::Map(int id) :
	_id(id),
	_needsUpdate(false)
{
	auto future = gDB("world")->query<bool>([this](mongocxx::database db) {
		bsoncxx::builder::stream::document filter_builder;
		filter_builder << "map" << _id;

		auto cursor = db["monsters"].find(filter_builder.view());
		for (auto&& doc : cursor)
		{
			_mobs.emplace_back(new Mob{
				doc["type"].get_int32(),
				doc["mob_id"].get_int32(),
				WorldHandler::get()->getFreeID(),
				{ doc["x"].get_int32(), doc["y"].get_int32() },
				doc["radius"].get_int32(),
				doc["hp"].get_int32(),
				doc["mp"].get_int32(),
				doc["hp"].get_int32(),
				doc["mp"].get_int32()
			});
		}

		return true;
	});
}

void Map::update()
{
	std::lock_guard<std::mutex> lock(_playersMutex);

	for (auto* client : _players)
	{
		// ??
	}

	for (Mob* mob : _mobs)
	{
		if (mob->update())
		{
			Packet* mv = gFactory->make(PacketType::SERVER_GAME, nullptr, mob->getMovementPacket());
			broadcastPacket(mv);
		}
	}

	while (!_broadcastList.empty())
	{
		Broadcaster* broadcaster = _broadcastList.front();
		_broadcastList.pop_front();

		for (auto* client : _players)
		{
			if (!broadcaster->exceptions.empty())
			{
				auto it = std::find(broadcaster->exceptions.begin(), broadcaster->exceptions.end(), client);
				if (it != broadcaster->exceptions.end())
				{
					continue;
				}
			}

			Packet* broadcastPacket = broadcaster->packet->clone(client->session());
			broadcaster->packet->send(client);
		}
	}
}

void Map::addPlayer(Client* client)
{
	for (auto* mob : _mobs)
	{
		Packet* spawn = gFactory->make(PacketType::SERVER_GAME, client->session(), mob->getSpawnPacket());
		spawn->send(client);
	}

	// TODO: Broadcast player spawn
	client->setMap(this);

	// in 1 [name] - [ingameID] [x] [y] [orientacion] 0 [sexo] [pelo] [color] [clase] [gorro].[cuello].[arma].?.[boca].?.[cuello2?].-1 [hp%] [mp%] 0 -1 [posicion-bicho-volador] [atributo-pj] 0 0 0 0 1 1 [familia,-1=no] [nombre,-=ninguno] [1=principiante,13=experto...] 0(1=desaparece) 0 0 0 [lvl] [brillo-familia?] 0(1=aura?) 0(1=aura?) [size,10=normal] 0
	Packet* spawn = gFactory->make(PacketType::SERVER_GAME, client->session(), client->getSpawnPacket());
	broadcastPacket(spawn, { client });

	std::lock_guard<std::mutex> lock(_playersMutex);
	
	for (auto* other : _players)
	{
		spawn = gFactory->make(PacketType::SERVER_GAME, other->session(), other->getSpawnPacket());
		spawn->send(client);
	}

	_players.push_back(client);
	_needsUpdate = true;
}

void Map::broadcastPacket(Packet* packet, std::vector<Client*>&& exceptions)
{
	_broadcastList.emplace_back(new Broadcaster{ packet, exceptions });
}
