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
	WorkType worker;
};

PacketHandler packetHandler[] = {
	{"Char_NEW", MAKE_WORK(&Client::createCharacter) },
	{"Char_DEL", MAKE_WORK(&Client::deleteCharacter) },
	{"", nullptr}
};

Client::Client() :
	_currentWork(MAKE_WORK(&Client::handleConnect))
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
			return (this->*currentHandler->worker)(work);
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
				_characters.emplace_back(new Character{ doc["slot"].get_int32(), doc["_id"].get_utf8().value.to_string(), (Sex)(int)doc["sex"].get_int32(), doc["hair"].get_int32(), doc["color"].get_int32(), doc["level"].get_int32() });

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
			*clist << "0 0 " << (int)pj->level << ' ';

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
