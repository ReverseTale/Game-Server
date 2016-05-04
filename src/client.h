#pragma once

#include <string>

#include "asyncwork.h"
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
	friend class WorldHandler;

public:
	Client();

	inline Utils::Game::Session* session() { return &_session; }
	void onRead(NString packet);

private:
	Utils::Game::Session _session;
	std::string _username;
	WorkType _currentWork;
	CurrentPhase _phase;
	uint32_t _ingameID;

	std::vector<Character*> _characters;
	Character* _currentCharacter;
};
