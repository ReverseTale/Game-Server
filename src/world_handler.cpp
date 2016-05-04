#include "client.h"
#include "asyncwork.h"
#include "database.h"
#include "map_manager.h"
#include "map.h"
#include "world_handler.h"

#include <map>

#include <threadpool.h>
#include <Game/packet.h>
#include <threadpool11/threadpool11.hpp>

#include <boost/lockfree/queue.hpp>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

using namespace Net;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::open_document;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::finalize;


extern boost::lockfree::queue<AbstractWork*> asyncWork;


WorldHandler* WorldHandler::_instance = nullptr;


PacketHandler packetHandler[] = {
	{ "Char_NEW",	{ CurrentPhase::SELECTION_SCREEN,	MAKE_WORK(&WorldHandler::createCharacter) } },
	{ "Char_DEL",	{ CurrentPhase::SELECTION_SCREEN,	MAKE_WORK(&WorldHandler::deleteCharacter) } },
	{ "select",		{ CurrentPhase::SELECTION_SCREEN,	MAKE_WORK(&WorldHandler::gameStartInitialize) } },
	{ "game_start", { CurrentPhase::SELECTION_SCREEN,	MAKE_WORK(&WorldHandler::gameStartConfirmation) } },
	{ "lbs",		{ CurrentPhase::INGAME,				MAKE_WORK(&WorldHandler::receivedLBS) } },
	{ "npinfo",		{ CurrentPhase::INGAME,				MAKE_WORK(&WorldHandler::receivedNPINFO) } },
	{ "walk",		{ CurrentPhase::INGAME,				MAKE_WORK(&WorldHandler::processWalk) } },
	{ "say",		{ CurrentPhase::INGAME,				MAKE_WORK(&WorldHandler::chatMessage) } },

	{ "", { CurrentPhase::NONE, nullptr } }
};


WorldHandler::WorldHandler() :
	_currentFreeID(1)
{
	PacketHandler* currentHandler = &packetHandler[0];
	while (currentHandler->handle.worker != nullptr)
	{
		_packetHandler.emplace(currentHandler->opcode, &currentHandler->handle);
		++currentHandler;
	}
}

uint32_t WorldHandler::getFreeID()
{
	if (!_reusableIDs.empty())
	{
		uint32_t id = _reusableIDs.front();
		_reusableIDs.pop_front();
		return id;
	}

	uint32_t id = _currentFreeID + 1;
	if (id < _currentFreeID)
	{
		assert(false && "Run out of IDs");
	}

	++_currentFreeID;
	return id;
}

bool WorldHandler::workRouter(AbstractWork* work)
{
	// FIXME: Implement other packets!
	if (work->client()->_currentWork != nullptr)
	{
		return (WorldHandler::get()->*work->client()->_currentWork)(work);
	}

	auto self = get();
	auto it = self->_packetHandler.find(((ClientWork*)work)->packet().tokens().str(1));

	if (it != self->_packetHandler.end())
	{
		auto handler = it->second;
		if (handler->type == work->client()->_phase)
		{
			return (WorldHandler::get()->*handler->worker)(work);
		}
	}

	// TODO: Heartbeat and so on
	return true;
}

bool WorldHandler::handleConnect(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		work->client()->session()->setAlive(work->packet().tokens().from_int<uint32_t>(0) + 1);
		work->client()->session()->setID(work->packet().tokens().from_int<uint32_t>(1));

		work->client()->_currentWork = MAKE_WORK(&WorldHandler::handleUserCredentials);
		return true;
	}

	return false;
}

bool WorldHandler::handleUserCredentials(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		work->client()->_username = work->packet().tokens().str(1);
		work->client()->_currentWork = MAKE_WORK(&WorldHandler::handlePasswordCredentials);
		return true;
	}

	return false;
}

bool WorldHandler::handlePasswordCredentials(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		std::string username = work->client()->_username;
		std::string password = work->packet().tokens().str(1);
		auto id = work->client()->session()->id();

		std::cout << "User: " << username << " PASS: " << password << std::endl;

		auto future = gDB("login")->query<bool>([this, password, username, id](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "_id" << username << "password" << password << "session_id" << (uint16_t)id;

			return db["users"].count(filter_builder.view()) == 1;
		});

		asyncWork.push(new FutureWork<bool>(work->client(), MAKE_WORK(&WorldHandler::sendConnectionResult), std::move(future)));

		return true;
	}

	return false;
}

bool WorldHandler::sendConnectionResult(FutureWork<bool>* work)
{
	if (work->get())
	{
		auto client = work->client();
		auto future = gDB("characters")->query<bool>([this, client](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "user" << client->_username;

			client->_characters.clear();

			auto cursor = db["characters"].find(filter_builder.view());
			for (auto&& doc : cursor)
			{
				client->_characters.emplace_back(new Character{
					doc["slot"].get_int32(),
					doc["title"].get_utf8().value.to_string(),
					doc["_id"].get_utf8().value.to_string(),
					(Sex)(int)doc["sex"].get_int32(),
					doc["hair"].get_int32(),
					doc["color"].get_int32(),
					doc["level"].get_int32(),
					doc["hp"].get_int32(),
					doc["mp"].get_int32(),
					doc["exp"].get_int32(),
					{ // Profession
						doc["profession"]["level"].get_int32(),
						doc["profession"]["exp"].get_int32()
					}
				});

				std::cout << "New char: " << std::endl <<
					"\t" << client->_characters.back()->name << std::endl <<
					"\t" << client->_characters.back()->color << std::endl;
			}

			return true;
		});

		asyncWork.push(new FutureWork<bool>(client, MAKE_WORK(&WorldHandler::sendCharactersList), std::move(future)));
		client->_currentWork = nullptr;
		return true;
	}
	
	return false;
}

bool WorldHandler::sendCharactersList(FutureWork<bool>* work)
{
	if (work->get())
	{
		auto client = work->client();

		/*<< clist 0 Blipi 0 0 1 4 0 0 2 -1.12.1.8.-1.-1.-1.-1 1  1 1 -1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1 0 0*/
		Packet* clist_start = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("clist_start 0"));
		clist_start->send(client);

		// "clist 0 Blipi 0 0 1 4 0 0 2 -1.12.1.8.-1.10.-1.-1 1  1 1 -1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1 0 0"

		Packet* clist_all = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString());

		for (auto&& pj : client->_characters)
		{
			Packet* clist = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString());
			*clist << "clist " << (int)pj->slot << ' ';
			*clist << pj->name << " 0 ";
			*clist << (int)pj->sex << ' ';
			*clist << (int)pj->hair << ' ';
			*clist << (int)pj->color << ' ';
			*clist << "0 0 " << (int)pj->level << " 0 ";

			if (pj->slot == 0)
			{
				*clist << "-1.12.1.8.-1.-1.-1.-1 1 1  1 -1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1 0 0";
			}
			else
			{
				*clist << "-1.-1.-1.-1.-1.-1.-1.-1 1 1 1 -1 0";
			}

			std::cout << "Sending player " << pj->name << std::endl;
			std::cout << clist->data().get() << std::endl;

			clist_all = *clist_all + *clist;
		}

		Packet* clist_end = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("clist_end"));
		clist_all->send(client);
		clist_end->send(client);
		
		return true;
	}
	else
	{
		sendError(work->client(),  "User not found");
		return false;
	}
}

bool WorldHandler::createCharacter(ClientWork* work)
{
	if (work->packet().tokens().length() != 7)
	{
		return false;
	}

	auto name = work->packet().tokens().str(2);
	auto slot = work->packet().tokens().from_int<uint8_t>(3);
	auto sex = work->packet().tokens().from_int<uint8_t>(4);
	auto hair = work->packet().tokens().from_int<uint8_t>(5);
	auto color = work->packet().tokens().from_int<uint8_t>(6);

	if (name.length() <= 4 || slot > 2 || sex > 1 || hair > 1 || color > 9)
	{
		return false;
	}

	auto client = work->client();
	auto future = gDB("characters")->query<bool>([client, name, slot, sex, hair, color](mongocxx::database db) {
		auto character = document{} <<
			"_id" << name <<
			"slot" << slot <<
			"title" << "" <<
			"user" << client->_username <<
			"sex" << sex <<
			"hair" << hair <<
			"color" << color <<
			"level" << (int)1 <<
			finalize;

		try
		{
			db["characters"].insert_one(character.view());
		}
		catch (std::exception e)
		{
			return false;
		}

		return true;
	});

	asyncWork.push(new FutureWork<bool>(client, MAKE_WORK(&WorldHandler::sendConnectionResult), std::move(future)));

	return true;
}

bool WorldHandler::deleteCharacter(ClientWork* work)
{
	if (work->packet().tokens().length() == 4)
	{
		int slot = work->packet().tokens().from_int<int>(2);
		std::string password = work->packet().tokens().str(3);

		auto client = work->client();
		auto future = gDB("login")->query<int>([this, password, slot, client](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "_id" << client->_username << "password" << password;

			if (db["users"].count(filter_builder.view()) == 1)
			{
				return slot;
			}

			return -1;
		});

		asyncWork.push(new FutureWork<int>(client, MAKE_WORK(&WorldHandler::confirmDeleteCharacter), std::move(future)));
		return true;
	}

	return false;
}

bool WorldHandler::confirmDeleteCharacter(FutureWork<int>* work)
{
	int slot = work->get();

	if (slot >= 0)
	{
		auto client = work->client();
		auto future = gDB("characters")->query<bool>([this, slot, client](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "user" << client->_username << "slot" << slot;

			db["characters"].delete_one(filter_builder.view());
			return true;
		});

		asyncWork.push(new FutureWork<bool>(client, MAKE_WORK(&WorldHandler::sendConnectionResult), std::move(future)));
		return true;
	}
	
	// FIXME: Password was not correct, do not disconnect but send an error
	return false;
}

bool WorldHandler::gameStartInitialize(ClientWork* work)
{
	auto client = work->client();

	if (work->packet().tokens().length() == 3)
	{
		int slot = work->packet().tokens().from_int<int>(1);
		for (auto&& pj : client->_characters)
		{
			if (pj->slot == slot)
			{
				client->_currentCharacter = pj;

				// FIXME: HP/MP from DB?
				pj->hp = pj->maxHP;
				pj->mp = pj->maxMP;

				break;
			}
		}
	}

	if (client->_currentCharacter != nullptr)
	{
		Packet* packet = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("OK"));
		packet->send(client);

		return true;
	}

	return false;
}

bool WorldHandler::gameStartConfirmation(ClientWork* work)
{
	auto client = work->client();
	if (client->_currentCharacter != nullptr)
	{
		client->_phase = CurrentPhase::INGAME;
		return true;
	}
	return false;
}

bool WorldHandler::receivedLBS(ClientWork* work)
{
	auto client = work->client();
	Packet* packet = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("tit ") << client->_currentCharacter->title << " " << client->_currentCharacter->name);
	packet->send(client);

	// Fama difnidad
	packet = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("fd 0 1 0 1"));
	packet->send(client);

	client->_ingameID = getFreeID();
	printf("Client using ID: %d\n", client->_ingameID);
	client->sendCharacterInformation();


	// [POS.ID.CALIDAD.MEJORA.??]
	Packet* equip = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("equip 0 0 0.1.0.0.0 1.12.0.0.0 5.8.0.0.0"));
	equip->send(client);

	client->sendCharacterLevel();
	client->sendCharacterStatus();

	Packet* ski = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("ski 200 201 200 201 209"));
	ski->send(client);

	MapManager::get()->map(1)->addPlayer(work->client());
	client->sendCharacterPosition();
	client->sendCharacterMap();

	Packet* sc = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sc 0 0 30 38 30 4 70 1 0 32 34 41 2 70 0 17 34 19 34 17 0 0 0 0"));
	sc->send(client);

	Packet* cond = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("cond 1 ") << client->_ingameID << " 0 0 11");
	cond->send(client);

	Packet* pairy = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("pairy 1 ") << client->_ingameID << " 0 0 0 0");
	pairy->send(client);

	Packet* rsfi = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("rsfi 1 1 0 9 0 9"));
	rsfi->send(client);

	Packet* rank_cool = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("rank_cool 0 0 18000"));
	rank_cool->send(client);

	Packet* src = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("scr 0 0 0 0 0 0 0"));
	src->send(client);

	Packet* exts = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("exts 0 48 48 48"));
	exts->send(client);

	Packet* gidx = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("gidx 1 ") << client->_ingameID << " -1 - 0");
	gidx->send(client);

	Packet* mlinfo = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("mlinfo 3800 2000 100 0 0 10 0 Abcdefghijk"));
	mlinfo->send(client);

	Packet* p_clear = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("p_clear"));
	p_clear->send(client);

	Packet* sc_p_stc = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sc_p_stc 0"));
	sc_p_stc->send(client);

	Packet* p_init = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("p_init 0"));
	p_init->send(client);

	Packet* zzim = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("zzim"));
	zzim->send(client);

	// 1 = Slot + 1?
	Packet* twk = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("twk 2 ") << client->_ingameID << ' ' << client->_username << ' ' << client->_currentCharacter->name << " shtmxpdlfeoqkr");
	twk->send(client);
	
	return true;
}

bool WorldHandler::receivedNPINFO(ClientWork* work)
{
	auto client = work->client();

	// Acto
	Packet* script = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("script 1 27"));
	script->send(client);

	Packet* qstlist = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("qstlist 5.1997.1997.19.0.0.0.0.0.0.0.0.0.0.0.0"));
	qstlist->send(client);

	Packet* target = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("target 57 149 1 1997"));
	target->send(client);

	Packet* sqst0 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  0 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst0->send(client);
		
	Packet* sqst1 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  1 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst1->send(client);

	Packet* sqst2 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  2 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst2->send(client);

	Packet* sqst3 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  3 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst3->send(client);

	Packet* sqst4 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  4 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst4->send(client);

	Packet* sqst5 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  5 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst5->send(client);

	Packet* sqst6 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("sqst  6 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst6->send(client);

	Packet* fs = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("fs 0"));
	fs->send(client);

	Packet* inv0 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 0"));
	inv0->send(client);

	Packet* inv1 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 1"));
	inv1->send(client);

	Packet* inv2 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 2 0.2024.10 1.2081.1"));
	inv2->send(client);

	Packet* inv3 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 3"));
	inv3->send(client);

	Packet* mlobjlst = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("mlobjlst"));
	mlobjlst->send(client);

	Packet* inv6 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 6"));
	inv6->send(client);

	Packet* inv7 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("inv 7"));
	inv7->send(client);

	// gold [cantidad] ?
	Packet* gold = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("gold 0 0"));
	gold->send(client);

	Packet* qslot0 = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("qslot 0 1.1.1 0.2.0 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 1.1.16 1.3.1"));
	qslot0->send(client);

	for (int i = 1; i <= 9; ++i)
	{
		Packet* qslot = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("qslot ") << i << " 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1");
		qslot->send(client);
	}

	Packet* p_clear = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("p_clear"));
	p_clear->send(client);

	char* ins[] = {
		
		"in 2 1431 1908 102 70 5 100 100 462 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1908 1 0 48 Gestor de Familia",
		"in 2 915 1907 90 45 0 100 100 54 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1907 1 0 0 Puntuaciones máximas",
		"in 2 905 1906 156 5 2 100 100 6567 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1906 1 0 0 Lista de personas buscadas",
		"in 2 331 1905 59 86 2 100 100 306 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1905 1 0 0 El Vagabundo Sam",
		"in 2 955 1904 67 56 2 100 100 439 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1904 1 0 0 Teletransporte",
		"in 2 330 1903 50 65 0 100 100 11 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1903 1 0 0 Espiral del tiempo",
		"in 2 328 1902 23 65 1 100 100 2 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1902 1 0 4 Juego de azar",
		"in 2 305 1901 20 108 1 100 100 6 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1901 1 0 7 Tienda de alquimia",
		"in 2 303 1900 24 117 5 100 100 10 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1900 1 0 5 Tienda de moda",
		"in 2 304 1899 23 100 1 100 100 9 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1899 1 0 6 Mercado de NosVille",
		"in 2 300 1898 96 65 0 100 100 3 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1898 1 4 1 Información de NosVille",
		"in 2 302 1897 100 47 7 100 100 4 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1897 1 1 2 Maestro de técnicas",
		"in 2 301 1896 30 71 0 100 100 1 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"shop 2 1896 1 0 3 Tienda de armas",
		"in 2 939 1895 81 58 2 100 100 1 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 379 1894 113 171 0 100 100 99 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 351 1893 78 42 5 100 100 99 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 350 1892 85 41 0 100 100 99 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1891 154 22 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1890 156 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1889 150 25 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1888 150 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1887 143 25 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		//"in 2 334 1886 135 117 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 089 ëëëëëë",
		"in 2 334 1885 135 100 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1884 135 71 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1883 127 45 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1882 125 37 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1881 115 153 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1880 116 75 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1879 108 146 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1878 97 56 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1877 78 56 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1876 78 145 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1875 68 138 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1874 65 64 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1873 50 58 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1872 48 121 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1871 50 109 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1870 38 99 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1869 40 59 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1868 30 116 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 334 1867 29 110 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1866 47 43 4 100 100 301 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1865 117 30 0 100 100 303 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1864 63 152 7 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1863 51 145 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1862 36 131 7 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 322 1861 28 129 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 321 1860 123 171 4 100 100 305 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 320 1859 39 44 4 100 100 302 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 320 1858 109 34 0 100 100 300 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 2 2540 1857 82 116 0 100 100 9714 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 9 1046 172053 69 6 16 0 0 171360"
	};

	for (char* msg : ins)
	{
		Packet* spawn = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString(msg));
		spawn->send(client);
	}

	return true;
}


bool WorldHandler::processWalk(ClientWork* work)
{
	// 213 walk 54 135 0 11
	// cond 1 171370 0 0 11
	if (work->packet().tokens().length() == 6)
	{
		int x = work->packet().tokens().from_int<int>(2);
		int y = work->packet().tokens().from_int<int>(3);
		int speed = work->packet().tokens().from_int<int>(5);

		auto client = work->client();

		// TODO: Pathfinding
		client->pos().x = x;
		client->pos().y = y;

		Packet* cond = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("cond 1 ") << client->_ingameID << " 0 0 " << speed);
		cond->send(client);

		Packet* mv = gFactory->make(PacketType::SERVER_GAME, nullptr, NString("mv 1 ") << client->_ingameID << ' ' << x << ' ' << y << ' ' << speed);
		std::cout << "Broadcasting: " << mv->data().get() << std::endl;
		client->getMap()->broadcastPacket(mv);
	}

	return true;
}

bool WorldHandler::chatMessage(ClientWork* work)
{
	// 213 say !
	if (work->packet().tokens()[2][0] == '!')
	{
		std::string msg = work->packet().tokens().str(2);
		for (int i = 3; i < work->packet().tokens().length(); ++i)
		{
			msg += ' ' + work->packet().tokens().str(i);
		}

		auto client = work->client();
		Packet* packet = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString(msg.substr(1)));
		packet->send(client);
	}

	return true;
}

void WorldHandler::sendError(Client* client, std::string&& error)
{
	Packet* errorPacket = gFactory->make(PacketType::SERVER_GAME, &client->_session, NString("fail ") << error);
	errorPacket->send(client);
}
