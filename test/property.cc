#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

STF_SUITE_NAME("property")

int tester = 0;
float ftester = 0.0f;

void set_tester(int value) {
	tester = value;
}

int tester_get() {
	return tester;
}

void tester_set(int value) {
	tester = value;
}

float ftester_get() {
	return ftester;
}

void ftester_set(float value) {
	ftester = value;
}

STF_TEST("global namespace") {
	int fail;
	LUA();
	InterLua::GlobalNamespace(L).
		Function("set", set_tester).
		Property("tester", tester_get, tester_set).
		Property("tester_ro", tester_get).
		Property("ftester", ftester_get, ftester_set).
		Property("ftester_ro", ftester_get).
	End();
	DO("set(10)");
	STF_ASSERT(tester == 10);
	DO("tester(5)");
	STF_ASSERT(tester == 5);
	DO("set(tester()+1)");
	STF_ASSERT(tester == 6);
	DO("set(tester_ro()+1)");
	STF_ASSERT(tester == 7);
	fail = luaL_dostring(L, "tester_ro(5)");
	if (!fail) {
		STF_ERRORF("R/O property should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	DO("ftester(3.5)");
	STF_ASSERT(ftester == 3.5f);
	DO("ftester(ftester() + 1)");
	STF_ASSERT(ftester == 4.5f);
	DO("ftester(ftester_ro() + 1)");
	STF_ASSERT(ftester == 5.5f);
	fail = luaL_dostring(L, "ftester_ro(3.1415)");
	if (!fail) {
		STF_ERRORF("R/O property should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("local namespace") {
	int fail;
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			Function("set", set_tester).
			Property("tester", tester_get, tester_set).
			Property("tester_ro", tester_get).
			Property("ftester", ftester_get, ftester_set).
			Property("ftester_ro", ftester_get).
		End().
	End();
	DO("test.set(10)");
	STF_ASSERT(tester == 10);
	DO("test.tester(5)");
	STF_ASSERT(tester == 5);
	DO("test.set(test.tester()+1)");
	STF_ASSERT(tester == 6);
	DO("test.set(test.tester_ro()+1)");
	STF_ASSERT(tester == 7);
	fail = luaL_dostring(L, "test.tester_ro(5)");
	if (!fail) {
		STF_ERRORF("R/O property should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	DO("test.ftester(3.5)");
	STF_ASSERT(ftester == 3.5f);
	DO("test.ftester(test.ftester() + 1)");
	STF_ASSERT(ftester == 4.5f);
	DO("test.ftester(test.ftester_ro() + 1)");
	STF_ASSERT(ftester == 5.5f);
	fail = luaL_dostring(L, "test.ftester_ro(3.1415)");
	if (!fail) {
		STF_ERRORF("R/O property should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Foo {
	static void set_foo(int value) { Foo::value = value; }
	static int get_foo() { return Foo::value; }
	static int value;
};

struct Bar : Foo {
	static void set_bar(int value) { Bar::value = value; }
	static int get_bar() { return Bar::value; }
	static int value;
};

struct Baz : Bar {
	static int get_baz() { return Baz::value; }
	static int value;
};

int Foo::value = 0;
int Bar::value = 0;
int Baz::value = 0;

STF_TEST("class") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Foo>("Foo").
			StaticProperty("foo",  Foo::get_foo, Foo::set_foo).
			StaticVariable("value", &Foo::value).
		End().
		DerivedClass<Bar, Foo>("Bar").
			StaticProperty("bar", Bar::get_bar, Bar::set_bar).
			StaticVariable("value", &Bar::value).
		End().
		DerivedClass<Baz, Bar>("Baz").
			StaticProperty("baz", Baz::get_baz).
			StaticVariable("value", &Baz::value).
		End().
	End();
	const char *code1 = R"*****(
		Foo.foo(5)
		if Bar.value() == 5 then
			Bar.bar(Bar.bar() + 1)
		else
			Bar.bar(Bar.foo() - 6)
		end
		Bar.value(Baz.baz() + Baz.bar())
		Foo.value(Baz.foo() - Baz.value())
	)*****";
	DO(code1);
	STF_ASSERT(Foo::value == 5);
	STF_ASSERT(Bar::value == -1);
	STF_ASSERT(Baz::value == 0);
	int fail = luaL_dostring(L, "Baz.baz(10)");
	if (!fail) {
		STF_ERRORF("R/O static property should report an error on write");
	} else {
		lua_pop(L, 1);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}
