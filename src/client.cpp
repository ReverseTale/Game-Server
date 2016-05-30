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


Client::Client() :
	Entity{1},
	_currentWork(MAKE_WORK(&WorldHandler::handleConnect)),
	_phase(CurrentPhase::SELECTION_SCREEN),
	_currentCharacter(nullptr),
	_currentMap(nullptr)
{
	_position.x = 52;
	_position.y = 135;
}

void Client::sendCharacterInformation()
{
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
}

void Client::sendCharacterLevel()
{
	Packet* lev = gFactory->make(PacketType::SERVER_GAME, &_session, NString("lev "));
	*lev << (int)_currentCharacter->level << ' ' << _currentCharacter->experience << ' ';
	*lev << (int)_currentCharacter->profession.level << ' ' << _currentCharacter->profession.experience << ' ';
	*lev << 300 << ' ' << 500 << ' ';
	*lev << 0 << ' ' << (int)(_currentCharacter->level + 1);
	*lev << " 0 0 3";
	lev->send(this);
}

void Client::sendCharacterStatus()
{
	Packet* stat = gFactory->make(PacketType::SERVER_GAME, &_session, NString("stat "));
	*stat << _currentCharacter->maxHP << ' ' << _currentCharacter->maxHP << ' ';
	*stat << _currentCharacter->maxMP << ' ' << _currentCharacter->maxMP << ' ';
	*stat << 0 << ' ' << 1024;
	stat->send(this);
}

void Client::sendCharacterPosition()
{
	Packet* at = gFactory->make(PacketType::SERVER_GAME, &_session, NString("at "));
	*at << _ingameID << ' ';
	*at << _currentMap->id() << ' ';
	*at << _position.x << ' ';
	*at << _position.y << ' ';
	*at << "2 0 0 1 -1";
	at->send(this);
}

void Client::sendCharacterMap()
{
	Packet* cmap = gFactory->make(PacketType::SERVER_GAME, &_session, NString("c_map 0 "));
	*cmap << _currentMap->id() << " 1";
	cmap->send(this);
}

NString Client::getSpawnPacket()
{
	if (_mustRegenSpawn)
	{
		_mustRegenSpawn = false;

		_spawnPacket = Entity::getSpawnPacket();
		_spawnPacket << pj()->name << " - ";
		_spawnPacket << id() << ' ';
		_spawnPacket << pos().x << ' ';
		_spawnPacket << pos().y << ' ';
		_spawnPacket << "2 0 ";
		_spawnPacket << (int)pj()->sex << ' ';
		_spawnPacket << (int)pj()->hair << ' ';
		_spawnPacket << (int)pj()->color << ' ';
		_spawnPacket << "0 ";
		_spawnPacket << "202.13.3.8.-1.-1.-1.-1 ";
		_spawnPacket << pj()->hpPercent() << ' ';
		_spawnPacket << pj()->mpPercent() << ' ';
		_spawnPacket << "0 -1 ";
		_spawnPacket << "0 0 0 0 0 0 1 1 -1 - 2 0 0 0 0 ";
		_spawnPacket << (int)pj()->level << ' ';
		_spawnPacket << "0 0 0 10 0";
	}

	return _spawnPacket;
}

void Client::onRead(NString packet)
{
	Packet* loginPacket = gFactory->make(PacketType::SERVER_GAME, &_session, packet);
	auto packets = loginPacket->decrypt();
	for (auto data : packets)
	{
		std::cout << ">> " << data.get() << std::endl;
		asyncWork.push(new ClientWork(this, MAKE_WORK(&WorldHandler::workRouter), data));
	}
}
