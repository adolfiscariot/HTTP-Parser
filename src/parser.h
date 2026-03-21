#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef enum{
	METHOD,
	PATH,
	VERSION,
	CR,
	LF,
	HEADER_KEY,
	HEADER_VALUE,
	BODY,
	DONE,
	ERROR
}Parser_State;

typedef struct{
	Parser_State state;
	size_t position;
	
	const char *method_start;
	size_t method_len;

	const char *path_start;
	size_t path_len;

	const char *version_start;
	size_t version_len;

	const char *line_start; // simplifies parsing CRLF delimited lines

	long body_len; // expected content-length
	long body_bytes_read;

}Parser;

typedef struct{
	const char *key;
	size_t key_len;

	const char *value;
	size_t value_len;
}Header;

typedef struct{ //Ordered from largest to smallest for better cache alignment 
	Header headers[20]; // Host: localhost:4040, Keep-alive: yes
	uint8_t header_count; 

	const char *method; //GET, POST, etc
	size_t method_len;

	const char *path; // /info.html 
	size_t path_len;

	const char *query_string; // ?pageNo=5 
	size_t query_len;

	const char *protocol; // HTTP/1.1 
	size_t protocol_len;

	const char *body; // Used in POST, PUT methods e.g form submissions 
	size_t body_len;

} HttpRequest;

int parse_http_request(HttpRequest *request,
			Parser *parser, 
			const char *buf, 
			size_t buf_total);

#endif
