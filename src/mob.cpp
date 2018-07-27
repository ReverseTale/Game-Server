#include "GameServer/mob.h"

Mob::Mob(int type, int id, uint32_t ingameID, Position pos, int radius, int maxHp, int maxMp, int hp, int mp) :
	Entity{ type, ingameID },
	_id(id),
	_radius(radius),
	_maxHp(maxHp),
	_maxMp(maxMp),
	_hp(hp),
	_mp(mp),
	_status(MobStatus::IDLE)
{
	_position = pos;
	_initialPosition = pos;
}

bool Mob::update()
{
	if (_status == MobStatus::IDLE)
	{
		_status = MobStatus::MOVING;
		pos().x = _initialPosition.x + (int)((rand() / float(RAND_MAX) * _radius) - (_radius / 2.0));
		pos().y = _initialPosition.y + (int)((rand() / float(RAND_MAX) * _radius) - (_radius / 2.0));
		Entity::update();

		_lastStatusChange = high_resolution_clock::now();
		return true;
	}
	else if (_lastStatusChange + std::chrono::milliseconds((int)(rand() / float(RAND_MAX) * 2500 + 1500)) <= high_resolution_clock::now())
	{
		_status = MobStatus::IDLE;
	}

	return false;
}

NString Mob::getSpawnPacket()
{
	if (_mustRegenSpawn)
	{
		_mustRegenSpawn = false;

		// in 2 tipo id 666 48 163 2 100 100 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0
		_spawnPacket = Entity::getSpawnPacket();
		_spawnPacket << _id << ' ';
		_spawnPacket << _ingameID << ' ';
		_spawnPacket << _position.x << ' ';
		_spawnPacket << _position.y << " 2 ";
		_spawnPacket << (int)(_hp * 100 / (float)_maxHp) << ' ';
		_spawnPacket << (int)(_mp * 100 / (float)_maxHp) << " 0 0 0 -1 1 0 -1 - 0 -1 0 0 0 0 0 0 0 0";
	}

	return _spawnPacket;
}
