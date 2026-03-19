/*
 * This parser is:
 * Single-pass -> sequential, reading each byte once
 * Zero-copying -> no memory allocations. 
 * Finite state machine -> progress is encoded via states.
 * Stream-friendly -> parses partial data.
 *
 * We will be passing through the header byte by byte using pointers
 * instead of allocating any strings to memory. As such the structs below
 * have a length field for each header value e.g. char *path, size_t path_len
 *
 * This parser is heavily influenced by this paper: 
 * Finite State Machine Parsing for Internet Protocols: Faster Than You Think 
 * https://ieeexplore.ieee.org/document/6957302 (Link to above paper)
 */

#if (defined (__GNUC__) && __GNUC__ >= 7) || (defined(__clang__) && __clang_major__ >= 10)
	#define FALLTHROUGH __attribute__((fallthrough))
#else
	#define FALLTHROUGH ((void)0)
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define PARSE_SUCCESS 0
#define PARSE_ERROR 1
#define PARSE_INCOMPLETE 2

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

/*
 * We will use a switch statement due to: 
 *
 * Avoids branch prediction penalties by removing 'break' on state transitions as we 
 * already know that state1 flows into state2 (METHOD into PATH into VERSION...)
 * thus we avoid re-entering the switch statement and fallthrough instead.
 *
 * Correct speculative execution since a linear execution of instructions takes 
 * place and so since there's only one possible transition the CPU guesses correctly.
 *
 * We maintain spatial locality of the instruction cache. Instructions are executed
 * sequentially as there's no break statement.
 *
 * The alternative was a matrix i.e. new_state = table[curr_state][input] but
 * this would consume much more memory as each input byte has 256 possible values
 * multiplied by the 10 states in the parser_state that's 2560 possible entries.
 * We could narrow this down since we know headers only contain alphanumeric characters,
 * a carriage return, a line feed, a colon, a hyphen, and a space, thus turning 256 possible
 * states for each input to only 8 (A-Z, a-z, 0-9, ':',, '-', ' ', '\r', '\n') but that
 * would be extra work that switch statements avoid.
 */

int parse_http_request(HttpRequest *request, Parser *parser, const char *buf, size_t buf_total){ 
	Parser_State state = parser->state;

	for (size_t i = parser->position; i < buf_total; i++){
		char c = buf[i];

		switch(state){
			case METHOD:
				if (parser->method_len == 0){
					parser->method_start = &buf[i];
					parser->line_start = &buf[i];
				}

				/*
				 * if we see a space, we're done. add 1 to i to
				 * skip the space, change state to path and fallthrough.
				 * if method_len > 6 return an error as the longest method
				 * we support is 'delete '. otherwise keep parsing through the
				 * buffer.
				 */

				if (c == ' '){
					i++; // skip the space
					if (i >= buf_total){
						break;
					}
					c = buf[i];
					state = PATH;
					FALLTHROUGH;
				} else if (parser->method_len > 6){
					state = ERROR;
					break;
				} else{
					parser->method_len++;
					break;
				} 

			case PATH:
				if (parser->path_len == 0){
					parser->path_start = &buf[i];
				}

				if (c == ' '){
					i++; 
					if (i >= buf_total){
						break;
					}
					c = buf[i];
					state = VERSION;
					FALLTHROUGH;
				} else{
					parser->path_len++;
					break;
				}

			case VERSION:
				if (parser->version_len == 0){
					parser->version_start = &buf[i];
				}

				if (c == '\r'){
					state = CR;
					break;
				} else{
					parser->version_len++;
					break;
				}

			case CR:
				printf("CR: c='%c' (%d) at position (%zu\n", c, c, i);
				if (c == '\n'){
					state = LF;
					i++;
					c = buf[i];
					printf("CR: Moving to LF, new c = '%c' (%d) at pos %zu\n", c, c, i);
					FALLTHROUGH;
				} else{
					state = ERROR;
					break;
				}

			/*
			 * '\n' can be followed by '\r' of by an uppercase letter symbolizing
			 * the start of headers. If it's followed by '\r' return back to CF state
			 * otherwise loop through the uppercase letters and check if it's followed
			 * by any of them. If it is, we transition to HEADER_KEY state. Otherwise,
			 * it must be an error
			 */
			case LF:
				printf("LF: c='%c' (%d)\n", c, c);
				if (c == '\r'){
					// if CR re-appears after LF that means the body is next
					i++; // skip to \n
					if (i < buf_total && buf[i] == '\n'){
						i++; //skip to beginning of body
						if (parser->body_len > 0){
							state = BODY;
							request->body = &buf[i];
							parser->line_start = &buf[i];
							break;
							//FALLTHROUGH;
						} else{
							state = DONE;
							break;
						}
					}
				} else if (c >= 'A' && c <= 'Z'){
					printf("Starting new header at position %zu\n", i);
					if (request->header_count >= 20){
						state = ERROR;
						break;
					}

					Header *header = &request->headers[request->header_count];
					header->key = &buf[i];
					header->key_len = 0;
					header->value = NULL;
					header->value_len = 0;
					state = HEADER_KEY;
					parser->line_start = &buf[i];
					c = buf[i];
					FALLTHROUGH;
				} else{
					state = ERROR;
					break;
				}
			
			case HEADER_KEY:
				{
					Header *header = &request->headers[request->header_count];
					if (c == ':'){
						i++; // skip colon
						if (i < buf_total && buf[i] == ' '){
							i++;
						}
						if (i >= buf_total){
							break;
						}
						header->value = &buf[i];
						header->value_len = 0;
						state = HEADER_VALUE;
						FALLTHROUGH;
					} else{
						header->key_len++;
						break;
					}
				}

			case HEADER_VALUE:
				{
					Header *header = &request->headers[request->header_count];
					if (c == '\r'){
						printf("Header: %.*s = %.*s\n", (int)header->key_len, header->key, (int)header->value_len, header->value);
						/* 
						 * check if this value belongs to 'Content-Length' and
						 * set parser->body_len to this value if it does
						 */
						if (header->key_len == strlen("Content-Length") && strncasecmp(header->key, "Content-Length", strlen("Content-Length")) == 0){
							parser->body_len = 0;
							for (size_t j = 0;j < header->value_len; j++){
								char digit = header->value[j];
								if (digit >= '0' && digit <= '9'){
									parser->body_len = parser->body_len * 10 + (digit - '0');
								}
							}
						}
						request->header_count++;
						state = CR;
						c = buf[i];
						break; // Need to go back to CR, not BODY
					} else{
						header->value_len++;
						break;
					}
				}

			case BODY:
				{
					size_t left_in_buf = buf_total - i;
					printf("left_in_buf = buf_total (%zu) - i (%zu) = %zu\n", buf_total, i, left_in_buf);

					size_t left_to_read = parser->body_len - parser->body_bytes_read;
					printf("parser->body_len = %zu, parser->body_bytes_read = %zu\n", parser->body_len , parser->body_bytes_read);

					size_t bytes_to_read = (left_in_buf >= left_to_read)? left_in_buf : left_to_read;
					printf("bytes_to_read = %zu\n", bytes_to_read); 

					parser->body_bytes_read += bytes_to_read;
					i += bytes_to_read - 1; // -1 since we add 1 in the loop
					if (parser->body_bytes_read >= parser->body_len){
						state = DONE;
					}
					break;
				}
			
			case ERROR:
				parser->position = buf_total;
				parser->state = state;
				return PARSE_ERROR;
			
			case DONE:
				break;
		}
		printf("End of iteration %zu, state = %d\n", i, state);
	}
	printf("Exited loop, final state = %d\n", state);
	parser->position = buf_total;
	parser->state = state;

	if (parser->state == DONE){
		return PARSE_SUCCESS;
	}

	return PARSE_INCOMPLETE;
}

int main(void){
    HttpRequest request = {0};
    Parser parser = {0};
    const char *buf =
	    "POST /api/users HTTP/1.1\r\n"
	    "Host: localhost\r\n"
	    //"Content-Length: 25\r\n"
	    "Content-Length: 44\r\n"
	    "\r\n"
	    //"{\"name\":\"John\", \"age\":30}";
	    "{\"name\":\"John\", \"age\":30\", \"gender\":\"male\"}";

    size_t buf_total = strlen(buf);
    int result = parse_http_request(&request, &parser, buf, buf_total); 

    printf("Result: %d\n", result);
    printf("State: %d\n", parser.state);
    printf("Position: %zu / %zu bytes\n", parser.position, buf_total);
    printf("Method: %.*s\n", (int)parser.method_len, parser.method_start);
    printf("Path: %.*s\n", (int)parser.path_len, parser.path_start);
    printf("Body (%ld bytes): %.*s\n", parser.body_len, (int)parser.body_bytes_read, request.body);
    printf("Buffer: [%s]\n", buf);
    printf("Buffer length: %zu\n", strlen(buf));

    return 0;
}
