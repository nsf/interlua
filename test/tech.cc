#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

// Technical tests, here I write test cases based on interlua C++ code chunks,
// testing each of them in various ways. InterLua contains a lot of templates,
// testing them isn't trivial, but I do my best.
STF_SUITE_NAME("tech")

// static inline void rawgetfield(lua_State *L, int index, const char *key)
STF_TEST("rawgetfield") {
	LUA();
	const char *init = R"*****(
		table1 = {a = 1, b = 2, c = 3}
		table2 = {d = -1, e = 2, f = -3}
	)*****";
	DO(init);
	lua_getglobal(L, "table1");
	lua_getglobal(L, "table2");
	InterLua::rawgetfield(L, -2, "b");
	InterLua::rawgetfield(L, -2, "e");
	STF_ASSERT(lua_compare(L, -1, -2, LUA_OPEQ));
	END();
}

// static inline void rawsetfield(lua_State *L, int index, const char *key)
STF_TEST("rawsetfield") {
	LUA();
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, 42);
	InterLua::rawsetfield(L, -2, "test");
	lua_pushinteger(L, 42);
	InterLua::rawsetfield(L, -3, "test");

	InterLua::rawgetfield(L, -2, "test");
	InterLua::rawgetfield(L, -2, "test");
	STF_ASSERT(lua_compare(L, -1, -2, LUA_OPEQ));
	END();
}

// class stack_pop
STF_TEST("stack_pop") {
	LUA();
	lua_pushinteger(L, 1);
	lua_pushinteger(L, 1);
	lua_pushinteger(L, 2);
	lua_pushinteger(L, 3);
	{
		InterLua::stack_pop(L, 2);
	}
	STF_ASSERT(lua_gettop(L) == 2);
	STF_ASSERT(lua_compare(L, -1, -2, LUA_OPEQ));
	END();
}

// void create_class_tables(lua_State *L, const char *name)
STF_TEST("create_class_tables") {
	LUA();
	InterLua::create_class_tables(L, "Dummy");
	lua_pushvalue(L, -1);
	lua_setglobal(L, "Dummy");

	const char *init = R"*****(
		function check()
			static = Dummy
			assert(static == getmetatable(static))
			assert(static.__index ~= nil)
			assert(static.__newindex ~= nil)
			assert(static.__class ~= nil)
			class = static.__class
			assert(class == getmetatable(class))
			assert(class.__type == "Dummy")
			assert(class.__index ~= nil)
			assert(class.__newindex ~= nil)
			assert(class.__const ~= nil)
			const = class.__const
			assert(const == getmetatable(const))
			assert(const.__type == "const Dummy")
			assert(const.__index ~= nil)
			assert(const.__newindex ~= nil)
		end
	)*****";
	DO(init);
	{
		InterLua::VerboseError err;
		auto check = InterLua::Global(L, "check");
		check(&err);
		if (err) {
			STF_ERRORF("%s", err.What());
		}
	}
	END();
}

// template <typename T> struct ClassKey
STF_TEST("ClassKey") {
	using namespace InterLua;
	struct X {};
	struct Y {};
	STF_ASSERT(ClassKey<X>::Static() == ClassKey<X>::Static());
	STF_ASSERT(ClassKey<X>::Static() != ClassKey<Y>::Static());
	STF_ASSERT(ClassKey<Y>::Class() == ClassKey<Y>::Class());
	STF_ASSERT(ClassKey<X>::Class() != ClassKey<Y>::Class());
	STF_ASSERT(ClassKey<X>::Const() == ClassKey<X>::Const());
	STF_ASSERT(ClassKey<X>::Const() != ClassKey<Y>::Const());
	STF_ASSERT(ClassKey<X>::Static() != ClassKey<X>::Class());
	STF_ASSERT(ClassKey<X>::Static() != ClassKey<X>::Const());
	STF_ASSERT(ClassKey<X>::Class() != ClassKey<X>::Const());
}

struct NotRegistered {};

struct BaseClass {
	virtual int get_value() { return 1; }
};

struct DerivedClass : BaseClass {
	int get_value() override { return 2; }

	static DerivedClass new_mutable() {
		return {};
	}

	static const DerivedClass new_const() {
		return {};
	}
};

void test_get_userdata(int n, lua_State *L) {
	DerivedClass d;
	using namespace InterLua;
	switch (n) {
	case 1:
		get_userdata(L, -1, ClassKey<NotRegistered>::Class(), true);
		break;
	case 2:
		StackOps<const DerivedClass*>::Push(L, &d);
		get_userdata(L, -1, ClassKey<BaseClass>::Class(), false);
		break;
	default:
		break;
	}
}

// Userdata *get_userdata(lua_State *L, int index, void *base_class_key, bool can_be_const)
STF_TEST("get_userdata") {
	using namespace InterLua;
	LUA();
	GlobalNamespace(L).
		Class<BaseClass>("Base").
			Function("get_value", &BaseClass::get_value).
		End().
		DerivedClass<DerivedClass, BaseClass>("Derived").
			Function("get_value", &DerivedClass::get_value).
		End().
		Function("test_get_userdata", &test_get_userdata).
	End();
	const char *init = R"*****(
		function pcall_expect(expect, f, ...)
			local ok, err = pcall(f, ...)
			assert(not ok)
			assert(err:find(expect) ~= nil)
		end
		pcall_expect("unregistered base class",
			test_get_userdata, 1)
		pcall_expect("mutable class required",
			test_get_userdata, 2)
		test_get_userdata(3)
		test_get_userdata(4)
		test_get_userdata(5)
		test_get_userdata(6)
	)*****";
	DO(init);
	END();
}
