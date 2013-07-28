#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

STF_SUITE_NAME("cfunction")

int tester = 0;

int test_cfunction1(lua_State*) {
	tester = 1;
	return 0;
}

int test_cfunction2(lua_State*) {
	tester = 2;
	return 0;
}

STF_TEST("global namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		CFunction("test1", test_cfunction1).
		CFunction("test2", test_cfunction2).
	End();
	DO("test1()");
	STF_ASSERT(tester == 1);
	DO("test2()");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("local namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			CFunction("test1", test_cfunction1).
			CFunction("test2", test_cfunction2).
		End().
	End();
	DO("test.test1()");
	STF_ASSERT(tester == 1);
	DO("test.test2()");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("nested namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			Namespace("foo").
				CFunction("test1", test_cfunction1).
				CFunction("test2", test_cfunction2).
			End().
		End().
	End();
	DO("test.foo.test1()");
	STF_ASSERT(tester == 1);
	DO("test.foo.test2()");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Foo {
	static int test_foo(lua_State*) {
		tester = 3;
		return 0;
	}
};

struct Bar : Foo {
	static int test_bar(lua_State*) {
		tester = 4;
		return 0;
	}
};

STF_TEST("class") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Foo>("Foo").
			StaticCFunction("test1", test_cfunction1).
			StaticCFunction("test2", test_cfunction2).
			StaticCFunction("foo", Foo::test_foo).
		End().
		DerivedClass<Bar, Foo>("Bar").
			StaticCFunction("bar", Bar::test_bar).
		End().
	End();
	DO("Foo.test1()");
	STF_ASSERT(tester == 1);
	DO("Foo.test2()");
	STF_ASSERT(tester == 2);
	DO("Foo.foo()");
	STF_ASSERT(tester == 3);
	DO("Bar.bar()");
	STF_ASSERT(tester == 4);
	DO("Bar.foo()");
	STF_ASSERT(tester == 3);
	DO("Bar.test2()");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Baz {
	int value;
	Baz(int value): value(value) {}
	int test(lua_State *L) {
		tester = lua_tointeger(L, 2);
		return 0;
	}
};

STF_TEST("class instance") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Baz>("Baz").
			Constructor<int>().
			CFunction("test", &Baz::test).
		End().
	End();
	DO("b = Baz(13); b:test(13)");
	STF_ASSERT(tester == 13);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}
