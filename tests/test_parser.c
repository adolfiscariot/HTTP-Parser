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

void test_content_length(){
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
	printf("test_content_length passed\n");
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


/*
 * ============================================================
 * EDGE CASES
 * ============================================================
 */

void test_empty_body(){
	const char *buf =
	"GET / HTTP/1.1\r\n"
	"\r\n";

	Parser parser = {0};
	HttpRequest req = {0};

	int r = parse_http_request(&req, &parser, buf, strlen(buf));

	assert(r == PARSE_SUCCESS);
	printf("test_empty_body passed\n");
}

void test_large_chunk(){
	const char *buf =
	"POST / HTTP/1.1\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"A\r\n"
	"1234567890\r\n"
	"0\r\n"
	"\r\n";

	Parser parser = {0};
	HttpRequest req = {0};

	int r = parse_http_request(&req, &parser, buf, strlen(buf));

	assert(r == PARSE_SUCCESS);
	printf("test_large_chunk passed\n");
}

/*
 * ============================================================
 * STREAMING (CRITICAL)
 * ============================================================
 */

void test_partial_input(){
	const char *part1 =
	"POST / HTTP/1.1\r\n"
	"Content-Length: 5\r\n"
	"\r\n"
	"He";

	const char *part2 = "llo";

	Parser parser = {0};
	HttpRequest req = {0};

	int r;

	r = parse_http_request(&req, &parser, part1, strlen(part1));
	assert(r == PARSE_INCOMPLETE);

	r = parse_http_request(&req, &parser, part2, strlen(part2));
	assert(r == PARSE_SUCCESS);
	printf("test_partial_input passed\n");
}

/*
 * ============================================================
 * ERROR CASES
 * ============================================================
 */

void test_invalid_chunk_size(){
	const char *buf =
	"POST / HTTP/1.1\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"Z\r\n"   // invalid hex
	"Hello\r\n"
	"0\r\n"
	"\r\n";

	Parser parser = {0};
	HttpRequest req = {0};

	int r = parse_http_request(&req, &parser, buf, strlen(buf));

	assert(r == PARSE_ERROR);
	printf("test_invalid_chunk_size passed\n");
}

void test_missing_crlf(){
	const char *buf =
	"GET / HTTP/1.1\n";  // invalid

	Parser parser = {0};
	HttpRequest req = {0};

	int r = parse_http_request(&req, &parser, buf, strlen(buf));
	printf("%d\n", r);

	assert(r == PARSE_ERROR);
	printf("test_missing_crlf\n");
}

/*
 * ============================================================
 * MAIN
 * ============================================================
 */


int main(void){
	test_basic_request();
	test_content_length();
	test_transfer_encoding();
	test_empty_body();
	test_large_chunk();
	test_partial_input();
	test_invalid_chunk_size();
	test_missing_crlf();

	printf("All tests passed\n");
}


