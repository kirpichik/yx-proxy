
#ifndef _PROXY_UTILS_H
#define _PROXY_UTILS_H

#define CHECK_ERROR(value, msg, ret) if (value) { perror(msg); return ret; }
#define CHECK_ERROR_INT(value, msg) CHECK_ERROR(value, msg, -1)
#define CHECK_ERROR_PTR(value, msg) CHECK_ERROR(value, msg, NULL)
#define CHECK_ERROR_BOOL(value, msg) CHECK_ERROR(value, msg, false)

#define CHECK_NULL(target, msg, ret) CHECK_ERROR(target == NULL, msg, ret)
#define CHECK_NULL_INT(target, msg) CHECK_NULL(target, msg, -1)
#define CHECK_NULL_PTR(target, msg) CHECK_NULL(target, msg, NULL)
#define CHECK_NULL_BOOL(target, msg) CHECK_NULL(target, msg, false)

#define CHECK_POSITIVE(target, msg, ret) CHECK_ERROR(target < 0, msg, ret)
#define CHECK_POSITIVE_INT(target, msg) CHECK_POSITIVE(target, msg, -1)
#define CHECK_POSITIVE_PTR(target, msg) CHECK_POSITIVE(target, msg, NULL)
#define CHECK_POSITIVE_BOOL(target, msg) CHECK_POSITIVE(target, msg, false)

#endif

