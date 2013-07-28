#include "stf.hh"
#include "helpers.hh"
#include "interlua.hh"

STF_SUITE_NAME("luaref")

STF_TEST("Ref::Is*()") {
	LUA();
	const char init[] = R"*****(
		a = 123
		b = "hello"
		c = nil
		d = {1, 2, 3}
		e = function (n) return n*2 end
	)*****";
	DO(init);
	{
		auto a = InterLua::Global(L, "a");
		auto b = InterLua::Global(L, "b");
		auto c = InterLua::Global(L, "c");
		auto d = InterLua::Global(L, "d");
		auto e = InterLua::Global(L, "e");
		STF_ASSERT(a.IsNumber());
		STF_ASSERT(b.IsString());
		STF_ASSERT(c.IsNil());
		STF_ASSERT(d.IsTable());
		STF_ASSERT(e.IsFunction());
		STF_ASSERT(lua_gettop(L) == 0);
	}
	END();
}

STF_TEST("Ref::operator()") {
	LUA();
	const char init[] = R"*****(
		function add(a, b)
			return a + b
		end
		function bad(a, b)
			error("oops")
		end
		function noreturn()
			-- do nothing
		end
	)*****";
	DO(init);
	{
		auto add = InterLua::Global(L, "add");
		STF_ASSERT(add.IsFunction());
		auto result = add(5, 10);
		STF_ASSERT((int)result == 15);

		auto bad = InterLua::Global(L, "bad");
		STF_ASSERT(!bad.IsNil());
		InterLua::VerboseError err;
		bad(1, 2, &err);
		if (!err) {
			STF_ERRORF("error expected");
		}

		auto noreturn = InterLua::Global(L, "noreturn");
		auto nil = noreturn();
		STF_ASSERT(nil.IsNil());
		STF_ASSERT(lua_gettop(L) == 0);
	}
	END();
}

STF_TEST("Ref::operator[]") {
	LUA();
	const char init[] = R"*****(
		config = {
			resolution = "1440x900",
			vsync = true,
			sensitivity = 0.5,
			player = {
				name = "nsf",
			},
		}

		function check_sensitivity()
			return config.sensitivity > 0.6
		end
	)*****";
	DO(init);
	{
		auto config = InterLua::Global(L, "config");

		const char *res = config["resolution"];
		STF_ASSERT(strcmp(res, "1440x900") == 0);

		bool vsync = config["vsync"];
		STF_ASSERT(vsync == true);

		float sens = config["sensitivity"];
		STF_ASSERT(eq(sens, 0.5));

		const char *name = config["player"]["name"];
		STF_ASSERT(strcmp(name, "nsf") == 0);

		config["sensitivity"] = 0.7;
		auto cs = InterLua::Global(L, "check_sensitivity");
		STF_ASSERT(cs() == true);
		STF_ASSERT(lua_gettop(L) == 0);
	}
	END();
}
