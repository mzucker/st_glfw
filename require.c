#include "require.h"
#include <stdio.h>
#include <stdlib.h>

void _require_fail(const char* file, int line, const char* what) {

    fprintf(stderr, "%s:%d: requirement failed: %s\n",
            file, line, what);

    exit(1);
    
}
