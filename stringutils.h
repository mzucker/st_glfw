#ifndef _STRINGUTILS_H_
#define _STRINGUTILS_H_

typedef struct enum_info {
    const char* string;
    int value;
} enum_info_t;

int lookup_enum(const enum_info_t* enums, const char* value);



const char* get_extension(const char* filename);


#endif
