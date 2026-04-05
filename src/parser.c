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
#include "parser.h"

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

// The lookup table below is used to convert size in chunked encoding from hex to their numeric values
static const int8_t unhex[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x00-0x0F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x10-0x1F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x20-0x2F
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,  // 0x30-0x3F ('0'-'9')
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x40-0x4F ('A'-'F')
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x50-0x5F
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x60-0x6F ('a'-'f')
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x70-0x7F
    // Rest (0x80-0xFF) all -1
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

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
				if (c == '\n'){
					state = LF;
					i++;
					c = buf[i];
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
				if (c == '\r'){
					// if CR re-appears after LF that means the body is next
					i++; // skip to \n
					if (i < buf_total && buf[i] == '\n'){
						i++; //skip to beginning of body

						// Handle chunked encoding
						if (parser->flags & F_CHUNKED){
							state = CHUNK_SIZE;
							i--; //Gotta do this cause the main loop increments i by the time we get to chunk size
							break;
						}

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
						/* 
						 * check if this value belongs to 'Content-Length' and
						 * set parser->body_len to this value if it does
						 */
						if (header->key_len == strlen("Content-Length") && strncasecmp(header->key, "Content-Length", strlen("Content-Length")) == 0){
							parser->body_len = 0;
							for (size_t j = 0; j < header->value_len; j++){
								char digit = header->value[j];
								if (digit >= '0' && digit <= '9'){
									parser->body_len = parser->body_len * 10 + (digit - '0');
								}
							}
						}

						// If header == Transfer-Encoding ...
						else if (header->key_len == strlen("Transfer-Encoding") && strncasecmp(header->key, "Transfer-Encoding", strlen("Transfer-Encoding")) == 0){
							// Check if value is chunked
							if (header->value_len == strlen("chunked") && strncasecmp(header->value, "chunked", strlen("chunked")) == 0){
								parser->flags |= F_CHUNKED;
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

					size_t left_to_read = parser->body_len - parser->body_bytes_read;

					size_t bytes_to_read = (left_in_buf >= left_to_read)? left_in_buf : left_to_read;

					parser->body_bytes_read += bytes_to_read;
					i += bytes_to_read - 1; // -1 since we add 1 in the loop
					if (parser->body_bytes_read >= parser->body_len){
						state = DONE;
					}
					break;
				}

		/*
		 * =======================================================================
		 * CHUNKED ENCODING CASES
		 * =======================================================================
		 */
			case CHUNK_SIZE:

				if (parser->state != CHUNK_SIZE){
					parser->chunk_size = 0;
				}

				if (c == ';' || c == ' '){
					state = CHUNK_EXTENSIONS;
					break;
				} else if (c == '\r'){
					state = CHUNK_SIZE_CR;
					break;
				} else{
					int value = unhex[(unsigned char)c];
					if (value == -1){
						state = ERROR;
						break;
					}

					parser->chunk_size = (parser->chunk_size << 4) | value;
					state = CHUNK_SIZE_CR;
					i++;
					break;
				}

			case CHUNK_EXTENSIONS:
				if (c == '\r'){
					state = CHUNK_SIZE_CR;
				} else{
					//ignore extensions. let the characters be skipped.
				}

				break;

			case CHUNK_SIZE_CR:
				if (c == '\n'){
					state = CHUNK_SIZE_LF;
					FALLTHROUGH;
				} else{
					state = ERROR;
					break;
				}

			case CHUNK_SIZE_LF:

				parser->chunk_bytes_read = 0;

				if (parser->chunk_size > 0){
					state = CHUNK_DATA;
				} else if (parser->chunk_size == 0){
					state = CHUNK_TRAILERS;
				} 
				break;

			case CHUNK_DATA:

				if (parser->chunk_bytes_read == 0){
					parser->chunk_data = &buf[i];
				}

				parser->chunk_bytes_read++;
				if (parser->chunk_bytes_read == parser->chunk_size){
					state = CHUNK_DATA_CR;
					//continue;
				}

				break;

			case CHUNK_DATA_CR:
				if (c != '\r'){
					state = ERROR;
					break;
				}

				state = CHUNK_DATA_LF;
				break;

			case CHUNK_DATA_LF:
				if (c != '\n'){
					state = ERROR;
					break;
				}

				state = CHUNK_SIZE;
				break;

			case CHUNK_TRAILERS:
				if (c == '\r'){
					state = CHUNK_TRAILERS_LF;
				} else{
					//ignore trailers. let the characters be skipped.
				}

				break;

			case CHUNK_TRAILERS_LF:
				if (c != '\n'){
					state = ERROR;
					break;
				}

				state = DONE;
				break;

			case ERROR:
				parser->position = buf_total;
				parser->state = state;
				return PARSE_ERROR;
			
			case DONE:
				break;
		}
	}
	parser->position = buf_total;
	parser->state = state;

	if (parser->state == DONE){
		return PARSE_SUCCESS;
	}

	return PARSE_INCOMPLETE;
}

