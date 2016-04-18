#pragma once

#include <string>

#include <asyncwork.h>
#include <Tools/utils.h>
#include <Tools/accepted_socket.h>


#ifdef max
	#undef max
	#undef min
#endif

struct ClientWork;
template <typename T>
struct FutureWork;

enum class Sex
{
	MALE,
	FEMALE
};

enum class CurrentPhase
{
	NONE,
	SELECTION_SCREEN,
	INGAME
};


struct Character
{
	uint8_t slot;
	std::string title;
	std::string name;
	Sex sex;
	uint8_t hair;
	uint8_t color;
	uint8_t level;

	int maxHP;
	int maxMP;
	int experience;

	struct Profession
	{
		int level;
		int experience;
	} profession;
};

class Client : public AcceptedSocket
{
public:
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

public:
	Client();

	inline Utils::Game::Session* session() { return &_session; }
	void onRead(NString packet);
	void sendError(std::string&& error);

private:
	Utils::Game::Session _session;
	std::string _username;
	WorkType _currentWork;
	CurrentPhase _phase;
	uint32_t _ingameID;

	std::vector<Character*> _characters;
	Character* _currentCharacter;
};
