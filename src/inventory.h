#pragma once

#include <Tools/utils.h>

#include <map>


struct Item
{
	uint8_t pos;
	uint16_t id;
	uint8_t rare;
	uint8_t upgrade;
	uint8_t type;
};

class Inventory
{
public:
	Inventory(int idx);

	Item* getItem(uint8_t pos);
	int getItemID(uint8_t pos);
	NString getItemsList();
	NString getCompleteItemInfo(Item* item, int removePos = -1);
	NString getCompleteItemsList();
	int getFirstFree();

	bool insert(Item* item, uint8_t pos = 0xFF);
	Item* remove(uint8_t pos);
	void remove_unsecure(uint8_t pos);

private:
	int _idx;
	std::map<uint8_t, Item*> _items;
};