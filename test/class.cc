#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

STF_SUITE_NAME("class")

struct Vec3 {
	int x;
	int y;
	int z;

	Vec3(int x, int y, int z): x(x), y(y), z(z) {}
	bool operator==(const Vec3 &r) const {
		return x == r.x &&
			y == r.y &&
			z == r.z;
	}
};

STF_TEST("basic random use") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Vec3>("Vec3").
			Constructor<int, int, int>().
			Variable("x", &Vec3::x).
			Variable("y", &Vec3::y).
			Variable("z", &Vec3::z).
		End().
	End();
	const char *init = R"*****(
		function getvec()
			return Vec3(2, 4, 6)
		end
		function vectest(v)
			x = v.x
			y = v.y
			z = v.z
		end
		x = 0
		y = 0
		z = 0
	)*****";
	DO(init);
	{
		auto vectest = InterLua::Global(L, "vectest");
		auto getvec = InterLua::Global(L, "getvec");

		Vec3 v = getvec();
		vectest(v);

		auto x = InterLua::Global(L, "x");
		STF_ASSERT(x == 2);
		auto y = InterLua::Global(L, "y");
		STF_ASSERT(y == 4);
		auto z = InterLua::Global(L, "z");
		STF_ASSERT(z == 6);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

void const_to_nonconst_ref(InterLua::Ref v) {
	v.As<Vec3&>();
}

void const_to_nonconst_ptr(InterLua::Ref v) {
	v.As<Vec3*>();
}

STF_TEST("const protection") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Vec3>("Vec3").
			Constructor<int, int, int>().
			Variable("x", &Vec3::x).
			Variable("y", &Vec3::y).
			Variable("z", &Vec3::z).
		End().
		Function("const_to_nonconst_ref", const_to_nonconst_ref).
		Function("const_to_nonconst_ptr", const_to_nonconst_ptr).
	End();
	const char *init = R"*****(
		function mutate(v)
			v.x = 5
			v.y = 6
			v.z = 7
		end
		function mutate2(v)
			v.x = -1
			v.y = -2
			v.z = -3
		end
		function retself(v)
			return v
		end
	)*****";
	DO(init);
	{
		auto mutate = InterLua::Global(L, "mutate");
		auto mutate2 = InterLua::Global(L, "mutate2");
		auto retself = InterLua::Global(L, "retself");
		Vec3 orig {1, 2, 3};
		Vec3 v = orig;

		mutate(v); // makes a copy
		STF_ASSERT(orig == v);

		mutate((Vec3&)v); // same as above, just checking
		STF_ASSERT(orig == v);

		mutate((const Vec3&)v); // makes a copy
		STF_ASSERT(orig == v);

		orig = {5, 6, 7};
		mutate(&v); // mutating, but ok
		STF_ASSERT(orig == v);

		// should fail, but vector stays immutable
		InterLua::Error err;
		mutate2((const Vec3*)&v, &err);
		STF_ASSERT(err);
		STF_ASSERT(orig == v);

		// let's test return values as well
		Vec3 copy = retself(&v);
		STF_ASSERT(copy == v);
		STF_ASSERT(&copy != &v);

		Vec3 &ref = retself(&v).As<Vec3&>();
		STF_ASSERT(ref == v);
		STF_ASSERT(&ref == &v);

		const Vec3 &cref = retself(&v).As<const Vec3&>();
		STF_ASSERT(cref == v);
		STF_ASSERT(&cref == &v);

		Vec3 *p = retself(&v);
		STF_ASSERT(*p == v);
		STF_ASSERT(p == &v);

		const Vec3 *cp = retself(&v);
		STF_ASSERT(*cp == v);
		STF_ASSERT(cp == &v);

		// (fails as it should)
		{
			InterLua::Error err;
			InterLua::Global(L, "const_to_nonconst_ref")((const Vec3*)&v, &err);
			STF_ASSERT(err);
		}
		{
			InterLua::Error err;
			InterLua::Global(L, "const_to_nonconst_ptr")((const Vec3*)&v, &err);
			STF_ASSERT(err);
		}
		// Vec3 &ref2 = retself((const Vec3*)&v).As<Vec3&>();
		// STF_ASSERT(ref2 == v);
		// STF_ASSERT(&ref2 == &v);
		// Vec3 *p2 = retself((const Vec3*)&v);
		// STF_ASSERT(*p2 == v);
		// STF_ASSERT(p2 == &v);

		const Vec3 &cref2 = retself((const Vec3*)&v).As<const Vec3&>();
		STF_ASSERT(cref2 == v);
		STF_ASSERT(&cref2 == &v);

		const Vec3 *cp2 = retself((const Vec3*)&v);
		STF_ASSERT(*cp2 == v);
		STF_ASSERT(cp2 == &v);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Foo {
	int foo;

	Foo(int value): foo(value) {}
	int get_foo() const { return foo; }
	void set_foo(int value) { foo = value; }
};

void proxy_set_foo(Foo *foo, int value) {
	foo->foo = value;
}

int proxy_get_foo(const Foo *foo) {
	return foo->foo;
}

STF_TEST("properties") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Foo>("Foo").
			Constructor<int>().
			Property("foo", &Foo::get_foo, &Foo::set_foo).
			Property("foo_ro", &Foo::get_foo).
		End().
	End();
	DO("f = Foo(3); f.foo = f.foo + 4");
	{
		Foo f = InterLua::Global(L, "f");
		STF_ASSERT(f.foo == 7);
	}
	DO("f = Foo(-1); f.foo = f.foo_ro - 2");
	{
		Foo f = InterLua::Global(L, "f");
		STF_ASSERT(f.foo == -3);
	}
	int fail = luaL_dostring(L, "f = Foo(100); f.foo_ro = 500");
	if (!fail) {
		STF_ERRORF("R/O property should report an error on write access");
	} else {
		lua_pop(L, 1);
	}
	const char *init = R"*****(
		function mutate(f)
			f.foo = 10
		end
		function mutate2(f)
			f.foo = 20
		end
	)*****";
	DO(init);
	{
		auto mutate = InterLua::Global(L, "mutate");
		auto mutate2 = InterLua::Global(L, "mutate2");

		Foo f{5};
		mutate(f); // makes a copy
		STF_ASSERT(f.foo == 5);

		mutate((Foo&)f); // same as above, just checking
		STF_ASSERT(f.foo == 5);

		mutate((const Foo&)f); // makes a copy
		STF_ASSERT(f.foo == 5);

		mutate(&f); // mutating, but ok
		STF_ASSERT(f.foo == 10);

		// should fail, but foo stays immutable
		InterLua::Error err;
		mutate2((const Foo*)&f, &err);
		STF_ASSERT(err);
		STF_ASSERT(f.foo == 10);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

STF_TEST("proxy properties") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Foo>("Foo").
			Constructor<int>().
			Property("foo", proxy_get_foo, proxy_set_foo).
		End().
	End();
	DO("f = Foo(3); f.foo = f.foo + 4");
	{
		Foo f = InterLua::Global(L, "f");
		STF_ASSERT(f.foo == 7);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}

struct Storage {
	int i;
	float f;
	double d;
	void store_int(int i) {
		this->i = i;
	}
	void store_float(float f) {
		this->f = f;
	}
	void store_double(double d) {
		this->d = d;
	}
	int get_int() const { return i; }
	float get_float() const { return f; }
	double get_double() const { return d; }
};

bool Examine(const Storage *s) {
	return (
		s->i == 5 &&
		eq(s->f, 6) &&
		eq(s->d, 7)
	);
}

STF_TEST("methods") {
	LUA();
	InterLua::GlobalNamespace(L).
		Class<Storage>("Storage").
			Constructor().
			Function("store_int", &Storage::store_int).
			Function("store_float", &Storage::store_float).
			Function("store_double", &Storage::store_double).
			Function("get_int", &Storage::get_int).
			Function("get_float", &Storage::get_float).
			Function("get_double", &Storage::get_double).
		End().
		Function("examine", Examine).
	End();
	const char *init = R"*****(
		s = Storage()
		s:store_int(7)
		s:store_float(7)
		s:store_double(7)
		s:store_int(s:get_int() - 2)
		s:store_float(s:get_float() - 1)
		s:store_double(s:get_double() - 0)
		ok = examine(s)
	)*****";
	DO(init);
	{
		bool ok = InterLua::Global(L, "ok");
		STF_ASSERT(ok);
	}
	STF_ASSERT(lua_gettop(L) == 0);
	END();
}
