#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"
#include <cstring>
#include <cmath>

STF_SUITE_NAME("function")

int tester = 0;

bool equals(double a, double b) {
	constexpr double epsilon = 0.0000001;
	return fabs(a - b) < epsilon;
}

void func_noargs_noreturn() {
	tester = 1;
}

void func_int_noreturn(int value) {
	tester = value;
}

int func_noargs_int() {
	return tester;
}

void func_argtypes(int i, float f, double d, const char *cstr, InterLua::Ref r) {
	if (
		i == 123 &&
		equals(f, 3.1415) &&
		equals(d, -3.1415) &&
		strcmp(cstr, "hello") == 0 &&
		r.IsNil()
	) {
		tester = 3;
	}
}

int func_returnint() {
	return 42;
}

float func_returnfloat() {
	return 3.1415;
}

double func_returndouble() {
	return -3.1415;
}

const char *func_returncstr() {
	return "world";
}

InterLua::Ref func_returnluaref(lua_State *L) {
	auto t = InterLua::NewTable(L);
	t.Append(10);
	t.Append(20);
	t.Append(30);
	return t;
}

STF_TEST("global namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Function("test1", func_noargs_noreturn).
		Function("test2", func_int_noreturn).
		Function("test3", func_noargs_int).
	End();
	DO("test1()");
	STF_ASSERT(tester == 1);
	DO("test2(test3()+1)");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("local namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			Function("test1", func_noargs_noreturn).
			Function("test2", func_int_noreturn).
			Function("test3", func_noargs_int).
		End().
	End();
	DO("test.test1()");
	STF_ASSERT(tester == 1);
	DO("test.test2(test.test3()+1)");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("nested namespace") {
	LUA();
	InterLua::GlobalNamespace(L).
		Namespace("test").
			Namespace("foo").
				Function("test1", func_noargs_noreturn).
				Function("test2", func_int_noreturn).
				Function("test3", func_noargs_int).
			End().
		End().
	End();
	DO("test.foo.test1()");
	STF_ASSERT(tester == 1);
	DO("test.foo.test2(test.foo.test3()+1)");
	STF_ASSERT(tester == 2);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("argument types") {
	LUA();
	InterLua::GlobalNamespace(L).
		Function("callme", func_argtypes).
	End();
	DO(R"(callme(123, 3.1415, -3.1415, "hello", nil))");
	STF_ASSERT(tester == 3);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("automatic argument conversion") {
	LUA();
	InterLua::GlobalNamespace(L).
		Function("callme", func_argtypes).
	End();
	DO(R"(callme("123", "3.1415", "-3.1415", "hello", nil))");
	STF_ASSERT(tester == 3);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("return types") {
	LUA();
	InterLua::GlobalNamespace(L).
		Function("getint", func_returnint).
		Function("getfloat", func_returnfloat).
		Function("getdouble", func_returndouble).
		Function("getstring", func_returncstr).
		Function("gettable", func_returnluaref).
		Function("settester", func_int_noreturn).
	End();
	const char init[] = R"*****(
		function eq(a, b)
			local epsilon = 0.0000001
			return math.abs(a - b) < epsilon
		end
		if getint() == 42 and
			eq(getfloat(), 3.1415) and
			eq(getdouble(), -3.1415) and
			getstring() == "world"
		then
			local t = gettable()
			if #t == 3 and
				t[1] == 10 and
				t[2] == 20 and
				t[3] == 30
			then
				settester(4)
			end
		end
	)*****";
	DO(init);
	STF_ASSERT(tester == 4);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Foo {
	static int get_foo() {
		return tester;
	}
	static void set_foo(int value) {
		tester = value;
	}
	static bool test_args_ret(bool a, bool b, bool c) {
		return a && !b && c;
	}
};

struct Bar : Foo {
	static int get_bar() {
		return tester;
	}
	static void set_bar(int value) {
		tester = value;
	}
};


STF_TEST("class") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Foo>("Foo").
			StaticFunction("set_foo", Foo::set_foo).
			StaticFunction("get_foo", Foo::get_foo).
			StaticFunction("test", Foo::test_args_ret).
		End().
		DerivedClass<Bar, Foo>("Bar").
			StaticFunction("set_bar", Bar::set_bar).
			StaticFunction("get_bar", Bar::get_bar).
		End().
	End();
	DO("Foo.set_foo(1)");
	STF_ASSERT(tester == 1);
	DO("Foo.set_foo(Foo.get_foo() + 1)");
	STF_ASSERT(tester == 2);
	DO("Bar.set_bar(Bar.get_bar() + 1)");
	STF_ASSERT(tester == 3);
	DO("Bar.set_foo(Bar.get_foo() + 2)");
	STF_ASSERT(tester == 5);
	DO("if Bar.test(true, false, true) then Bar.set_foo(7) end");
	STF_ASSERT(tester == 7);
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}
