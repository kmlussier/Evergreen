#include "srfsh.h"


int main( int argc, char* argv[] ) {


	/* --------------------------------------------- */
	if( argc < 2 ) {

		/* see if they have a .srfsh.xml in their home directory */
		char* home = getenv("HOME");
		int l = strlen(home) + 36;
		char fbuf[l];
		memset(fbuf, 0, l);
		sprintf(fbuf,"%s/.srfsh.xml",home);

		if(!access(fbuf, R_OK)) {
			if( ! osrf_system_bootstrap_client(fbuf) ) 
				fatal_handler( "Unable to bootstrap client for requests");

		} else {
			fatal_handler( "No Config file found at %s and none specified. "
					"\nusage: %s <config_file>", fbuf, argv[0] );
		}

	} else {
		if( ! osrf_system_bootstrap_client(argv[1]) ) 
			fatal_handler( "Unable to bootstrap client for requests");
	}
	/* --------------------------------------------- */
	load_history();


	client = osrf_system_get_transport_client();


	/* main process loop */
	char* request;
	while((request=readline(prompt))) {

		if( !strcmp(request, "exit") || !strcmp(request,"quit")) 
			break; 

		char* req_copy = strdup(request);

		parse_request( req_copy ); 
		if( request && strlen(request) > 2 ) {
			add_history(request);
		}

		free(request);
		free(req_copy);
	}

	if(history_file != NULL )
		write_history(history_file);
	free(request);
	client_disconnect( client );
	client_free( client );	
	config_reader_free();	
	log_free();
		
	return 0;
}

void sig_child_handler( int s ) {
	child_dead = 1;
}


int load_history() {

	char* home = getenv("HOME");
	int l = strlen(home) + 24;
	char fbuf[l];

	memset(fbuf, 0, l);
	sprintf(fbuf,"%s/.srfsh_history",home);
	history_file = strdup(fbuf);

	if(!access(history_file, W_OK | R_OK )) {
		//set_history_length(999);
		history_length = 999;
		read_history(history_file);
	}
	return 1;
}


int parse_error( char* words[] ) {

	if( ! words )
		return 0;

	int i = 0;
	char* current;
	char buffer[256];
	memset(buffer, 0, 256);
	while( (current=words[i++]) ) {
		strcat(buffer, current);
		strcat(buffer, " ");
	}
	if( ! buffer || strlen(buffer) < 1 ) 
		printf("\n");

	fprintf( stderr, "???: %s\n", buffer );
	return 0;

}


int parse_request( char* request ) {

	if( request == NULL )
		return 0;

	int ret_val = 0;
	int i = 0;
	char* words[COMMAND_BUFSIZE]; 
	memset(words,0,COMMAND_BUFSIZE);
	char* req = request;

	char* cur_tok = strtok( req, " " );

	if( cur_tok == NULL )
		return 0;

	while(cur_tok != NULL) {
		words[i++] = cur_tok;
		cur_tok = strtok( NULL, " " );
	}


	// not sure why (strtok?), but this is necessary
	memset( words + i, 0, COMMAND_BUFSIZE - i );

	/* pass off to the top level command */
	if( !strcmp(words[0],"router") ) 
		ret_val = handle_router( words );

	else if( !strcmp(words[0],"time") ) 
		ret_val = handle_time( words );

	else if (!strcmp(words[0],"request"))
		ret_val = handle_request( words, 0 );

	else if (!strcmp(words[0],"relay"))
		ret_val = handle_request( words, 1 );

	else if (!strcmp(words[0],"help"))
		ret_val = print_help();

	else if (!strcmp(words[0],"set"))
		ret_val = handle_set(words);

	else if (!strcmp(words[0],"print"))
		ret_val = handle_print(words);

	else if (words[0][0] == '!')
		ret_val = handle_exec( words );

	if(!ret_val)
		return parse_error( words );

	return 1;

}

int handle_set( char* words[]) {

	char* variable;
	if( (variable=words[1]) ) {

		char* val;
		if( (val=words[2]) ) {

			if(!strcmp(variable,"pretty_print")) {
				if(!strcmp(val,"true")) {
					pretty_print = 1;
					printf("pretty_print = true\n");
					return 1;
				} 
				if(!strcmp(val,"false")) {
					pretty_print = 0;
					printf("pretty_print = false\n");
					return 1;
				} 
			}
		}
	}

	return 0;
}


int handle_print( char* words[]) {

	char* variable;
	if( (variable=words[1]) ) {
		if(!strcmp(variable,"pretty_print")) {
			if(pretty_print) {
				printf("pretty_print = true\n");
				return 1;
			} else {
				printf("pretty_print = false\n");
				return 1;
			}
		}
	}
	return 0;
}

int handle_router( char* words[] ) {

	if(!client)
		return 1;

	int i;

	if( words[1] ) { 
		if( !strcmp(words[1],"query") ) {
			
			if( words[2] && !strcmp(words[2],"servers") ) {
				for(i=3; i < COMMAND_BUFSIZE - 3 && words[i]; i++ ) {	
					router_query_servers( words[i] );
				}
				return 1;
			}
			return 0;
		}
		return 0;
	}
	return 0;
}


int handle_exec(char* words[]) {

	int len = strlen(words[0]);
	char command[len];
	memset(command,0,len);

	int i; /* chop out the ! */
	for( i=1; i!= len; i++) {
		command[i-1] = words[0][i];
	}

	free(words[0]);
	words[0] = strdup(command);
	signal(SIGCHLD,sig_child_handler);
	if(fork()) {
		while(1) {
			sleep(100);
			if(child_dead) {
				signal(SIGCHLD,sig_child_handler);
				child_dead = 0;
				break;
			}
		}
	} else {
		execvp( words[0], words );
		exit(0);
	}
	return 1;
}


int handle_request( char* words[], int relay ) {

	if(!client)
		return 1;

	if(words[1]) {
		char* server = words[1];
		char* method = words[2];
		int i;
		growing_buffer* buffer = NULL;
		if(!relay) {
			buffer = buffer_init(128);
			buffer_add(buffer, "[");
			for(i = 3; words[i] != NULL; i++ ) {
				buffer_add( buffer, words[i] );
				buffer_add(buffer, " ");
			}
			buffer_add(buffer, "]");
		}

		return send_request( server, method, buffer, relay );
	} 

	return 0;
}

int send_request( char* server, 
		char* method, growing_buffer* buffer, int relay ) {
	if( server == NULL || method == NULL )
		return 0;

	json* params = NULL;
	if( !relay ) {
		if( buffer != NULL && buffer->n_used > 0 ) 
			params = json_tokener_parse(buffer->buf);
	} else {
		if(!last_result->result_content) { 
			warning_handler("We're not going to call 'relay' on empty result");
			return 1;
		}
		else {
			json* arr = json_object_new_array();
			json_object_array_add( arr, last_result->result_content );
			params = arr;
		}
	}

	osrf_app_session* session = osrf_app_client_session_init(server);
	double start = get_timestamp_millis();

	if(!osrf_app_session_connect(session)) {
		warning_handler( "Unable to connect to remote service %s\n", server );
		return 1;
	}

	int req_id = osrf_app_session_make_request( session, params, method, 1 );

	osrf_message* omsg = osrf_app_session_request_recv( session, req_id, 8 );

	if(!omsg) 
		printf("\nReceived no data from server\n");
	
	
	while(omsg) {

		if(omsg->result_content) {

			osrf_message_free(last_result);
			last_result = omsg;

			if( pretty_print ) {
				char* content = json_printer( omsg->result_content );
				printf( "\nReceived Data: %s\n",content );
				free(content);
			} else {
				printf( "\nReceived Data: %s\n",
					json_object_to_json_string(omsg->result_content));
			}

		} else {

			printf( "\nReceived Exception:\nName: %s\nStatus: "
					"%s\nStatusCode %d\n", omsg->status_name, 
					omsg->status_text, omsg->status_code );
		}

		omsg = osrf_app_session_request_recv( session, req_id, 5 );
	}

	double end = get_timestamp_millis();

	printf("\n------------------------------------\n");
	if( osrf_app_session_request_complete( session, req_id ))
		printf("Request Completed Successfully\n");


	printf("Request Time in seconds: %f\n", end - start );
	printf("------------------------------------\n");

	osrf_app_session_request_finish( session, req_id );
	osrf_app_session_disconnect( session );
	osrf_app_session_destroy( session );

	return 1;


}

int handle_time( char* words[] ) {

	if( ! words[1] ) {

		char buf[36];
		memset(buf,0,36);
		get_timestamp(buf);
		printf( "%s\n", buf );
		return 1;
	}

	if( words[1] ) {
		time_t epoch = (time_t)atoi( words[1] );
		char* localtime = strdup( ctime( &epoch ) );
		printf( "%s => %s", words[1], localtime );
		free(localtime);
		return 1;
	}

	return 0;

}

		

int router_query_servers( char* router_server ) {

	if( ! router_server || strlen(router_server) == 0 ) 
		return 0;

	char rbuf[256];
	memset(rbuf,0,256);
	sprintf(rbuf,"router@%s/router", router_server );
		
	transport_message* send = 
		message_init( "servers", NULL, NULL, rbuf, NULL );
	message_set_router_info( send, NULL, NULL, NULL, "query", 0 );

	client_send_message( client, send );
	message_free( send );

	transport_message* recv = client_recv( client, -1 );
	if( recv == NULL ) {
		fprintf(stderr, "NULL message received from router\n");
		return 1;
	}
	
	printf( 
			"---------------------------------------------------------------------------------\n"
			"Received from 'server' query on %s\n"
			"---------------------------------------------------------------------------------\n"
			"original reg time | latest reg time | last used time | class | server\n"
			"---------------------------------------------------------------------------------\n"
			"%s"
			"---------------------------------------------------------------------------------\n"
			, router_server, recv->body );

	message_free( recv );
	
	return 1;
}
		
int print_help() {

	printf(
			"---------------------------------------------------------------------------------\n"
			"Commands:\n"
			"---------------------------------------------------------------------------------\n"
			"help			- Display this message\n"
			"!<command> [args] - Forks and runs the given command in the shell\n"
			"time			- Prints the current time\n"					
			"time <timestamp>	- Formats seconds since epoch into readable format\n"	
			"set <variable> <value> - set a srfsh variable (e.g. set pretty_print true )\n"
			"print <variable>		- Displays the value of a srfsh variable\n"
			"---------------------------------------------------------------------------------\n"
			"router query servers <server1 [, server2, ...]>\n"
			"	- Returns stats on connected services\n"
			"\n"
			"request <service> <method> [ <json formatted string of params> ]\n"
			"	- Anything passed in will be wrapped in a json array,\n"
			"		so add commas if there is more than one param\n"
			"\n"
			"relay <service> <method>\n"
			"	- Performs the requested query using the last received result as the param\n"
			"---------------------------------------------------------------------------------\n"
			);

	return 1;
}



char* tabs(int count) {
	growing_buffer* buf = buffer_init(24);
	int i;
	for(i=0;i!=count;i++)
		buffer_add(buf, "   ");

	char* final = buffer_data( buf );
	buffer_free( buf );
	return final;
}

char* json_printer( json* object ) {

	if(object == NULL)
		return NULL;
	char* string = json_object_to_json_string(object);

	growing_buffer* buf = buffer_init(64);
	int i;
	int tab_var = 0;
	for(i=0; i!= strlen(string); i++) {

		if( string[i] == '{' ) {

			buffer_add(buf, "\n");
			char* tab = tabs(tab_var);
			buffer_add(buf, tab);
			free(tab);
			buffer_add( buf, "{");
			tab_var++;
			buffer_add( buf, "\n" );	
			tab = tabs(tab_var);
			buffer_add( buf, tab );	
			free(tab);

		} else if( string[i] == '[' ) {

			buffer_add(buf, "\n");
			char* tab = tabs(tab_var);
			buffer_add(buf, tab);
			free(tab);
			buffer_add( buf, "[");
			tab_var++;
			buffer_add( buf, "\n" );	
			tab = tabs(tab_var);
			buffer_add( buf, tab );	
			free(tab);

		} else if( string[i] == '}' ) {

			tab_var--;
			buffer_add(buf, "\n");
			char* tab = tabs(tab_var);
			buffer_add(buf, tab);
			free(tab);
			buffer_add( buf, "}");
			buffer_add( buf, "\n" );	
			tab = tabs(tab_var);
			buffer_add( buf, tab );	
			free(tab);

		} else if( string[i] == ']' ) {

			tab_var--;
			buffer_add(buf, "\n");
			char* tab = tabs(tab_var);
			buffer_add(buf, tab);
			free(tab);
			buffer_add( buf, "]");
			buffer_add( buf, "\n" );	
			tab = tabs(tab_var);
			buffer_add( buf, tab );	
			free(tab);

		} else {
			char b[2];
			b[0] = string[i];
			b[1] = '\0';
			buffer_add( buf, b ); 
		}

	}

	char* result = buffer_data(buf);
	buffer_free(buf);
	return result;

}
