#include "entity.h"

Entity::Entity(uint8_t type, uint32_t ingameID) :
	_mustRegenSpawn(true),
	_mustRegenMovement(true),
	_ingameID(ingameID),
	_type(type),
	_speed(type == 1 ? 11 : 5)
{}

Entity::~Entity()
{}

bool Entity::update()
{
	_mustRegenSpawn = true;
	_mustRegenMovement = true;
	return true;
}

NString Entity::getSpawnPacket()
{
	return NString("in ") << (int)_type << ' ';
}

NString Entity::getMovementPacket()
{
	if (_mustRegenMovement)
	{
		_mustRegenMovement = false;

		_movementPacket = NString("mv ");
		_movementPacket << (int)_type << ' ';
		_movementPacket << _ingameID << ' ';
		_movementPacket << _position.x << ' ';
		_movementPacket << _position.y << ' ';
		_movementPacket << (int)_speed;
	}

	return _movementPacket;
}
