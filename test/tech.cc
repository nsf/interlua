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
	STF_ASSERT(_interlua_compare(L, -1, -2, _INTERLUA_OPEQ));
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
	STF_ASSERT(_interlua_compare(L, -1, -2, _INTERLUA_OPEQ));
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
	STF_ASSERT(_interlua_compare(L, -1, -2, _INTERLUA_OPEQ));
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
		pcall_expect("class mismatch",
			test_get_userdata, 4)
		pcall_expect("not userdata",
			test_BaseClass, 5)
		pcall_expect("not userdata",
			test_BaseClass, "123")
		pcall_expect("class mismatch",
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

static int ref_to_number(const InterLua::Ref &r) {
	if (r.IsNil()) {
		return 0;
	}
	return r;
}

class OOPTester {
public:
	OOPTester() { default_ctor++; }
	OOPTester(const OOPTester&) { copy_ctor++; }
	OOPTester(OOPTester&&) { move_ctor++; }
	~OOPTester() { dtor++; }
	OOPTester &operator=(const OOPTester&) { copy_op++; return *this; }
	OOPTester &operator=(OOPTester&&) { move_op++; return *this; }

	int get() { return 42; }
	int const_get() const { return -42; }

	static int default_ctor;
	static int copy_ctor;
	static int move_ctor;
	static int dtor;
	static int copy_op;
	static int move_op;

	static void reset() {
		default_ctor = 0;
		copy_ctor = 0;
		move_ctor = 0;
		dtor = 0;
		copy_op = 0;
		move_op = 0;
	}

	static void dump() {
		fprintf(stderr, "OOPTester stats ===========\n");
		fprintf(stderr, "default constructor: %d\n", default_ctor);
		fprintf(stderr, "copy constructor: %d\n", copy_ctor);
		fprintf(stderr, "move constructor: %d\n", move_ctor);
		fprintf(stderr, "destructor: %d\n", dtor);
		fprintf(stderr, "copy assignment operator: %d\n", copy_op);
		fprintf(stderr, "move assignment operator: %d\n", move_op);
	}

	static bool assert(InterLua::Ref r) {
		auto dc = r["dc"];
		auto cc = r["cc"];
		auto mc = r["mc"];
		auto dt = r["dt"];
		auto co = r["co"];
		auto mo = r["mo"];
		bool ok = true;
		if (ref_to_number(dc) != default_ctor)
			ok = false;
		if (ref_to_number(cc) != copy_ctor)
			ok = false;
		if (ref_to_number(mc) != move_ctor)
			ok = false;
		if (ref_to_number(dt) != dtor)
			ok = false;
		if (ref_to_number(co) != copy_op)
			ok = false;
		if (ref_to_number(mo) != move_op)
			ok = false;
		if (!ok)
			dump();
		return ok;
	}
};

int OOPTester::default_ctor = 0;
int OOPTester::copy_ctor = 0;
int OOPTester::move_ctor = 0;
int OOPTester::dtor = 0;
int OOPTester::copy_op = 0;
int OOPTester::move_op = 0;

void test_const_stackops_get(lua_State *L) {
	OOPTester x = InterLua::StackOps<OOPTester&>::Get(L, 1);
}

// template <typename T> StackOps
STF_TEST("StackOps<T>") {
	OOPTester::reset();
	using namespace InterLua;
	LUA();
	GlobalNamespace(L).
		Class<OOPTester>("Tester").
			Constructor().
			Function("get", &OOPTester::get).
			StaticFunction("assert", &OOPTester::assert).
		End().
		Function("test_const_stackops_get", &test_const_stackops_get).
	End();

	// ------------ Push tests -------------
	// rvalue ref
	StackOps<OOPTester>::Push(L, {});
	lua_setglobal(L, "a");
	DO("assert(a:get() == 42 and Tester.assert{dc=1, mc=1, dt=1})");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dc=1, mc=1, dt=2})");
	OOPTester::reset();

	// lvalue ref
	OOPTester t;
	StackOps<OOPTester&>::Push(L, t);
	lua_setglobal(L, "a");
	DO("assert(a:get() == 42 and Tester.assert{dc=1, cc=1})");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dc=1, cc=1, dt=1})");
	OOPTester::reset();

	// const lvalue ref
	StackOps<const OOPTester&>::Push(L, t);
	lua_setglobal(L, "a");
	DO("assert(a:get() == 42 and Tester.assert{cc=1})");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{cc=1, dt=1})");
	OOPTester::reset();

	// lvalue ref explicit move (just checking)
	StackOps<OOPTester&&>::Push(L, std::move(t));
	lua_setglobal(L, "a");
	DO("assert(a:get() == 42 and Tester.assert{mc=1})");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{mc=1, dt=1})");
	OOPTester::reset();

	// minor random lua check
	DO("a = Tester()");
	DO("assert(a:get() == 42 and Tester.assert{dc=1})");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dc=1, dt=1})");
	OOPTester::reset();

	// ------------ Get tests -------------
	// rvalue ref doesn't work (expected?)
	// lvalue ref
	StackOps<OOPTester>::Push(L, {});
	OOPTester b = StackOps<OOPTester&>::Get(L, -1);
	DO("assert(Tester.assert{dc=1, cc=1, mc=1, dt=1})");
	OOPTester::reset();

	// lvalue ref, check that it doesn't do anything (silly check?)
	StackOps<OOPTester&>::Get(L, -1);
	DO("assert(Tester.assert{})");

	// const lvalue ref
	OOPTester c = StackOps<const OOPTester&>::Get(L, -1);
	DO("assert(Tester.assert{cc=1})");
	OOPTester::reset();

	// cleanup
	lua_pop(L, 1);
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dt=1})");
	OOPTester::reset();

	// const value to non-const lvalue ref
	StackOps<const OOPTester*>::Push(L, &t);
	lua_setglobal(L, "a");
	const char *testget = R"*****(
		ok, err = pcall(test_const_stackops_get, a)
		assert(not ok)
		assert(err:find("mutable class \".-\" required"))
	)*****";
	DO(testget);
	DO("assert(Tester.assert{})");

	// test Get for plain T, seems like RVO kicks in
	StackOps<OOPTester>::Push(L, {});
	auto d = StackOps<OOPTester>::Get(L, -1);
	DO("assert(Tester.assert{dc=1, dt=1, mc=1, cc=1})");
	END();
}

// template <typename T> StackOps<T*>
STF_TEST("StackOps<T*>") {
	OOPTester::reset();
	using namespace InterLua;
	LUA();
	GlobalNamespace(L).
		Class<OOPTester>("Tester").
			Constructor().
			Function("get", &OOPTester::get).
			Function("const_get", &OOPTester::const_get).
			StaticFunction("assert", &OOPTester::assert).
		End().
	End();

	OOPTester t;

	// T*, passing a pointer doesn't trigger any ctors/dtors
	StackOps<OOPTester*>::Push(L, &t);
	lua_setglobal(L, "a");
	DO("assert(a:get() == 42 and a:const_get() == -42 and Tester.assert{dc=1})");
	DO("assert(a.__type == 'Tester')");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dc=1})"); // gc doesn't do anything as well

	// const T*, non-const get() is not available, typename starts with "const"
	StackOps<const OOPTester*>::Push(L, &t);
	lua_setglobal(L, "a");
	DO("assert(a.get == nil and a:const_get() == -42 and Tester.assert{dc=1})");
	DO("assert(a.__type == 'const Tester')");
	DO("a = nil");
	lua_gc(L, LUA_GCCOLLECT, 0);
	DO("assert(Tester.assert{dc=1})"); // gc doesn't do anything as well
	END();

	// TODO: more to go
}
