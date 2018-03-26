#ifndef _REQUIRE_H_
#define _REQUIRE_H_

#define require(x) do { if (!(x)) { _require_fail(__FILE__, __LINE__, #x); } } while (0)

void _require_fail(const char* file, int line, const char* what);

#endif
