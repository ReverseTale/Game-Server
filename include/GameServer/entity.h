#pragma once

#include <Tools/utils.h>


struct Position
{
	int x;
	int y;
};

class Entity
{
public:
	Entity(uint8_t type, uint32_t ingameID = -1);
	virtual ~Entity();

	virtual bool update();
	virtual NString getSpawnPacket();
	NString getMovementPacket();

	inline Position& pos() { return _position; }
	inline uint32_t id() { return _ingameID; }

protected:
	uint8_t _type;
	uint32_t _ingameID;
	Position _position;
	uint8_t _speed;

	bool _mustRegenSpawn;
	NString _spawnPacket;

	bool _mustRegenMovement;
	NString _movementPacket;
};
