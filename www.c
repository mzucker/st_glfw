#include "www.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ST_GLFW_USE_CURL
#include <curl/curl.h>
#endif

size_t write_response(void *ptr, size_t size, size_t nmemb, void * b) {

    buffer_t* buf = (buffer_t*)b;

    buf_append_mem(buf, ptr, size*nmemb, BUF_RAW_APPEND);
    
    return size * nmemb;
    
}

//////////////////////////////////////////////////////////////////////

void fetch_url(const char* url, buffer_t* buf) {

#ifndef ST_GLFW_USE_CURL

    fprintf(stderr, "not compiled with curl support, can't fetching URL!");
    exit(1);

#else
    
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl = curl_easy_init();

    if (!curl) {
        fprintf(stderr, "error initting curl!\n");
        exit(1);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);

    printf("fetching %s...\n", url);
    
    int status = curl_easy_perform(curl);

    if (status != 0) {
        fprintf(stderr, "curl error %s\n", curl_easy_strerror(status));
        exit(1);
    }

    long code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (code != 200) {
        fprintf(stderr, "server responded with code %ld\n", code);
        exit(1);
    }

    printf("  ...retrieved data of length %d\n", (int)buf->size);

#endif
    
} 


//////////////////////////////////////////////////////////////////////


json_t* jsobject(const json_t* object,
                  const char* key,
                  int type) {

    json_t* j = json_object_get(object, key);
    
    if (!j) {
        fprintf(stderr, "JSON error: JSON key not found: %s\n", key);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "JSON error: incorrect type for %s in JSON\n", key);
        exit(1);
    }

    return j;
    
}

//////////////////////////////////////////////////////////////////////

const char* jsobject_string(const json_t* object,
                             const char* key) {

    json_t* j = jsobject(object, key, JSON_STRING);
    return json_string_value(j);
    
}

//////////////////////////////////////////////////////////////////////

json_t* jsobject_first(const json_t* object,
                       const char** keys,
                       int type,
                       int* idx) {

    for (int i=0; keys[i]; ++i) {

        json_t* j = json_object_get(object, keys[i]);
        if (!j) { continue; }
        
        if (json_typeof(j) != type) {
            fprintf(stderr, "JSON error: incorrect type for %s in JSON\n", keys[i]);
            exit(1);
        }

        if (idx) { *idx = i; }
        return j;
        
    }

    fprintf(stderr, "JSON error: none of the keys (");
    for (int i=0; keys[i]; ++i) {
        if (i)  { fprintf(stderr, ", "); }
        fprintf(stderr, "%s", keys[i]);
    }
    fprintf(stderr, ") was found\n");
    exit(1);

}

//////////////////////////////////////////////////////////////////////

const char* jsobject_first_string(const json_t* object,
                                  const char** keys,
                                  int* idx) {

    json_t* j = jsobject_first(object, keys, JSON_STRING, idx);

    return json_string_value(j);

}

//////////////////////////////////////////////////////////////////////

int jsobject_integer(const json_t* object,
                     const char* key) {

    json_t* j = jsobject(object, key, JSON_INTEGER);
    return json_integer_value(j);
    
}

//////////////////////////////////////////////////////////////////////

json_t* jsarray(const json_t* array,
                int idx,
                int type) {

    json_t* j = json_array_get(array, idx);
    
    if (!j) {
        fprintf(stderr, "JSON error: array item %d not found in JSON\n", idx);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "JSON error: incorrect type for array item %d in JSON\n", idx);
        exit(1);
    }

    return j;
    
}

//////////////////////////////////////////////////////////////////////

json_t* jsparse(const buffer_t* buf) {
    
    json_error_t error;

    const char* data = buf->data;
    size_t size = buf->size;

    // check for UTF-8 BOM :(
    if (size > 3 && memcmp("\xef\xbb\xbf", data, 3) == 0) {
        fprintf(stderr, "JSON has utf-8 BOM!\n");
        size -= 3;
        data += 3;
    }

    json_t* json_root = json_loadb(data, size, 0, &error);
    
    if (!json_root) {
        fprintf(stderr, "JSON error: on line %d: %s\n", error.line, error.text);
        exit(1);
    }

    if (!json_is_object(json_root)) {
        fprintf(stderr, "JSON error: expected JSON root to be object!\n");
        exit(1);
    }

    return json_root;

}
