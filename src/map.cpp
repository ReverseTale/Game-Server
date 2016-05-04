#include "map.h"
#include "client.h"
#include "database.h"
#include "world_handler.h"

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

			Packet* mv = gFactory->make(PacketType::SERVER_GAME, nullptr, NString("mv "));
			*mv << mob->type << ' ';
			*mv << mob->ingameID << ' ';
			*mv << x << ' ';
			*mv << y << ' ';
			*mv << '5';
			broadcastPacket(mv);

			mob->lastStatusChange = high_resolution_clock::now();
		}
		else if (mob->lastStatusChange + std::chrono::milliseconds((int)(rand() / float(RAND_MAX) * 2500 + 1500)) <= high_resolution_clock::now())
		{
			mob->status = MobStatus::IDLE;
		}
	}

	while (!_broadcastList.empty())
	{
		Packet* packet = _broadcastList.front();
		_broadcastList.pop_front();

		for (auto* client : _players)
		{
			Packet* broadcastPacket = packet->clone(client->session());
			packet->send(client);
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

	// TODO: Broadcast player spawn
	// in 1 [name] - [ingameID] [x] [y] [orientacion] 0 [sexo] [pelo] [color] [clase] [gorro].[cuello].[arma].?.[boca].?.[cuello2?].-1 [hp%] [mp%] 0 -1 [posicion-bicho-volador] [atributo-pj] 0 0 0 0 1 1 [familia,-1=no] [nombre,-=ninguno] [1=principiante,13=experto...] 0(1=desaparece) 0 0 0 [lvl] [brillo-familia?] 0(1=aura?) 0(1=aura?) [size,10=normal] 0
	client->setMap(this);

	Packet* spawn = gFactory->make(PacketType::SERVER_GAME, client->session(), NString("in 1 "));
	*spawn << client->pj()->name << " - ";
	*spawn << client->id() << ' ';
	*spawn << client->pos().x << ' ';
	*spawn << client->pos().y << ' ';
	*spawn << "2 0 ";
	*spawn << (int)client->pj()->sex << ' ';
	*spawn << (int)client->pj()->hair << ' ';
	*spawn << (int)client->pj()->color << ' ';
	*spawn << "0 ";
	*spawn << "202.13.3.8.-1.-1.-1.-1 ";
	*spawn << client->pj()->hpPercent() << ' ';
	*spawn << client->pj()->mpPercent() << ' ';
	*spawn << "0 -1 ";
	*spawn << "0 0 0 0 0 0 1 1 -1 - 2 0 0 0 0 ";
	*spawn << (int)client->pj()->level << ' ';
	*spawn << "0 0 0 10 0";

	std::cout << "Sending: " << spawn->data().get() << "::::" << std::endl;

	broadcastPacket(spawn);

	std::lock_guard<std::mutex> lock(_playersMutex);
	
	for (auto* client : _players)
	{
		spawn = gFactory->make(PacketType::SERVER_GAME, client->session(), NString("in 1 "));
		*spawn << client->pj()->name << " - ";
		*spawn << client->id() << ' ';
		*spawn << client->pos().x << ' ';
		*spawn << client->pos().y << ' ';
		*spawn << "2 0 ";
		*spawn << (int)client->pj()->sex << ' ';
		*spawn << (int)client->pj()->hair << ' ';
		*spawn << (int)client->pj()->color << ' ';
		*spawn << "0 ";
		*spawn << "202.13.3.8.-1.-1.-1.-1 ";
		*spawn << client->pj()->hpPercent() << ' ';
		*spawn << client->pj()->mpPercent() << ' ';
		*spawn << "0 -1 ";
		*spawn << "0 0 0 0 0 0 1 1 -1 - 2 0 0 0 0 ";
		*spawn << (int)client->pj()->level << ' ';
		*spawn << "0 0 0 10 0";
		broadcastPacket(spawn);

		spawn->send(client);
	}

	_players.push_back(client);
	_needsUpdate = true;
}

void Map::broadcastPacket(Packet* packet)
{
	_broadcastList.emplace_back(packet);
}
