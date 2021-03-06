#include <pthread.h>
#include <time.h>

#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <tcutil.h>

#include <tbb/task_scheduler_init.h>

#include "network.pb.h"
#include "login.pb.h"

#include "constants.h"
#include "config.h"
#include "login.h"
#include "httpserver.h"
#include "misc.h"
#include "world.h"

using namespace tbb;
using namespace std;
using namespace Game;

//Uncomment this line to get dense logging for the web server
#define APP_DEBUG 1

#ifndef APP_DEBUG
#define DEBUG_PRINTF(...)
#else
#define DEBUG_PRINTF(...)  fprintf(stderr,__VA_ARGS__)
#endif


bool app_running = false;

Config* 	config;
LoginDB* 	login_db;
HttpServer* server;
World*		world;


//Sends a nice error message to the client
Network::ServerPacket* error_response(string const& message)
{
	auto response = new Network::ServerPacket();
	response->mutable_error_response()->set_error_message(message);
	return response;
}

//Validates a user name
bool valid_user_name(string const& user_name)
{
	//FIXME: Add stuff here to prevent client side javascript injection
	return true;
}

bool valid_character_name(string const& character_name)
{
	//FIXME: Do validation stuff here
	return true;
}

//Handles a login request
Network::ServerPacket* handle_login(Network::LoginRequest const& login_req)
{
	DEBUG_PRINTF("Got login request\n");

	if(!login_req.has_user_name())
	{
		return error_response("Missing user name");
	}
	else if(!login_req.has_password_hash())
	{
		return error_response("Missing password hash");
	}
	else if(!login_req.has_action())
	{
		return error_response("Missing login action");
	}
	
	DEBUG_PRINTF("User name = %s, password hash = %s, action = %d\n",
		login_req.user_name().c_str(),
		login_req.password_hash().c_str(),
		login_req.action());
	
	Network::ServerPacket packet;
	auto response = packet.mutable_login_response();
	
	switch(login_req.action())
	{
	case Network::LoginRequest_LoginAction_Login:
	{
		Login::Account account;
		if(!login_db->get_account(login_req.user_name(), login_req.password_hash(), account))
		{
			return error_response("Invalid user name/password");
		}
		
		response->set_success(true);
		response->mutable_character_names()->CopyFrom(account.character_names());
	}
	break;
	
	case Network::LoginRequest_LoginAction_CreateAccount:
	{
		if(!valid_user_name(login_req.user_name()))
		{
			return error_response("Invalid user name");
		}
		
		if(!login_db->create_account(login_req.user_name(), login_req.password_hash()))
		{
			return error_response("User name taken");
		}
		
		response->set_success(true);
		response->mutable_character_names()->Clear();
	}
	break;

	case Network::LoginRequest_LoginAction_DeleteAccount:
	{
		Login::Account account;
		if(!login_db->get_account(login_req.user_name(), login_req.password_hash(), account))
		{
			return error_response("Invalid user name/password");
		}
		
		login_db->delete_account(login_req.user_name());
		response->set_success(true);
	}
	break;
	
	case Network::LoginRequest_LoginAction_CreateCharacter:
	{
		if( !login_req.has_character_name() ||
			!valid_character_name(login_req.character_name()) )
		{
			return error_response("Bad character name");
		}
	
		Login::Account account;
		if(!login_db->get_account(login_req.user_name(), login_req.password_hash(), account))
		{
			return error_response("Invalid user name/password");
		}

		if(!world->player_create(login_req.character_name()))
		{
			return error_response("Character name already in use");
		}
		
		//Add character
		Login::Account next_account;
		next_account.CopyFrom(account);
		next_account.add_character_names(login_req.character_name());
		
		//Update database
		if(!login_db->update_account(login_req.user_name(), account, next_account))
		{
			world->player_delete(login_req.character_name());
			return error_response("Could not update account");
		}
		
		//Set response values
		response->set_success(true);
		response->mutable_character_names()->CopyFrom(next_account.character_names());
	}
	break;
	
	case Network::LoginRequest_LoginAction_DeleteCharacter:
	{
		if(!login_req.has_character_name())
		{
			return error_response("Missing character name");
		}
		
		Login::Account account;
		if(!login_db->get_account(login_req.user_name(), login_req.password_hash(), account))
		{
			return error_response("Invalid user name/password");
		}

		int idx = -1, sz = account.character_names_size();
		for(int i=0; i<sz; ++i)
		{
			if(account.character_names(i) == login_req.character_name())
			{
				idx = i;
				break;
			}
		}
		
		if(idx == -1)
		{
			return error_response("Character does not exist");
		}
		
		//Remove player from world
		if(!world->player_delete(login_req.character_name()))
		{
			return error_response("Could not delete character");
		}
		
		//Remove character
		Login::Account next_account;
		next_account.CopyFrom(account);
		next_account.set_character_names(idx, account.character_names(sz-1));
		next_account.mutable_character_names()->RemoveLast();
		
		//Update database
		if(!login_db->update_account(login_req.user_name(), account, next_account))
		{
			return error_response("Could not update account");
		}
		
		//Set response values
		response->set_success(true);
		response->mutable_character_names()->CopyFrom(next_account.character_names());
	}
	break;

	case Network::LoginRequest_LoginAction_Join:
	{
		DEBUG_PRINTF("Character logging in\n");
		if(!login_req.has_character_name())
		{
			return error_response("Missing character name");
		}
	
		Login::Account account;
		if(!login_db->get_account(login_req.user_name(), login_req.password_hash(), account))
		{
			return error_response("Invalid user name/password");
		}
		
		int idx = -1, sz = account.character_names_size();
		for(int i=0; i<sz; ++i)
		{
			if(account.character_names(i) == login_req.character_name())
			{
				idx = i;
				break;
			}
		}
		
		if(idx == -1)
		{
			return error_response("Character does not exist");
		}

		//FIXME: Allocate session id for character and return it
		SessionID session_id;
		if(!world->player_join(login_req.character_name(), session_id))
		{
			DEBUG_PRINTF("Creating session id\n");
			return error_response("Player is already logged in");
		}
		
		response->set_session_id(session_id);
		response->set_success(true);
	}
	break;
	}
	
	return new Network::ServerPacket(packet);
}

//Handles an http event
Network::ServerPacket* post_callback(HttpRequest const& request, Network::ClientPacket* client_packet)
{
	DEBUG_PRINTF("Got a packet!\n");
	if(client_packet->has_login_packet())
	{
		return handle_login(client_packet->login_packet());
	}
	else
	{
		return NULL;
	}
}

int hex_2_num(char c)
{
	if(c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if(c >= 'a' && c <= 'f')
	{
		return 10 + (c - 'a');
	}
	if(c >= 'A' && c <= 'F')
	{
		return 10 + (c - 'A');
	}
	return 0;
}


//Handles a websocket connection
bool websocket_callback(HttpRequest const& request, WebSocket* websocket)
{
	//Parse out portname and session id from request
	int slash_pos = -1;
	for(int i=0; i<request.url.size(); ++i)
	{
		char c = request.url[i];
	
		if(c == '/')
		{
			slash_pos = i;
			break;
		}
	}
	
	if(slash_pos == -1 || slash_pos + 16 >= request.url.size() )
	{
		DEBUG_PRINTF("Invalid web socket connection\n");
		return false;
	}
	
	string portname = request.url.substr(0, slash_pos);
	
	//Parse out the session id
	uint64_t session_id = 0;
	for(int i=0; i<16; ++i)
	{
		session_id = session_id*16 + hex_2_num(request.url[slash_pos+i+1]);
	}
	
	DEBUG_PRINTF("Got socket, portname = %s, session id = %ld\n", portname.c_str(), session_id);
	
	//Attach the socket
	if(portname == "map")
	{
		DEBUG_PRINTF("Attaching map socket\n");
		return world->player_attach_map_socket((SessionID)session_id, websocket);
	}
	else if(portname == "update")
	{
		DEBUG_PRINTF("Attaching update socket\n");
		return world->player_attach_update_socket((SessionID)session_id, websocket);
	}
	
	DEBUG_PRINTF("Unrecognized port name\n");

	return false;
}

//Server initialization
void init_app()
{
	if(app_running)
	{
		printf("App already started\n");
		return;
	}

	printf("Starting app\n");
	
	
	printf("Starting login database\n");
	if(!login_db->start())
	{
		login_db->stop();
		printf("Failed to start login database\n");
		return;
	}

	printf("Starting world instance\n");
	if(!world->start())
	{
		world->stop();
		printf("Failed to start world instance");
		return;
	}
	
	printf("Starting http server\n");
	if(!server->start())
	{
		login_db->stop();
		world->stop();
		server->stop();
		printf("Failed to start http server\n");
		return;
	}
	
	app_running = true;
}


//Kills server
void shutdown_app()
{
	if(!app_running)
	{
		printf("App already stopped\n");
		return;
	}

	printf("Stopping app\n");

	printf("Stopping world instance\n");
	world->stop();
	
	printf("Stopping http server\n");
	server->stop();
	
	printf("Stopping login database\n");
	login_db->stop();
	
	
	app_running = false;
}

//Saves the state of the entire game to the database
void resync()
{
	if(!app_running)
	{
		printf("App is not running\n");
		return;
	}
	
	printf("Synchronizing login\n");
	login_db->sync();
	
	printf("Synchronizing world state\n");
	world->sync();
}

//This loop runs in the main thread.  Implements the admin console
void console_loop()
{
	while(true)
	{
		string command;
		
		cin >> command;
		
		if( command == "q" || command == "quit" )
		{
			break;
		}
		else if(command == "r" || command == "recache")
		{
			if(app_running)
			{
				printf("Recaching wwwroot\n");
				server->recache();
			}
			else
			{
				printf("App not running");
			}
		}
		else if(command == "s" || command == "sync")
		{
			resync();
		}
		else if(command == "print")
		{
			string a;
			cin >> a;
			cout << config->readString(a) << endl;
		}
		else if(command == "set")
		{
			string a, b;
			cin >> a >> b;
			config->storeString(a, b);
		}
		else if(command == "reset_config")
		{
			printf("Resetting configuration file to defaults\n");
			config->resetDefaults();
		}
		else if(command == "help")
		{
			printf("Read source code for documentation\n");
		}
		else
		{
			printf("Unknown command\n");
		}
	}
}

//Program start point
int main(int argc, char** argv)
{
		

	//Verify protocol buffers are working correctly
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	//Randomize timer
	srand(time(NULL));
	
	//Initialize task scheduler
	task_scheduler_init init;

	string config_file = "data/config.tch";	
	if(argc > 1)
		config_file = string(argv[1]);

	printf("Allocating objects\n");
	auto GC = ScopeDelete<Config>(config = new Config(config_file));
	auto GW = ScopeDelete<World>(world = new World(config));
	auto GL = ScopeDelete<LoginDB>(login_db = new LoginDB(config));
	auto GS = ScopeDelete<HttpServer>(server = new HttpServer(config, post_callback, websocket_callback));

	init_app();
	
	if(app_running)
	{
		console_loop();
		shutdown_app();
	}
	else
	{
		printf("Error initiailizing server, shutting down\n");
	}
	
	//Kill protocol buffer library
	google::protobuf::ShutdownProtobufLibrary();

	return 0;
}

