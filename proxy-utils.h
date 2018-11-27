
#include <stdio.h>

#ifndef _PROXY_UTILS_H
#define _PROXY_UTILS_H

#ifdef _PROXY_DEBUG
#define PROXY_DEBUG(msg) \
  fprintf(stderr, "%s(%d): %s\n", __PRETTY_FUNCTION__, __LINE__, msg);
#else
#define PROXY_DEBUG(msg)
#endif

#endif
