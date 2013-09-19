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
			assert(class.__type == "Dummy")
			assert(class.__index ~= nil)
			assert(class.__newindex ~= nil)
			assert(class.__const ~= nil)
			const = class.__const
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

	static DerivedClass new_inst() {
		return {};
	}
};

struct AnotherClass {
	int x;
	static AnotherClass new_inst() {
		return {6};
	}
};

void test_get_userdata(int n, lua_State *L) {
	using namespace InterLua;

	DerivedClass d;
	AnotherClass a = {5};

	switch (n) {
	case 1:
		// expect "unregistered base class"
		get_userdata(L, -1, ClassKey<NotRegistered>::Class(), true);
		break;
	case 2:
		// expect "mutable class required"
		StackOps<const DerivedClass*>::Push(L, &d);
		get_userdata(L, -1, ClassKey<BaseClass>::Class(), false);
		break;
	case 3:
		// ok
		StackOps<const DerivedClass*>::Push(L, &d);
		get_userdata(L, -1, ClassKey<BaseClass>::Class(), true);
		break;
	case 4:
		// expect "class mismatch"
		StackOps<const AnotherClass&>::Push(L, a);
		get_userdata(L, -1, ClassKey<BaseClass>::Class(), true);
		break;
	default:
		break;
	}
}

void test_BaseClass(BaseClass*) {
}

InterLua::Ref to_const(InterLua::Ref r, lua_State *L) {
	r.Push(L);
	auto ud = InterLua::get_userdata_typeless(L, -1);
	if (!ud)
		return r;
	ud->SetConst(true);
	if (!lua_getmetatable(L, -1))
		return r;
	InterLua::rawgetfield(L, -1, "__const");
	if (!lua_isnil(L, -1))
		lua_setmetatable(L, -3);
	return r;
}

int new_userdata_garbage(lua_State *L) {
	lua_newuserdata(L, sizeof(int));
	return 1;
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
			StaticFunction("new", &DerivedClass::new_inst).
		End().
		Class<AnotherClass>("Another").
			Variable("x", &AnotherClass::x).
			StaticFunction("new", &AnotherClass::new_inst).
		End().
		Function("test_get_userdata", &test_get_userdata).
		Function("test_BaseClass", &test_BaseClass).
		Function("to_const", &to_const).
		CFunction("new_userdata_garbage", &new_userdata_garbage).
	End();
	const char *init = R"*****(
		function pcall_expect(expect, f, ...)
			local ok, err = pcall(f, ...)
			assert(not ok)
			assert(err:find(expect) ~= nil,
				"expected: [" .. expect .. "], got: [" .. err .. "]")
		end
		pcall_expect("unregistered base class",
			test_get_userdata, 1)
		pcall_expect("mutable class \".-\" required",
			test_get_userdata, 2)
		test_get_userdata(3)
		pcall_expect("type mismatch",
			test_get_userdata, 4)
		pcall_expect("not userdata",
			test_BaseClass, 5)
		pcall_expect("not userdata",
			test_BaseClass, "123")
		pcall_expect("type mismatch",
			test_BaseClass, Another.new())
		test_BaseClass(Derived.new())
		pcall_expect("mutable class \".-\" required",
			test_BaseClass, to_const(Derived.new()))
		pcall_expect("foreign userdata",
			test_BaseClass, new_userdata_garbage())
	)*****";
	DO(init);
	END();
}
