#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "../src/parser.h"

void test_basic_request(){
	const char *buf = 
		"GET /index.html HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"\r\n";

	HttpRequest request = {0};
	Parser parser = {0};

	int result = parse_http_request(
		&request,
		&parser,
		buf,
		strlen(buf)
		);

	assert(result == 0);
	assert(strncmp(parser.method_start, "GET", parser.method_len) == 0);
	assert(strncmp(parser.path_start, "/index.html", parser.path_len) == 0);
	printf("test_basic_result passed\n");
}

void test_basic_post(){
	const char *buf = 
		"POST /login.html HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 4\r\n"
		"\r\n"
		"Mark\r\n";

	HttpRequest request = {0};
	Parser parser = {0};

	int result = parse_http_request(
		&request,
		&parser,
		buf,
		strlen(buf)
		);

	assert(result == 0);
	assert(strncmp(parser.method_start, "POST", parser.method_len) == 0);
	assert(strncmp(parser.path_start, "/login.html", parser.path_len) == 0);
	assert(strncmp(request.body, "Mark", request.body_len) == 0);
	printf("test_basic_post passed\n");
}

void test_transfer_encoding(){
	const char *buf = 
		"POST /login.html HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"B\r\n"
		"Mark Mwangi\r\n"
		"4\r\n"
		"Mark\r\n"
		"0\r\n"
		"\r\n";

	HttpRequest request = {0};
	Parser parser = {0};

	int result = parse_http_request(
		&request,
		&parser,
		buf,
		strlen(buf)
		);

	assert(result == 0);
	assert(strncmp(parser.method_start, "POST", parser.method_len) == 0);
	assert(strncmp(parser.path_start, "/login.html", parser.path_len) == 0);
	assert(parser.flags & F_CHUNKED); 
	assert(!(parser.flags & F_CONTENT_LENGTH)); 
	assert(parser.chunk_size == 0);
	printf("test_transfer_encoding passed\n");
}

int main(void){
	test_basic_request();
	test_basic_post();
	test_transfer_encoding();

	printf("All tests passed\n");
}


