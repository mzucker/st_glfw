#include "stringutils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const char* get_extension(const char* filename) {

    const char* extension = strrchr(filename, '.');
    if (!extension) { return ""; }

    return extension+1;

}

//////////////////////////////////////////////////////////////////////

int lookup_enum(const enum_info_t* enums, const char* value) {
    
    for (int i=0; enums[i].string; ++i) {

        if (!strcmp(enums[i].string, value)) {
            return enums[i].value;
        }
        
    }

    fprintf(stderr, "error: no matching value for %s\n", value);
    exit(1);
    
}
