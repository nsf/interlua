#include "interlua.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <unordered_map>
#include <memory>

namespace InterLua {

struct or_die_t {};
const or_die_t or_die = {};

} // namespace InterLua

void* operator new(size_t size, const InterLua::or_die_t&) noexcept {
	void *out = operator new(size, std::nothrow);
	if (!out)
		InterLua::die("InterLua: out of memory");
	return out;
}

void *operator new[](size_t size, const InterLua::or_die_t&) noexcept {
	void *out = operator new[](size, std::nothrow);
	if (!out)
		InterLua::die("InterLua: out of memory");
	return out;
}

namespace InterLua {

static std::unique_ptr<char[]> vasprintf(const char *format, va_list va) {
	va_list va2;
	va_copy(va2, va);

	int n = std::vsnprintf(nullptr, 0, format, va2);
	va_end(va2);
	if (n < 0)
		die("null vsnprintf error");
	if (n == 0) {
		char *buf = new (or_die) char[1];
		buf[0] = '\0';
		return std::unique_ptr<char[]>{buf};
	}

	char *buf = new (or_die) char[n+1];
	int nw = std::vsnprintf(buf, n+1, format, va);
	if (n != nw)
		die("vsnprintf failed to write n bytes");

	return std::unique_ptr<char[]>{buf};
}

static std::unique_ptr<char[]> asprintf(const char *format, ...) {
	va_list va;
	va_start(va, format);
	auto s = vasprintf(format, va);
	va_end(va);
	return s;
}

//============================================================================
// class_info
//============================================================================

static_assert(sizeof(uintptr_t) == 4 || sizeof(uintptr_t) == 8,
	"4 or 8 bytes uintptr_t size expected");
constexpr uintptr_t class_info_magic =
	(sizeof(uintptr_t) == 4)
	? 0xA3867EF9
	: 0xA3867EFCE93255CE;

struct class_info {
	uintptr_t magic = class_info_magic;
	void *class_id;
	class_info *parent;
	bool is_const;

	class_info(void *class_id, class_info *parent, bool is_const):
		class_id(class_id),
		parent(parent),
		is_const(is_const)
	{
	}

	bool is_valid() const { return magic == class_info_magic; }
};

static int class_info_gc(lua_State *L) {
	auto cip = (class_info*)lua_touserdata(L, 1);
	cip->~class_info();
	return 0;
}

static void push_class_info(lua_State *L, class_info ci) {
	void *mem = lua_newuserdata(L, sizeof(class_info));
	new (mem) class_info(std::move(ci));
	lua_newtable(L);
	lua_pushcfunction(L, class_info_gc);
	rawsetfield(L, -2, "__gc");
	lua_setmetatable(L, -2);
}

// converts userdata under 'index' to class_info* safely, returns nullptr if
// the userdata is wrong
static class_info *to_class_info(lua_State *L, int index) {
	auto cip = (class_info*)lua_touserdata(L, index);
	if (cip && _interlua_rawlen(L, index) == sizeof(class_info) && cip->is_valid())
		return cip;
	return nullptr;
}

//============================================================================
// misc
//============================================================================

void die(const char *str, ...) {
	va_list va;
	va_start(va, str);
	std::vfprintf(stderr, str, va);
	va_end(va);
	std::fprintf(stderr, "\n");
	std::abort();
}

void stack_dump(lua_State *L) {
      int i;
      int top = lua_gettop(L);
      printf("lua stack dump -------------\n");
      for (i = 1; i <= top; i++) {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t) {
          case LUA_TSTRING:  /* strings */
            printf("string: `%s'", lua_tostring(L, i));
            break;
          case LUA_TBOOLEAN:  /* booleans */
            printf(lua_toboolean(L, i) ? "boolean: true" : "boolean: false");
            break;
          case LUA_TNUMBER:  /* numbers */
            printf("number: %g", lua_tonumber(L, i));
            break;
          default:  /* other values */
            printf("other: %s", lua_typename(L, t));
            break;
        }
        printf(" | ");  /* put a separator */
      }
      printf("\n");  /* end the listing */
      printf("----------------------------\n");
}

static class_info *get_class_info_for(lua_State *L, void *key) {
	if (key == nullptr)
		return nullptr;

	_interlua_rawgetp(L, LUA_REGISTRYINDEX, key);
	if (lua_isnil(L, -1)) {
		die("should never happen");
		return nullptr;
	}

	lua_rawgeti(L, -1, 1);
	auto cip = to_class_info(L, -1);
	lua_pop(L, 2);
	return cip;
}

static int userdata_gc(lua_State *L) {
	auto ud = reinterpret_cast<Userdata*>(lua_touserdata(L, 1));
	ud->~Userdata();
	return 0;
}

static void push_parent_index(lua_State *L, void *key) {
	_interlua_rawgetp(L, LUA_REGISTRYINDEX, key);
	if (lua_isnil(L, -1)) {
		die("should never happen");
	}

	rawgetfield(L, -1, "__index");
	lua_remove(L, -2);
}

void register_class_tables(lua_State *L, const char *name, const class_keys &keys) {
	// TODO: method: .HideMetatable() or .HiddenMetatable()
	// lua_pushnil(L);
	// rawsetfield(L, -2, "__metatable");

	// === CONST METATABLE ===
	lua_newtable(L);

	lua_pushfstring(L, "const %s", name);
	rawsetfield(L, -2, "__type");

	lua_newtable(L);
	if (keys.parent) {
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
		push_parent_index(L, keys.parent->const_key);
		rawsetfield(L, -2, "__index");
	}
	rawsetfield(L, -2, "__index");

	push_class_info(L, {
		keys.const_key,
		keys.parent
			? get_class_info_for(L, keys.parent->const_key)
			: nullptr,
		true,
	});
	lua_rawseti(L, -2, 1);

	// === CLASS METATABLE ===
	lua_newtable(L);

	lua_pushstring(L, name);
	rawsetfield(L, -2, "__type");

	lua_newtable(L);
	if (keys.parent) {
		lua_pushvalue(L, -1);
		lua_setmetatable(L, -2);
		push_parent_index(L, keys.parent->class_key);
		rawsetfield(L, -2, "__index");
	}
	rawsetfield(L, -2, "__index");

	// a pointer to the const table, mutable value can become a const value
	lua_pushvalue(L, -2);
	rawsetfield(L, -2, "__const");

	push_class_info(L, {
		keys.class_key,
		keys.parent
			? get_class_info_for(L, keys.parent->class_key)
			: nullptr,
		false,
	});
	lua_rawseti(L, -2, 1);

	// === STATIC METATABLE ===
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	if (keys.parent) {
		_interlua_rawgetp(L, LUA_REGISTRYINDEX, keys.parent->static_key);
		rawsetfield(L, -2, "__index");
	}

	// a pointer to the class table, we need this in Class registration
	// function to reuse the tables in case if the same class is being
	// registered twice
	lua_pushvalue(L, -2);
	rawsetfield(L, -2, "__class");

	// -- register GC meta methods --

	lua_pushcfunction(L, userdata_gc);
	rawsetfield(L, -3, "__gc");
	lua_pushcfunction(L, userdata_gc);
	rawsetfield(L, -4, "__gc");

	// -- register metatables in the lua registry --
	lua_pushvalue(L, -1);
	_interlua_rawsetp(L, LUA_REGISTRYINDEX, keys.static_key);
	lua_pushvalue(L, -2);
	_interlua_rawsetp(L, LUA_REGISTRYINDEX, keys.class_key);
	lua_pushvalue(L, -3);
	_interlua_rawsetp(L, LUA_REGISTRYINDEX, keys.const_key);
}

NSWrapper NSWrapper::Namespace(const char *name) {
	rawgetfield(L, -1, name);
	if (!lua_isnil(L, -1))
		return {L};

	// pop nil left from the rawgetfield call above
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushvalue(L, -1);
	rawsetfield(L, -3, name);
	return {L};
}

Userdata::~Userdata() {}

// expects 'absidx' metatable on top of the stack
static void get_userdata_error(lua_State *L, int absidx, int idx,
	void *base_class_key, const char *str, Error *err)
{
	int popn = 1; // metatable
	const char *got = nullptr;
	if (lua_isnil(L, -1)) {
		got = lua_typename(L, lua_type(L, absidx));
	} else {
		rawgetfield(L, -1, "__type");
		got = lua_tostring(L, -1);
		popn++; // metatable.__type
	}

	_interlua_rawgetp(L, LUA_REGISTRYINDEX, base_class_key);
	popn++; // base_class_key
	if (lua_isnil(L, -1)) {
		argerror(L, idx, "trying to get an unregistered base class value", err);
		lua_pop(L, popn);
		return;
	}
	rawgetfield(L, -1, "__type");
	popn++; // base_class_key.__type
	const char *expected = lua_tostring(L, -1);
	auto msg = asprintf(str, expected, got);
	argerror(L, idx, msg.get(), err);
	lua_pop(L, popn);
}

Userdata *get_userdata(lua_State *L, int idx,
	void *base_key, void *base_const_key, bool can_be_const, Error *err)
{
	const int absidx = _interlua_absindex(L, idx);

	// is it userdata?
	auto ud = reinterpret_cast<Userdata*>(lua_touserdata(L, idx));
	if (!ud) {
		// not a userdata, let's check if there is some argument at all
		luaL_checkany(L, idx);

		// perhaps just a wrong argument (string, number, etc.)
		lua_pushnil(L);
		get_userdata_error(L, absidx, idx, base_key,
			"not userdata, class \"%s\" expected, got \"%s\" instead", err);
		return nullptr;
	}

	// get the metatable of the 'absidx'
	if (!lua_getmetatable(L, idx)) {
		lua_pushnil(L);
		get_userdata_error(L, absidx, idx, base_key,
			"interlua class \"%s\" expected, got foreign userdata instead", err);
		return nullptr;
	}

	// get class info
	lua_rawgeti(L, -1, 1);
	auto cip = to_class_info(L, -1);
	if (!cip) {
		lua_pushnil(L);
		get_userdata_error(L, absidx, idx, base_key,
			"interlua class \"%s\" expected, got foreign userdata instead", err);
		lua_pop(L, 2); // pop the class_info and the metatable
		return nullptr;
	}

	// at this point we're assuming userdata is ours
	if (cip->is_const && !can_be_const) {
		// if the userdata is const and we need a mutable one, that's
		// an error
		lua_insert(L, -2); // push the metatable on the top of the stack
		get_userdata_error(L, absidx, idx, base_key,
			"mutable class \"%s\" required, got \"%s\" instead", err);
		lua_pop(L, 1); // pop the class_info
		return nullptr;
	}

	if (cip->is_const) {
		base_key = base_const_key;
	}

	for (;;) {
		if (cip->class_id == base_key) {
			lua_pop(L, 2); // pop the class_info and the metatable
			return ud;
		}
		cip = cip->parent;
		if (cip == nullptr) {
			lua_insert(L, -2);
			get_userdata_error(L, absidx, idx, base_key,
				"class mismatch, \"%s\" expected, got \"%s\" instead", err);
			lua_pop(L, 1); // pop the class_info
			return nullptr;
		}
	}
}

Error::~Error() {
	delete[] message;
}

void Error::Reset() {
	delete[] message;
	code = _INTERLUA_OK;
	message = nullptr;
}

void Error::Set(int code, const char *format, ...) {
	Reset();

	this->code = code;
	if (verbosity == Quiet)
		return;

	va_list va;
	va_start(va, format);
	message = vasprintf(format, va).release();
	va_end(va);
}

void AbortError::Set(int code, const char *format, ...) {
	std::fprintf(stderr, "INTERLUA ABORT (%d): ", code);
	va_list va;
	va_start(va, format);
	std::vfprintf(stderr, format, va);
	va_end(va);
	std::fprintf(stderr, "\n");
	std::abort();
}

AbortError DefaultError;

// similar to luaL_where call with level == 1
static std::unique_ptr<char[]> where(lua_State *L) {
	lua_Debug ar;
	if (lua_getstack(L, 1, &ar)) {
		lua_getinfo(L, "Sl", &ar);
		if (ar.currentline > 0) {
			return asprintf("%s:%d: ",
				ar.short_src, ar.currentline);
		}
	}
	return asprintf("");
}

static void tag_error(lua_State *L, int narg, int tag, Error *err) {
	const char *expected = lua_typename(L, tag);
	auto msg = asprintf("%s expected, got %s",
		expected, luaL_typename(L, narg));
	argerror(L, narg, msg.get(), err);
}

void argerror(lua_State *L, int narg, const char *extra, Error *err) {
	if (err->Verbosity() == Quiet) {
		err->Set(LUA_ERRRUN, "");
		return;
	}

	lua_Debug ar;
	auto loc = where(L);
	if (!lua_getstack(L, 0, &ar)) {
		// no stack frame?
		err->Set(LUA_ERRRUN, "%s" "bad argument: #%d (%s)",
			loc.get(), narg, extra);
		return;
	}
	lua_getinfo(L, "n", &ar);
	if (strcmp(ar.namewhat, "method") == 0) {
		narg--; // do not count 'self'
		if (narg == 0) {
			err->Set(LUA_ERRRUN, "%s" "calling " LUA_QS " on bad self (%s)",
				loc.get(), ar.name, extra);
			return;
		}
	}
	if (ar.name == nullptr)
		ar.name = "?";
	err->Set(LUA_ERRRUN, "%s" "bad argument #%d to " LUA_QS " (%s)",
		loc.get(), narg, ar.name, extra);
}

void checkany(lua_State *L, int narg, Error *err) {
	if (lua_type(L, narg) == LUA_TNONE)
		argerror(L, narg, "value expected", err);
}

void checkinteger(lua_State *L, int narg, Error *err) {
#if LUA_VERSION_NUM < 502
	lua_Integer d = lua_tointeger(L, narg);
	if (d == 0 && !lua_isnumber(L, narg))
		tag_error(L, narg, LUA_TNUMBER, err);
#else
	int isnum;
	lua_tointegerx(L, narg, &isnum);
	if (!isnum)
		tag_error(L, narg, LUA_TNUMBER, err);
#endif
}

void checknumber(lua_State *L, int narg, Error *err) {
#if LUA_VERSION_NUM < 502
	lua_Number d = lua_tonumber(L, narg);
	if (d == 0 && !lua_isnumber(L, narg))
		tag_error(L, narg, LUA_TNUMBER, err);
#else
	int isnum;
	lua_tonumberx(L, narg, &isnum);
	if (!isnum)
		tag_error(L, narg, LUA_TNUMBER, err);
#endif
}

void checkstring(lua_State *L, int narg, Error *err) {
	const char *s = lua_tolstring(L, narg, nullptr);
	if (!s)
		tag_error(L, narg, LUA_TSTRING, err);
}

void ManualError::LJCheckAndDestroy(lua_State *L) {
	Error *err = Get();
	if (*err) {
		lua_pushstring(L, err->What());
		Destroy();
		lua_error(L);
	} else {
		Destroy();
	}
}

} // namespace InterLua
