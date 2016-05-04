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
	_currentWork(MAKE_WORK(&WorldHandler::handleConnect)),
	_phase(CurrentPhase::SELECTION_SCREEN),
	_currentCharacter(nullptr)
{}

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
