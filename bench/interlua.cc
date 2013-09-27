#include "interlua.hh"
#include <cstdio>

//============================================================================
// Set and get
//============================================================================

class SetGet {
	double n = 0.0;
public:
	void set(double n) { this->n = n; }
	double get() const { return this->n; }
};

const char set_and_get[] = R"*****(

local N = 10
local average = 0
local times = 1000000
for i = 0, N do
	local obj = SetGet()
	local t0 = os.clock()
	for i = 1, times do
		obj:set(i)
		if obj:get() ~= i then
			error("failed")
		end
	end
	local dt = os.clock() - t0
	if i ~= 0 then
		average = average + dt
	end
end

print("Getter/setter (average time): " .. average/N)

)*****";

//============================================================================
// Variable set and get
//============================================================================

class VarSetGet {
public:
	double n = 0.0;
};


const char var_set_and_get[] = R"*****(

local N = 10
local average = 0
local times = 1000000
for i = 0, N do
	local obj = VarSetGet()
	local t0 = os.clock()
	for i = 1, times do
		obj.n = i
		if obj.n ~= i then
			error("failed")
		end
	end
	local dt = os.clock() - t0
	if i ~= 0 then
		average = average + dt
	end
end

print("Variable get/set (average time): " .. average/N)

)*****";

//============================================================================
// Derived as Base
//============================================================================

class Base {
	int n = 0;
public:
	virtual ~Base() {}
	void increment_a_base(Base *base) {
		base->n++;
	}

	int get_n() const { return n; }
};

class Derived : public Base {
	int n = 0;
};

const char derived_as_base[] = R"*****(

local N = 10
local average = 0
local times = 1000000
for i = 0, N do
	local obj = Derived()
	local increment_me = Derived()
	local t0 = os.clock()
	for i = 1, times do
		obj:increment_a_base(increment_me)
	end
	local dt = os.clock() - t0
	if i ~= 0 then
		average = average + dt
	end

	assert(obj:get_n() == 0 and increment_me:get_n() == times)
end

print("Derived as base (average time): " .. average/N)

)*****";


//============================================================================
// Memory consumption VarSetGet[100000]
//============================================================================

const char memory_consumption[] = R"*****(

local t = {}
for i = 1, 100000 do
	t[#t+1] = VarSetGet()
end

local n = collectgarbage("count")
print("Memory consumption of 100000 VarSetGet objects: " .. n ..  " kbytes")

)*****";

//============================================================================
// main
//============================================================================

static void dostr(lua_State *L, const char *str) {
	int fail = luaL_dostring(L, str);
	if (fail) {
		fprintf(stderr, "%s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

int main(int, char**) {
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	InterLua::GlobalNamespace(L).
		Class<SetGet>("SetGet").
			Constructor().
			Function("set", &SetGet::set).
			Function("get", &SetGet::get).
		End().
		Class<VarSetGet>("VarSetGet").
			Constructor().
			Variable("n", &VarSetGet::n).
		End().
		Class<Base>("Base").
			Constructor().
			Function("increment_a_base", &Base::increment_a_base).
			Function("get_n", &Base::get_n).
		End().
		DerivedClass<Derived, Base>("Derived").
			Constructor().
		End().
	End();

	dostr(L, set_and_get);
	dostr(L, var_set_and_get);
	dostr(L, derived_as_base);
	//dostr(L, memory_consumption);
	lua_close(L);
}
