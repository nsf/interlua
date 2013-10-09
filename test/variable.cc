#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

STF_SUITE_NAME("variable")

int tester = 0;
float ftester = 0.0f;

void set_tester(int value) {
	tester = value;
}

STF_TEST("global namespace") {
	LUA();
	InterLua::GlobalNamespaceMT(L).
		Function("set", set_tester).
		Variable("tester", &tester).
		Variable("tester_ro", &tester, InterLua::ReadOnly).
	End();
	DO("set(10)");
	STF_ASSERT(tester == 10);
	DO("tester = 5");
	STF_ASSERT(tester == 5);
	DO("set(tester+1)");
	STF_ASSERT(tester == 6);
	DO("set(tester_ro+1)");
	STF_ASSERT(tester == 7);
	int fail = luaL_dostring(L, "tester_ro = 5");
	if (!fail) {
		STF_ERRORF("R/O variable should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("local namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			Function("set", set_tester).
			Variable("tester", &tester).
			Variable("tester_ro", &tester, InterLua::ReadOnly).
		End().
	End();
	DO("test.set(10)");
	STF_ASSERT(tester == 10);
	DO("test.tester = 5");
	STF_ASSERT(tester == 5);
	DO("test.set(test.tester+1)");
	STF_ASSERT(tester == 6);
	DO("test.set(test.tester_ro+1)");
	STF_ASSERT(tester == 7);
	int fail = luaL_dostring(L, "test.tester_ro = 5");
	if (!fail) {
		STF_ERRORF("R/O variable should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Base {
	static int x;
};

struct Derived : Base {
	static int y;
};

int Base::x = 0;
int Derived::y = 0;

STF_TEST("class") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Base>("Base").
			StaticVariable("x", &Base::x).
			StaticVariable("x_ro", &Base::x, InterLua::ReadOnly).
		End().
		DerivedClass<Derived, Base>("Derived").
			StaticVariable("y", &Derived::y, InterLua::ReadOnly).
		End().
	End();
	STF_ASSERT(Derived::x == 0 && Derived::y == 0);
	DO("Base.x = Base.x + 1");
	DO("Derived.x = Derived.x + 1");
	STF_ASSERT(Derived::x == 2);
	int fail = luaL_dostring(L, "Derived.y = 3");
	if (!fail) {
		STF_ERRORF("R/O static variable should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	fail = luaL_dostring(L, "Derived.x_ro = 3");
	if (!fail) {
		STF_ERRORF("R/O static variable should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(Derived::y == 0 && Derived::x == 2);
	Derived::y = 3;
	DO("Derived.x = Derived.x_ro + Derived.y");
	STF_ASSERT(Derived::x == 5);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}
