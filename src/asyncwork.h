#pragma once

#include <iostream>
#include <functional>
#include <future>

#include <Tools/nstring.h>


class Client;
class WorldHandler;
struct AbstractWork;

#ifdef _WIN32
	#define THIS_CALL __thiscall
#else
	#define THIS_CALL
#endif

using WorkType = bool(THIS_CALL WorldHandler::*)(AbstractWork*);
#define MAKE_WORK(x) (WorkType)(x)


struct AbstractWork
{
public:
	bool operator()(WorldHandler* handler)
	{
		return _handler(handler, this);
	}

	inline Client* client() { return _client; }

protected:
	AbstractWork(Client* client, std::function<bool(WorldHandler*, AbstractWork*)> handler) :
		_client(client),
		_handler(handler)
	{}

protected:
	Client* _client;
	std::function<bool(WorldHandler*, AbstractWork*)> _handler;
};

template <typename T>
struct FutureWork : public AbstractWork
{
public:
	FutureWork(Client* client, std::function<bool(WorldHandler*, AbstractWork*)> handler, std::future<T>&& future) :
		AbstractWork(client, handler)
	{
		_future = std::move(future);
	}

	inline T get() { return _future.get(); }

private:
	std::future<T> _future;
};

struct ClientWork : public AbstractWork
{
	ClientWork(Client* client, std::function<bool(WorldHandler*, AbstractWork*)> handler, NString packet) :
		AbstractWork(client, handler)
	{
		_packet = packet;
	}

	inline NString& packet() { return _packet; }

private:
	NString _packet;
};
