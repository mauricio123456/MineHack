#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <pthread.h>
#include <netinet/in.h>

#include <string>
#include <map>
#include <vector>

#include <cstdint>

#include "constants.h"
#include "config.h"
#include "network.pb.h"

namespace Game {

	class HttpServer;

	enum SocketEventState
	{
		Listening,
		WaitForHeader,
		Receiving,
		Processing,
		Sending
	};

	//The internal HTTP client event packet structure
	//Generally speaking, this is only used inside the http server.  Users interact with
	// HttpEvent
	struct SocketEvent
	{
		SocketEvent(int fd, bool listener);
		~SocketEvent();
	
		//The internal socket file descriptor
		int socket_fd;
		
		//Socket address
		sockaddr_storage addr;
		
		//State for the event
		SocketEventState state;
		
		//Recv buffer stuff
		int recv_buf_size, content_length, pending_bytes;
		char *recv_buf_start, *recv_buf_cur, *content_ptr;
		
		//Send buffer stuff
		bool free_send_buffer;		//If set, dispose of send buffer on send completion
		char *send_buf_start, *send_buf_cur;
		
		//Try parsing the request
		int try_parse(char** request, int* request_len, char** content_start, int* content_length);
	};
	
	//The external interface to the HttpEvent server
	struct HttpEvent
	{
		HttpEvent();
		~HttpEvent();
		
		void reply(void* buf, int len);
		
	private:
		SocketEvent*	socket_event;
		HttpServer*		server;
	};
	
	//The callback function type
	typedef bool (*http_func)(HttpEvent*);

	//The HttpServer
	struct HttpServer
	{
		//Creates the HttpServer
		HttpServer(Config* cfg, http_func handler);
		~HttpServer();
		
		bool start();
		void stop();
		
	private:

		//Configuration stuff
		Config* config;
		http_func callback;

		//Basic server stuff
		volatile bool running;
		int living_threads;
		
		//Event stuff
		int epollfd;
		pthread_spinlock_t event_map_lock;
		TCMAP* event_map;
		SocketEvent* create_event(int fd, bool listener = false);
		void dispose_event(SocketEvent* event);
		void cleanup_events();
		
		//Event handlers
		void accept_event(SocketEvent* event);
		void process_header(SocketEvent* event, bool do_recv = true);
		void process_cached_request(SocketEvent* event, char* req, int req_len);
		void process_command(SocketEvent* event);
		void process_recv(SocketEvent* event);
		void process_send(SocketEvent* event);
		
		//Worker stuff
		pthread_t workers[NUM_HTTP_WORKERS];
		static void* worker_start(void*);
		void worker_loop();
		
		//Cache functions
		TCMAP*	cached_response;
		void init_cache();
		const char* get_cached_response(const char* filename, int filename_len, int* cache_len);
		void clear_cache();
	};
};

#endif
