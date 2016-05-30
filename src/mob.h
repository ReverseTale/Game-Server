#pragma once

#include "common.h"
#include "asyncwork.h"
#include "entity.h"

#include <string>

#include <Tools/utils.h>
#include <Tools/accepted_socket.h>


#ifdef max
	#undef max
	#undef min
#endif


enum class MobStatus
{
	IDLE,
	MOVING
};


class Mob : public Entity
{
public:
	Mob(int type, int id, uint32_t ingameID, Position pos, int radius, int maxHp, int maxMp, int hp, int mp);

	bool update() override;
	NString getSpawnPacket() override;

private:
	int _id;
	int _radius;
	int _maxHp;
	int _maxMp;
	int _hp;
	int _mp;

	Position _initialPosition;

	MobStatus _status;
	high_resolution_clock::time_point _lastStatusChange;	
};
