#include "stf.hh"
#include "helpers.hh"
#include "interlua_ext.hh"

STF_SUITE_NAME("ext")

std::tuple<int, int> tuple_foo() {
	return std::make_tuple(7, 42);
}

STF_TEST("tuple") {
	LUA();
	InterLua::GlobalNamespace(L).
		Function("tuple_foo", &tuple_foo).
	End();
	const char *init = R"*****(
		local a, b = tuple_foo()
		assert(a == 7 and b == 42);
	)*****";
	DO(init);
	END();
}
