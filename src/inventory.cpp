#include "GameServer/inventory.h"

Inventory::Inventory(int idx) :
	_idx(idx)
{}

Item* Inventory::getItem(uint8_t pos)
{
	auto found = _items.find(pos);
	if (found != _items.end())
	{
		return found->second;
	}

	return nullptr;
}

int Inventory::getItemID(uint8_t pos)
{
	auto item = getItem(pos);
	return item == nullptr ? -1 : item->id;
}

NString Inventory::getItemsList()
{
	// Do 2 already
	NString list = NString() << getItemID(2);

	// 2 is done just above
	for (auto i : { 1, 0, 5, 3, 4, 6, 7 })
	{
		list << '.' << getItemID(i);
	}

	return list;
}

NString Inventory::getCompleteItemInfo(Item* item, int removePos)
{
	int position = (removePos == -1) ? item->pos : removePos;
	int id = (removePos == -1) ? item->id : -item->id;

	return NString() << position << '.' << id << '.' << (int)item->rare << '.' << (int)item->upgrade << '.' << (int)item->type;
}

NString Inventory::getCompleteItemsList()
{
	NString list = NString();

	for (int i = 0; i < 8; ++i)
	{
		auto found = _items.find(i);
		if (found != _items.end())
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

int Inventory::getFirstFree()
{
	for (int i = 0; i < 40; ++i)
	{
		auto found = _items.find(i);
		if (found == _items.end())
		{
			return i;
		}
	}

	return -1;
}

bool Inventory::insert(Item* item, uint8_t pos)
{
	pos = (pos == 0xFF) ? getFirstFree() : pos;
	if (pos == 0xFF)
	{
		return false;
	}

	item->pos = pos;
	_items.emplace(pos, item);
	return true;
}

Item* Inventory::remove(uint8_t pos)
{
	auto item = getItem(pos);
	if (!item)
	{
		return nullptr;
	}

	remove(pos);
	return item;
}

void Inventory::remove_unsecure(uint8_t pos)
{
	_items.erase(pos);
}