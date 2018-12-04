
#include <stdio.h>

#ifndef _PROXY_UTILS_H
#define _PROXY_UTILS_H

void proxy_error(int err, const char* format, ...);

void proxy_log(const char* format, ...);

#endif
