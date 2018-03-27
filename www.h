#ifndef _WWW_H_
#define _WWW_H_

#include <jansson.h>
#include "buffer.h"

void fetch_url(const char* url, buffer_t* buf);

json_t* jsparse(const buffer_t* buf);
json_t* jsobject(const json_t* object, const char* key, int type);

json_t* jsobject_first(const json_t* object,
                       const char** keys,
                       int type,
                       int* idx);

const char* jsobject_string(const json_t* object, const char* key);

const char* jsobject_first_string(const json_t* object,
                                  const char** keys,
                                  int* idx);

int jsobject_integer(const json_t* object, const char* key);

json_t* jsarray(const json_t* object, int idx, int type);

#endif
