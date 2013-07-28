#pragma once

#include <cmath>

bool eq(double a, double b) {
	constexpr double epsilon = 0.0000001;
	return std::fabs(a - b) < epsilon;
}

#define LUA() lua_State *L = luaL_newstate(); luaL_openlibs(L)
#define END() lua_close(L)
#define DO(s)							\
do {								\
	int fail = luaL_dostring(L, s);				\
	if (fail) {						\
		STF_ERRORF("%s", lua_tostring(L, -1));		\
		lua_pop(L, 1);					\
	}							\
} while (0)
