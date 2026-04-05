#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>

#define PARSE_SUCCESS 0
#define PARSE_ERROR 1
#define PARSE_INCOMPLETE 2

typedef enum{
	METHOD             ,
	PATH               ,
	VERSION            ,
	CR                 ,
	LF                 ,
	HEADER_KEY         ,
	HEADER_VALUE       ,
	BODY               ,
	CHUNK_SIZE         ,             // Always in hex
	CHUNK_EXTENSIONS   ,            // Always ignore extensions
	CHUNK_SIZE_CR      ,
	CHUNK_SIZE_LF      ,        
	CHUNK_DATA         ,
	CHUNK_DATA_CR      ,
	CHUNK_DATA_LF      ,
	CHUNK_TRAILERS     ,           // Always ignore trailers
	CHUNK_TRAILERS_LF  ,              
	DONE               ,
	ERROR              ,
}Parser_State;

typedef enum{               // You can use 0x01, 0x02 or 1 << 0, 1 << 1. Same difference.
	F_CHUNKED           = 0x01,
	F_KEEP_ALIVE        = 0x02,
	F_CONTENT_LENGTH    = 0x04
}flags;

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

	uint8_t flags; // values from Parser_Flags
       
	size_t chunk_size;
	const char *chunk_data;
	size_t chunk_bytes_read;
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

int parse_http_request(
		HttpRequest *request,
		Parser *parser, 
		const char *buf, 
		size_t buf_total
		);

#endif

/*
 *          CHUNKED FINITE STATE MACHINE
 *          ============================
 * 
 * size -> if ';' or ' ' then ext else if '\r' then size cr
 * ext -> if '\r' then size cr
 * size cr -> if '\n' then size lf
 * size lf -> if size > 0 then data else trailers
 * data -> if 'r\' then data cr
 * data cr -> if '\n' then data lf
 * data lf -> size
 * trailers -> if '\r\n\r\n' then done
 *                                    
 *    ____________                _____________________________________         
 *   |            |              |                                     |
 *   |            v              |                                     v
 * SIZE -> EXT -> SIZE CR -> SIZE LF -> DATA -> DATA CR -> DATA LF  TRAILERS -> DONE
 *   ^                                                       |
 *   |_______________________________________________________|
 *
 *
 */

