#pragma once

#include "asyncwork.h"
#include "entity.h"

#include <string>
#include <list>
#include <map>

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

struct Item
{
	uint8_t pos;
	uint16_t id;
	uint8_t rare;
	uint8_t upgrade;
	uint8_t type;
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

	std::map<uint8_t, Item*> items;
	std::map<uint8_t, Item*> inventory;

	inline Item* getItem(uint8_t pos, std::map<uint8_t, Item*>* items = nullptr)
	{
		if (!items)
		{
			items = &this->items;
		}

		auto found = items->find(pos);
		if (found != items->end())
		{
			return found->second;
		}

		return nullptr;
	}

	inline int getItemID(uint8_t pos)
	{
		auto item = getItem(pos);
		return item == nullptr ? -1 : item->id;
	}

	NString getItemsList()
	{
		// Do 2 already
		NString list = NString() << getItemID(2);

		// 2 is done just above
		for (auto i : {1, 0, 5, 3, 4, 6, 7})
		{
			list << '.' << getItemID(i);
		}

		return list;
	}

	NString getCompleteItemInfo(Item* item, int removePos = -1)
	{
		int position = (removePos == -1) ? item->pos : removePos;
		int id = (removePos == -1) ? item->id : -item->id;

		return NString() << position << '.' << id << '.' << (int)item->rare << '.' << (int)item->upgrade << '.' << (int)item->type;
	}

	NString getCompleteItemsList()
	{
		NString list = NString();

		for (int i = 0; i < 8; ++i)
		{
			auto found = items.find(i);
			if (found != items.end())
			{
				Item* item = found->second;
				if (!list.empty())
				{
					list << ' ';
				}

				list << getCompleteItemInfo(item).get();
			}
		}

		return list;
	}

	int getFirstFreeInventoryPosition()
	{
		for (int i = 0; i < 40; ++i)
		{
			auto found = inventory.find(i);
			if (found == inventory.end())
			{
				return i;
			}
		}

		return -1;
	}

	Item* unequip(uint8_t pos)
	{
		auto item = getItem(pos);
		if (!item)
		{
			return nullptr;
		}

		int newPos = getFirstFreeInventoryPosition();
		if (newPos != -1)
		{
			item->pos = newPos;
			items.erase(pos);
			inventory.emplace(item->pos, item);
			return item;
		}

		return nullptr;
	}
};


class Map;

class Client : public AcceptedSocket, public Entity
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
	
	NString getSpawnPacket();

	inline Character* pj() { return _currentCharacter; }

	inline void setMap(Map* map) { _currentMap = map; }
	inline Map* getMap() { return _currentMap; }

private:
	Utils::Game::Session _session;
	std::string _username;
	WorkType _currentWork;
	CurrentPhase _phase;

	Map* _currentMap;

	std::vector<Character*> _characters;
	Character* _currentCharacter;

};
