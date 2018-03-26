#include "www.h"
#include <curl/curl.h>

size_t write_response(void *ptr, size_t size, size_t nmemb, void * b) {

    buffer_t* buf = (buffer_t*)b;

    buf_append(buf, ptr, size*nmemb);
    
    return size * nmemb;
    
}

//////////////////////////////////////////////////////////////////////

void fetch_url(const char* url, buffer_t* buf) {

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

} 

//////////////////////////////////////////////////////////////////////

json_t* jsobject(const json_t* object,
                  const char* key,
                  int type) {

    json_t* j = json_object_get(object, key);
    
    if (!j) {
        fprintf(stderr, "error: JSON key not found: %s\n", key);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "error: incorrect type for %s in JSON\n", key);
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
        fprintf(stderr, "error: array item %d not found in JSON\n", idx);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "error: incorrect type for array item %d in JSON\n", idx);
        exit(1);
    }

    return j;
    
}

//////////////////////////////////////////////////////////////////////

json_t* jsparse(const buffer_t* buf) {
    
    json_error_t error;

    json_t* json_root = json_loadb(buf->data, buf->size, 0, &error);
    
    if (!json_root) {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
        exit(1);
    }

    if (!json_is_object(json_root)) {
        fprintf(stderr, "expected JSON root to be object!\n");
        exit(1);
    }

    return json_root;

}