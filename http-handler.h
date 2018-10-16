
#include <stdbool.h>
#include <stddef.h>

#define HTTP_METHOD_GET 0

#define HTTP_VERSION_1_0 0
#define HTTP_VERSION_1_1 1

#ifndef HTTP_VERSION_DEFAULT
#define HTTP_VERSION_DEFAULT HTTP_VERSION_1_0
#endif

#define HTTP_BUFFER_SIZE 1024

typedef struct http_handler {
  int socket;
  size_t buff_offset;
  size_t buff_len;
  char buffer[HTTP_BUFFER_SIZE];
} http_handler_t;

typedef struct client_initial_line {
  char method;
  char* uri;
  char http_version;
} client_initial_line_t;

typedef struct server_initial_line {
  char http_version;
  int status;
} server_initial_line_t;

typedef struct header_line {
  char* key;
  char* value;
} header_line_t;

/**
 * Inits HTTP connection handler struct.
 *
 * @param socket Connection socket descriptor.
 * @param result Result struct.
 */
void http_init_handler(int socket, http_handler_t* result);

/**
 * Inits client initial line with method and default HTTP protocol version.
 * Also dups uri to result struct.
 *
 * @param method Requested method.
 * @param uri Target URI.
 * @param result Result struct.
 *
 * @return {@code true} if success.
 */
bool http_init_client_initial_line(char method, char* uri, client_initial_line_t* result);

/**
 * Inits server initial line with status and default HTTP protocol version.
 *
 * @param status Result status.
 * @param result Result struct.
 */
void http_init_server_initial_line(char status, server_initial_line_t* result);

/**
 * Dups key and value strings to result structure.
 *
 * @param key Header key.
 * @param value Header value.
 * @param result Result struct.
 *
 * @return {@code true} if success.
 */
bool http_init_headler_line(char* key, char* value, header_line_t* result);

/**
 * Reads client initial line from the socket with the method and
 * protocol version, ending with newline.
 *
 * @param handler HTTP connection handler.
 * @param result Execution result.
 *
 * @return {@code true} if success.
 */
bool http_read_client_initial_line(http_handler_t* handler, client_initial_line_t* result);

/**
 * Reads server initial line from the socket with the method and
 * protocol version, ending with newline.
 *
 * @param handler HTTP connection handler.
 * @param result Execution result.
 *
 * @return {@code true} if success.
 */
bool http_read_server_initial_line(http_handler_t* handler, server_initial_line_t* result);

/**
 * Reads header line from the socket, ending with newline.
 * If in socket buffer, or http_handler buffer next symbol is newline, then
 * this function returns {@code 1}.
 *
 * @param handler HTTP connection handler.
 * @param result Execution result.
 *
 * @return {@code 0} if success {@code 1} if header ends or {@code -1} on error.
 */
int http_read_header_line(http_handler_t* handler, header_line_t* result);

/**
 * Writes client initial line to the socket and ends line with newline.
 *
 * @param handler HTTP connection handler.
 * @param output Socket output.
 *
 * @return {@code true} if success.
 */
bool http_write_client_initial_line(http_handler_t* handler, client_initial_line_t* output);

/**
 * Writes server initial line to the socket and ends line with newline.
 *
 * @param handler HTTP connection handler.
 * @param output Socket output.
 *
 * @return {@code true} if success.
 */
bool http_write_server_initial_line(http_handler_t* handler, server_initial_line_t* output);

/**
 * Writes header line to the socket and ends line with newline.
 *
 * @param handler HTTP connection handler.
 * @param output Socket output.
 *
 * @return {@code true} if success.
 */
bool http_write_header_line(http_handler_t* handler, header_line_t* output);

/**
 * Writes newline symbol to the socket, by that marks the beginning of the body.
 *
 * @param handler HTTP connection handler.
 *
 * @return {@code true} if success.
 */
bool http_write_body_begin(http_handler_t* handler);

/**
 * Free header key and value strings.
 *
 * @param data Target struct.
 */
void http_free_header_line(header_line_t* data);

/**
 * Free client initial line URI string.
 *
 * @param data Target struct.
 */
void http_free_client_initial_line(client_initial_line_t* data);

