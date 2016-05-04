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

	// Not DB related
	int mp;
	int hp;

	inline int hpPercent() { return (int)(hp * 100.0 / (float)maxHP); }
	inline int mpPercent() { return (int)(mp * 100.0 / (float)maxMP); }
};

struct Position
{
	int x;
	int y;
};

class Map;

class Client : public AcceptedSocket
{
	friend class WorldHandler;

public:
	Client();

	inline Utils::Game::Session* session() { return &_session; }
	void onRead(NString packet);

	void sendCharacterInformation();
	void sendCharacterLevel();
	void sendCharacterStatus();
	void sendCharacterPosition();
	void sendCharacterMap();

	inline uint32_t id() { return _ingameID; }
	inline Character* pj() { return _currentCharacter; }
	inline Position& pos() { return _position; }

	inline void setMap(Map* map) { _currentMap = map; }
	inline Map* getMap() { return _currentMap; }

private:
	Utils::Game::Session _session;
	std::string _username;
	WorkType _currentWork;
	CurrentPhase _phase;
	uint32_t _ingameID;

	Map* _currentMap;
	Position _position;

	std::vector<Character*> _characters;
	Character* _currentCharacter;
};
