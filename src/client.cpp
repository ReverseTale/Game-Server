#include "client.h"
#include "asyncwork.h"
#include "database.h"

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

struct PacketHandler
{
	std::string opcode;
	CurrentPhase type;
	WorkType worker;
};

PacketHandler packetHandler[] = {
	{ "Char_NEW",	CurrentPhase::SELECTION_SCREEN, MAKE_WORK(&Client::createCharacter) },
	{ "Char_DEL",	CurrentPhase::SELECTION_SCREEN, MAKE_WORK(&Client::deleteCharacter) },
	{ "select",		CurrentPhase::SELECTION_SCREEN, MAKE_WORK(&Client::gameStartInitialize) },
	{ "game_start", CurrentPhase::SELECTION_SCREEN, MAKE_WORK(&Client::gameStartConfirmation) },
	{ "lbs",		CurrentPhase::INGAME,			MAKE_WORK(&Client::receivedLBS) },
	{ "npinfo",		CurrentPhase::INGAME,			MAKE_WORK(&Client::receivedNPINFO) },
	{ "walk",		CurrentPhase::INGAME,			MAKE_WORK(&Client::processWalk) },

	{ "", CurrentPhase::NONE, nullptr }
};


Client::Client() :
	_currentWork(MAKE_WORK(&Client::handleConnect)),
	_phase(CurrentPhase::SELECTION_SCREEN),
	_currentCharacter(nullptr)
{}

bool Client::workRouter(AbstractWork* work)
{
	// FIXME: Implement other packets!
	if (_currentWork != nullptr)
	{
		return (this->*_currentWork)(work);
	}

	PacketHandler* currentHandler = &packetHandler[0];
	while (currentHandler->worker != nullptr)
	{
		if (currentHandler->opcode == ((ClientWork*)work)->packet().tokens()[1])
		{
			if (currentHandler->type == _phase)
			{
				return (this->*currentHandler->worker)(work);
			}
		}

		++currentHandler;
	}

	return true;
}

bool Client::handleConnect(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		_session.setAlive(work->packet().tokens().from_int<uint32_t>(0) + 1);
		_session.setID(work->packet().tokens().from_int<uint32_t>(1));

		_currentWork = MAKE_WORK(&Client::handleUserCredentials);

		return true;
	}

	return false;
}

bool Client::handleUserCredentials(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		_username = work->packet().tokens().str(1);
		_currentWork = MAKE_WORK(&Client::handlePasswordCredentials);
		return true;
	}

	return false;
}

bool Client::handlePasswordCredentials(ClientWork* work)
{
	if (work->packet().tokens().length() == 2)
	{
		std::string password = work->packet().tokens().str(1);

		std::cout << "User: " << _username << " PASS: " << password << std::endl;

		auto future = gDB("login")->query<bool>([this, password](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "_id" << _username << "password" << password << "session_id" << (uint16_t)_session.id();

			return db["users"].count(filter_builder.view()) == 1;
		});

		asyncWork.push(new FutureWork<bool>(this, MAKE_WORK(&Client::sendConnectionResult), std::move(future)));

		return true;
	}

	return false;
}

bool Client::sendConnectionResult(FutureWork<bool>* work)
{
	if (work->get())
	{
		auto future = gDB("characters")->query<bool>([this](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "user" << _username;

			_characters.clear();

			auto cursor = db["characters"].find(filter_builder.view());
			for (auto&& doc : cursor)
			{
				_characters.emplace_back(new Character{
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
					"\t" << _characters.back()->name << std::endl <<
					"\t" << _characters.back()->color << std::endl;
			}

			return true;
		});

		asyncWork.push(new FutureWork<bool>(this, MAKE_WORK(&Client::sendCharactersList), std::move(future)));
		_currentWork = nullptr;
		return true;
	}
	
	return false;
}

bool Client::sendCharactersList(FutureWork<bool>* work)
{
	if (work->get())
	{
		/*<< clist 0 Blipi 0 0 1 4 0 0 2 -1.12.1.8.-1.-1.-1.-1 1  1 1 -1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1 0 0*/
		Packet* clist_start = gFactory->make(PacketType::SERVER_GAME, &_session, NString("clist_start 0"));
		clist_start->send(this);

		// "clist 0 Blipi 0 0 1 4 0 0 2 -1.12.1.8.-1.10.-1.-1 1  1 1 -1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1.-1 0 0"

		Packet* clist_all = gFactory->make(PacketType::SERVER_GAME, &_session, NString());

		for (auto&& pj : _characters)
		{
			Packet* clist = gFactory->make(PacketType::SERVER_GAME, &_session, NString());
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

		Packet* clist_end = gFactory->make(PacketType::SERVER_GAME, &_session, NString("clist_end"));
		clist_all->send(this);
		clist_end->send(this);
		
		return true;
	}
	else
	{
		sendError("User not found");
		return false;
	}
}

bool Client::createCharacter(ClientWork* work)
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

	auto future = gDB("characters")->query<bool>([this, name, slot, sex, hair, color](mongocxx::database db) {
		auto character = document{} <<
			"_id" << name <<
			"slot" << slot <<
			"title" << "" <<
			"user" << _username <<
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

	asyncWork.push(new FutureWork<bool>(this, MAKE_WORK(&Client::sendConnectionResult), std::move(future)));

	return true;
}

bool Client::deleteCharacter(ClientWork* work)
{
	if (work->packet().tokens().length() == 4)
	{
		int slot = work->packet().tokens().from_int<int>(2);
		std::string password = work->packet().tokens().str(3);

		auto future = gDB("login")->query<int>([this, password, slot](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "_id" << _username << "password" << password;

			if (db["users"].count(filter_builder.view()) == 1)
			{
				return slot;
			}

			return -1;
		});

		asyncWork.push(new FutureWork<int>(this, MAKE_WORK(&Client::confirmDeleteCharacter), std::move(future)));
		return true;
	}

	return false;
}

bool Client::confirmDeleteCharacter(FutureWork<int>* work)
{
	int slot = work->get();

	if (slot >= 0)
	{
		auto future = gDB("characters")->query<bool>([this, slot](mongocxx::database db) {
			bsoncxx::builder::stream::document filter_builder;
			filter_builder << "user" << _username << "slot" << slot;

			db["characters"].delete_one(filter_builder.view());
			return true;
		});

		asyncWork.push(new FutureWork<bool>(this, MAKE_WORK(&Client::sendConnectionResult), std::move(future)));
		return true;
	}
	
	// FIXME: Password was not correct, do not disconnect but send an error
	return false;
}

bool Client::gameStartInitialize(ClientWork* work)
{
	if (work->packet().tokens().length() == 3)
	{
		int slot = work->packet().tokens().from_int<int>(1);
		for (auto&& pj : _characters)
		{
			if (pj->slot == slot)
			{
				_currentCharacter = pj;
				break;
			}
		}
	}

	if (_currentCharacter != nullptr)
	{
		Packet* packet = gFactory->make(PacketType::SERVER_GAME, &_session, NString("OK"));
		packet->send(this);

		return true;
	}

	return false;
}

bool Client::gameStartConfirmation(ClientWork* work)
{
	if (_currentCharacter != nullptr)
	{
		_phase = CurrentPhase::INGAME;
		return true;
	}
	return false;
}

bool Client::receivedLBS(ClientWork* work)
{
	Packet* packet = gFactory->make(PacketType::SERVER_GAME, &_session, NString("tit ") << _currentCharacter->title << " " << _currentCharacter->name);
	packet->send(this);

	packet = gFactory->make(PacketType::SERVER_GAME, &_session, NString("fd 0 1 0 1"));
	packet->send(this);

	_ingameID = 123;

	//c_info[Nombre] - -1 -1 - [INGAME_ID][SLOT][SEXO][PELO][COLOR] 0[LVL] 0 0 0 0 0 0
	Packet* cinfo = gFactory->make(PacketType::SERVER_GAME, &_session, NString("c_info "));
	*cinfo << _currentCharacter->name << ' ';
	*cinfo << "- -1 -1 - ";
	*cinfo << _ingameID << ' ';
	*cinfo << (int)_currentCharacter->slot << ' ';
	*cinfo << (int)_currentCharacter->sex << ' ';
	*cinfo << (int)_currentCharacter->hair << ' ';
	*cinfo << (int)_currentCharacter->color << ' ';
	*cinfo << "0 " << (int)_currentCharacter->level << " 0 0 0 0 0 0";
	cinfo->send(this);

	Packet* equip = gFactory->make(PacketType::SERVER_GAME, &_session, NString("equip 0 0 0.1.0.0.0 1.12.0.0.0 5.8.0.0.0"));
	equip->send(this);

	// lev[LVL][XP][LVL_PROF][XP_PROF][XP_NEXT_LEVEL][XP_NEXT_PROF] 0[NEXT_LVL]
	Packet* lev = gFactory->make(PacketType::SERVER_GAME, &_session, NString("lev "));
	*lev << (int)_currentCharacter->level << ' ' << _currentCharacter->experience << ' ';
	*lev << (int)_currentCharacter->profession.level << ' ' << _currentCharacter->profession.experience << ' ';
	*lev << 300 << ' ' << 500 << ' ';
	*lev << 0 << ' ' << (int)(_currentCharacter->level + 1);
	lev->send(this);

	Packet* stat = gFactory->make(PacketType::SERVER_GAME, &_session, NString("stat "));
	*stat << _currentCharacter->maxHP << ' ' << _currentCharacter->maxHP << ' ';
	*stat << _currentCharacter->maxMP << ' ' << _currentCharacter->maxMP << ' ';
	*stat << 0 << ' ' << 1024;
	stat->send(this);

	Packet* ski = gFactory->make(PacketType::SERVER_GAME, &_session, NString("ski 200 201 200 201 209"));
	ski->send(this);

	Packet* at = gFactory->make(PacketType::SERVER_GAME, &_session, NString("at ") << _ingameID << " 1 52 135 2 0 0 1 -1");
	at->send(this);

	Packet* cmap = gFactory->make(PacketType::SERVER_GAME, &_session, NString("cmap 0 1 1"));
	cmap->send(this);

	Packet* sc = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sc 0 0 30 38 30 4 70 1 0 32 34 41 2 70 0 17 34 19 34 17 0 0 0 0"));
	sc->send(this);

	Packet* cond = gFactory->make(PacketType::SERVER_GAME, &_session, NString("cond 1 ") << _ingameID << " 0 0 11");
	cond->send(this);

	Packet* pairy = gFactory->make(PacketType::SERVER_GAME, &_session, NString("pairy 1 ") << _ingameID << " 0 0 0 0");
	pairy->send(this);

	Packet* rsfi = gFactory->make(PacketType::SERVER_GAME, &_session, NString("rsfi 1 1 0 9 0 9"));
	rsfi->send(this);

	Packet* rank_cool = gFactory->make(PacketType::SERVER_GAME, &_session, NString("rank_cool 0 0 18000"));
	rank_cool->send(this);

	Packet* src = gFactory->make(PacketType::SERVER_GAME, &_session, NString("scr 0 0 0 0 0 0 0"));
	src->send(this);

	Packet* exts = gFactory->make(PacketType::SERVER_GAME, &_session, NString("exts 0 48 48 48"));
	exts->send(this);

	Packet* gidx = gFactory->make(PacketType::SERVER_GAME, &_session, NString("gidx 1 ") << _ingameID << " -1 - 0");
	gidx->send(this);

	Packet* mlinfo = gFactory->make(PacketType::SERVER_GAME, &_session, NString("mlinfo 3800 2000 100 0 0 10 0 Abcdefghijk"));
	mlinfo->send(this);

	Packet* p_clear = gFactory->make(PacketType::SERVER_GAME, &_session, NString("p_clear"));
	p_clear->send(this);

	Packet* sc_p_stc = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sc_p_stc 0"));
	sc_p_stc->send(this);

	Packet* p_init = gFactory->make(PacketType::SERVER_GAME, &_session, NString("p_init 0"));
	p_init->send(this);

	Packet* zzim = gFactory->make(PacketType::SERVER_GAME, &_session, NString("zzim"));
	zzim->send(this);

	// 1 = Slot + 1?
	Packet* twk = gFactory->make(PacketType::SERVER_GAME, &_session, NString("twk 2 ") << _ingameID << ' ' << _username << ' ' << _currentCharacter->name << " shtmxpdlfeoqkr");
	twk->send(this);
	
	return true;
}

bool Client::receivedNPINFO(ClientWork* work)
{
	Packet* script = gFactory->make(PacketType::SERVER_GAME, &_session, NString("script 1 27"));
	script->send(this);

	Packet* qstlist = gFactory->make(PacketType::SERVER_GAME, &_session, NString("qstlist 5.1997.1997.19.0.0.0.0.0.0.0.0.0.0.0.0"));
	qstlist->send(this);

	Packet* target = gFactory->make(PacketType::SERVER_GAME, &_session, NString("target 57 149 1 1997"));
	target->send(this);

	Packet* sqst0 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  0 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst0->send(this);
		
	Packet* sqst1 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  1 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst1->send(this);

	Packet* sqst2 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  2 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst2->send(this);

	Packet* sqst3 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  3 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst3->send(this);

	Packet* sqst4 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  4 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst4->send(this);

	Packet* sqst5 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  5 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst5->send(this);

	Packet* sqst6 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("sqst  6 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"));
	sqst6->send(this);

	Packet* fs = gFactory->make(PacketType::SERVER_GAME, &_session, NString("fs 0"));
	fs->send(this);

	Packet* inv0 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 0"));
	inv0->send(this);

	Packet* inv1 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 1"));
	inv1->send(this);

	Packet* inv2 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 2 0.2024.10 1.2081.1"));
	inv2->send(this);

	Packet* inv3 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 3"));
	inv3->send(this);

	Packet* mlobjlst = gFactory->make(PacketType::SERVER_GAME, &_session, NString("mlobjlst"));
	mlobjlst->send(this);

	Packet* inv6 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 6"));
	inv6->send(this);

	Packet* inv7 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("inv 7"));
	inv7->send(this);

	Packet* gold = gFactory->make(PacketType::SERVER_GAME, &_session, NString("gold 0 0"));
	gold->send(this);

	Packet* qslot0 = gFactory->make(PacketType::SERVER_GAME, &_session, NString("qslot 0 1.1.1 0.2.0 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 1.1.16 1.3.1"));
	qslot0->send(this);

	for (int i = 1; i <= 9; ++i)
	{
		Packet* qslot = gFactory->make(PacketType::SERVER_GAME, &_session, NString("qslot ") << i << " 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1 7.7.-1");
		qslot->send(this);
	}

	Packet* p_clear = gFactory->make(PacketType::SERVER_GAME, &_session, NString("p_clear"));
	p_clear->send(this);

	char* ins[] = {
		"in 3 333 1856 51 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1855 51 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1854 49 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1853 48 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1852 40 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1851 42 161 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1850 40 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1849 38 167 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1848 30 164 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1847 29 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1846 26 152 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1845 28 153 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1844 22 167 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1843 23 166 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1842 24 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1841 26 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1840 20 156 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1839 22 159 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1838 19 161 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1837 17 160 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1836 18 145 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1835 18 147 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1834 17 154 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1833 16 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1832 15 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1831 18 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1830 14 152 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 333 1829 16 154 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1828 133 5 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1827 131 4 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1826 129 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1825 129 4 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1824 124 12 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1823 126 11 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1822 122 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1821 125 11 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1820 121 9 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1819 122 11 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1818 121 8 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1817 121 9 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1816 118 3 2 14 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1815 118 4 2 33 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1814 118 2 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1813 118 2 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1812 113 7 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1811 114 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1810 113 11 2 85 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1809 112 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1808 108 5 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1807 111 7 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1806 109 13 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1805 108 11 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1804 100 3 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1803 101 4 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1802 97 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1801 97 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1800 70 13 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1799 69 14 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1798 69 5 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1797 71 9 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1796 66 3 2 43 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1795 66 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1794 59 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1793 61 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1792 55 8 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1791 56 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1790 48 13 2 87 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1789 46 11 2 34 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1788 41 8 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1787 41 8 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1786 31 7 2 87 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1785 27 5 2 14 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1784 21 5 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1783 24 4 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1782 23 8 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1781 23 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1780 16 18 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1779 15 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1778 15 12 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1777 12 10 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1776 14 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1775 12 4 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1774 10 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1773 9 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1772 8 5 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1771 6 6 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1770 5 13 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 30 1769 4 14 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1768 104 24 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1767 104 25 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1766 102 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1765 102 21 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1764 102 14 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1763 101 18 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1762 95 22 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1761 92 21 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1760 93 17 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1759 92 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1758 69 30 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1757 69 30 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1756 68 24 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1755 69 24 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1754 68 37 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1753 69 37 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1752 67 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1751 64 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1750 63 29 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1749 61 31 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1748 58 35 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1747 59 35 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1746 61 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1745 61 17 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1744 50 18 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1743 53 16 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1742 45 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1741 46 18 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1740 44 22 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1739 42 23 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1738 40 17 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1737 40 17 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1736 32 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1735 36 19 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1734 29 30 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1733 29 30 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1732 27 21 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1731 28 22 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1730 20 21 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1729 21 18 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1728 19 28 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 25 1727 19 29 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1726 52 162 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1725 53 161 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1724 51 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1723 53 158 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1722 49 151 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1721 51 152 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1720 43 167 2 12 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1719 46 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1718 45 167 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1717 46 166 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1716 46 162 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1715 44 162 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1714 44 155 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1713 42 154 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1712 34 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1711 34 162 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1710 34 151 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1709 32 152 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1708 32 141 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1707 33 141 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1706 30 139 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1705 33 141 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1704 30 144 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1703 30 145 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1702 29 137 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1701 29 139 2 39 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1700 30 167 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1699 27 165 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1698 25 159 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1697 25 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1696 24 144 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1695 23 141 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1694 25 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1693 24 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1692 22 164 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1691 22 164 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1690 20 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1689 18 164 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1688 20 168 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1687 20 169 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1686 15 158 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1685 17 159 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1684 14 147 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1683 13 147 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1682 14 157 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
		"in 3 24 1681 11 154 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0",
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
		Packet* spawn = gFactory->make(PacketType::SERVER_GAME, &_session, NString(msg));
		spawn->send(this);
	}
		

	return true;
}


bool Client::processWalk(ClientWork* work)
{
	// 213 walk 54 135 0 11
	// cond 1 171370 0 0 11
	if (work->packet().tokens().length() == 6)
	{
		Packet* cond = gFactory->make(PacketType::SERVER_GAME, &_session, NString("cond 1 ") << _ingameID << " 0 0 11");
		cond->send(this);
	}

	return true;
}

void Client::sendError(std::string&& error)
{
	Packet* errorPacket = gFactory->make(PacketType::SERVER_GAME, &_session, NString("fail ") << error);
	errorPacket->send(this);
}


void Client::onRead(NString packet)
{
	Packet* loginPacket = gFactory->make(PacketType::SERVER_GAME, &_session, packet);
	auto packets = loginPacket->decrypt();
	for (auto data : packets)
	{
		std::cout << ">> " << data.get() << std::endl;
		asyncWork.push(new ClientWork(this, MAKE_WORK(&Client::workRouter), data));
	}
}
