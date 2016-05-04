#include "map.h"
#include "client.h"
#include "database.h"

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
				rand(),
				doc["x"].get_int32(),
				doc["y"].get_int32(),
				doc["radius"].get_int32(),
				doc["hp"].get_int32(),
				doc["mp"].get_int32(),
				doc["hp"].get_int32(),
				doc["mp"].get_int32(),

				MobStatus::IDLE
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
		if (mob->status == MobStatus::IDLE)
		{
			mob->status = MobStatus::MOVING;
			int x = mob->x + (int)((rand() / float(RAND_MAX) * mob->radius) - (mob->radius / 2.0));
			int y = mob->y + (int)((rand() / float(RAND_MAX) * mob->radius) - (mob->radius / 2.0));

			for (auto* client : _players)
			{
				Packet* mv = gFactory->make(PacketType::SERVER_GAME, client->session(), NString("mv "));
				*mv << mob->type << ' ';
				*mv << mob->ingameID << ' ';
				*mv << x << ' ';
				*mv << y << ' ';
				*mv << '5';

				mv->send(client);
			}

			mob->lastStatusChange = high_resolution_clock::now();
		}
		else if (mob->lastStatusChange + std::chrono::milliseconds((int)(rand() / float(RAND_MAX) * 2500 + 1500)) <= high_resolution_clock::now())
		{
			mob->status = MobStatus::IDLE;
		}
	}
}

void Map::addPlayer(Client* client)
{
	for (auto* mob : _mobs)
	{
		Packet* spawn = gFactory->make(PacketType::SERVER_GAME, client->session(), NString("in "));
		*spawn << mob->type << ' ';
		*spawn << mob->id << ' ';
		*spawn << mob->ingameID << ' ';
		*spawn << mob->x << ' ';
		*spawn << mob->y << " 2 ";
		*spawn << (int)(mob->hp * 100 / (float)mob->maxHp) << ' ';
		*spawn << (int)(mob->mp * 100 / (float)mob->maxHp) << " 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0";

		spawn->send(client);
	}

	std::lock_guard<std::mutex> lock(_playersMutex);
	_players.push_back(client);
	_needsUpdate = true;
}
