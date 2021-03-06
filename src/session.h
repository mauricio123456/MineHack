#ifndef SESSION_H
#define SESSION_H

#include <string>
#include <stdint.h>

#include <tbb/tick_count.h>
#include <tbb/atomic.h>
#include <tbb/task.h>
#include <tbb/queuing_rw_mutex.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_hash_map.h>

#include "constants.h"
#include "config.h"
#include "httpserver.h"
#include "chunk.h"
#include "entity.h"

namespace Game
{
	//Session ID
	typedef uint64_t SessionID;
	
	enum SessionState
	{
		SessionState_Pending,
		SessionState_Active,
		SessionState_Dead
	};
	
	//A player's session information
	struct Session
	{
		Session(SessionID const& id, std::string const& player_name);
		~Session();
	
		//Basic session state information
		SessionID			session_id;
		SessionState		state;
		tbb::tick_count		last_activity, last_updated;
	
		//Player state/entity information
		std::string			player_name;
		Coord				player_coord;
		
		//Map state information
		typedef tbb::concurrent_unordered_map<ChunkID, uint64_t, ChunkIDHashCompare> chunk_records_t;
		chunk_records_t known_chunks;
		
		//Web socket connections
		WebSocket			*update_socket;
		WebSocket			*map_socket;
	};

	//Session manager
	struct SessionManager
	{
		SessionManager();
		~SessionManager();
		
		//Creates a new session
		bool create_session(std::string const& player_name, SessionID& session_id);
		
		//Socket attachment
		bool attach_update_socket(SessionID const& session_id, WebSocket* socket);
		bool attach_map_socket(SessionID const& session_id, WebSocket* socket);
		
		//Session removal
		void remove_session(SessionID const& session_id);
		
		//Must be called when no other task is accessing the session set, applies all
		//pending deletes to the session data set
		void process_pending_deletes();
		
		//Erases all sessions
		void clear_all_sessions();
		
		//The session data set (only accessed directly from the world class, do not ever touch this)
		tbb::concurrent_unordered_map<SessionID, Session*> sessions;
		
	private:
		//The session manager lock.  Is held during the delete, and must be read-locked before accessing a session
		tbb::queuing_rw_mutex session_lock;
		
		//Pending session removal events
		tbb::concurrent_queue<SessionID>	pending_erase;
		
		//Generates a random session
		SessionID generate_session_id();
	};
};

#endif

