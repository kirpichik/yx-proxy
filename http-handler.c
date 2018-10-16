
#include "http-handler.h"

void http_init_handler(int socket, http_handler_t* result) {

}

bool http_init_client_initial_line(char method, char* uri, client_initial_line_t* result) {
  return false;
}

void http_init_server_initial_line(char status, server_initial_line_t* result) {

}

bool http_init_headler_line(char* key, char* value, header_line_t* result) {
  return false;
}

bool http_read_client_initial_line(http_handler_t* handler, client_initial_line_t* result) {
  return false;
}

bool http_read_server_initial_line(http_handler_t* handler, server_initial_line_t* result) {
  return false;
}

int http_read_header_line(http_handler_t* handler, header_line_t* result) {
  return -1;
}

bool http_write_client_initial_line(http_handler_t* handler, client_initial_line_t* output) {
  return false;
}

bool http_write_server_initial_line(http_handler_t* handler, server_initial_line_t* output) {
  return false;
}

bool http_write_header_line(http_handler_t* handler, header_line_t* output) {
  return false;
}

bool http_write_body_begin(http_handler_t* handler) {
  return false;
}

