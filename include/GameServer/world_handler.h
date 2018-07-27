#pragma once

#include <string>

#include "GameServer/asyncwork.h"

#include <Tools/utils.h>
#include <Tools/accepted_socket.h>


#ifdef max
	#undef max
	#undef min
#endif

struct ClientWork;
template <typename T>
struct FutureWork;

struct PacketHandler
{
	std::string opcode;
	struct Handle
	{
		CurrentPhase type;
		WorkType worker;
	} handle;
};

class WorldHandler
{
public:
	static WorldHandler* get()
	{
		if (!_instance)
		{
			_instance = new WorldHandler();
		}

		return _instance;
	}
	
public:
	uint32_t getFreeID();

	bool workRouter(AbstractWork* work);

	bool handleConnect(ClientWork* work);
	bool handleUserCredentials(ClientWork* work);
	bool handlePasswordCredentials(ClientWork* work);
	bool sendConnectionResult(FutureWork<bool>* work);
	bool sendCharactersList(FutureWork<bool>* work);

	bool createCharacter(ClientWork* work);
	bool deleteCharacter(ClientWork* work);
	bool confirmDeleteCharacter(FutureWork<int>* work);

	bool gameStartInitialize(ClientWork* work);
	bool gameStartConfirmation(ClientWork* work);
	bool receivedLBS(ClientWork* work);
	bool receivedNPINFO(ClientWork* work);

	bool processWalk(ClientWork* work);
	bool chatMessage(ClientWork* work);

	bool removeItem(ClientWork* work);
	bool equipItem(ClientWork* work);
	bool moveItem(ClientWork* work);

	void sendError(Client* client, std::string&& error);

private:
	WorldHandler();

private:
	static WorldHandler* _instance;
	std::map<std::string, PacketHandler::Handle*> _packetHandler;

	uint32_t _currentFreeID;
	std::list<uint32_t> _reusableIDs;
};
