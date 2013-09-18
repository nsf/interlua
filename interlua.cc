#include "interlua.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

namespace InterLua {

void die(const char *str, ...) {
	va_list va;
	va_start(va, str);
	std::vfprintf(stderr, str, va);
	va_end(va);
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

int read_only_error(lua_State *L) {
	return luaL_error(L, "'%s' is read-only", lua_tostring(L, lua_upvalueindex(1)));
}

int const_read_only_error(lua_State *L) {
	return luaL_error(L, "'%s' is a read-only member of a const class instance",
		lua_tostring(L, lua_upvalueindex(1)));
}

// universal index metamethod, works for namespaces and all kinds of classes
// data the logic is simple:
//   1. get value from metatable if available, if not found proceed to (2)
//   2. get value using __propget function, if not found proceed to (3)
//   3. try to recurse into __parent, if not found return nil
static int index_meta_method(lua_State *L) {
	lua_getmetatable(L, 1); // push metatable of arg1 (++)
	for (;;) {
		lua_pushvalue(L, 2); // push key arg2 (++)
		lua_rawget(L, -2); // key lookup in the metatable (==)
		if (!lua_isnil(L, -1)) {
			// found something directly in the metatable, discard
			// metatable and return the value
			lua_remove(L, -2); // (--)
			return 1;
		}

		// otherwise let's try to find appropriate __propget
		lua_pop(L, 1); // discard nil (--)
		rawgetfield(L, -1, "__propget"); // __propget from metatable (++)
		lua_pushvalue(L, 2); // push key arg2 (++)
		lua_rawget(L, -2); // lookup key in __propget (==)
		lua_remove(L, -2); // discard __propget (--)
		if (lua_iscfunction(L, -1)) {
			lua_remove(L, -2); // discard metatable (--)
			lua_pushvalue(L, 1); // push arg1 (++)
			lua_call(L, 1, 1); // call cfunction
			return 1;
		}

		// not found, let's try parent
		lua_pop(L, 1); // discard nil (--)
		rawgetfield(L, -1, "__parent"); // (++)
		lua_remove(L, -2); // discard child's metatable (--)
		if (lua_isnil(L, -1)) {
			// no parent actually
			return 1;
		}
	}
}

static int newindex_meta_method(lua_State *L) {
	lua_getmetatable(L, 1); // push metatable of arg1 (++)
	for (;;) {
		rawgetfield(L, -1, "__propset"); // lookup __propset in metatable (++)
		lua_pushvalue(L, 2); // push key arg2 (++)
		lua_rawget(L, -2); // lookup key in __propset (==)
		lua_remove(L, -2); // discard __propset (--)
		if (lua_iscfunction(L, -1)) {
			lua_remove(L, -2); // discard metatable (--)
			lua_pushvalue(L, 3); // push new value arg3 (++)
			lua_call(L, 1, 0);
			return 0;
		}

		lua_pop(L, 1); // discard nil (--)
		rawgetfield(L, -1, "__parent"); // (++)
		lua_remove(L, -2); // discard child's metatable (--)
		if (lua_isnil(L, -1)) {
			// no parent, return an error
			lua_pop(L, 1); // discard nil (--)
			return luaL_error(L, "no writable variable '%s'",
				lua_tostring(L, 2));
		}
	}
}

static int class_newindex_meta_method(lua_State *L) {
	lua_getmetatable(L, 1); // push metatable of arg1 (++)
	for (;;) {
		rawgetfield(L, -1, "__propset"); // lookup __propset in metatable (++)
		lua_pushvalue(L, 2); // push key arg2 (++)
		lua_rawget(L, -2); // lookup key in __propset (==)
		lua_remove(L, -2); // discard __propset (--)
		if (lua_iscfunction(L, -1)) {
			lua_remove(L, -2); // discard metatable (--)
			lua_pushvalue(L, 1); // push userdata arg1 (++)
			lua_pushvalue(L, 3); // push new value arg3 (++)
			lua_call(L, 2, 0);
			return 0;
		}

		lua_pop(L, 1); // discard nil (--)
		rawgetfield(L, -1, "__parent"); // (++)
		lua_remove(L, -2); // discard child's metatable (--)
		if (lua_isnil(L, -1)) {
			// no parent, return an error
			lua_pop(L, 1); // discard nil (--)
			return luaL_error(L, "no writable variable '%s'",
				lua_tostring(L, 2));
		}
	}
}

void set_common_metamethods(lua_State *L,
	lua_CFunction index, lua_CFunction newindex)
{
	lua_pushcfunction(L, index);
	rawsetfield(L, -2, "__index");
	lua_pushcfunction(L, newindex);
	rawsetfield(L, -2, "__newindex");
	lua_newtable(L);
	rawsetfield(L, -2, "__propget");
	lua_newtable(L);
	rawsetfield(L, -2, "__propset");
}

void create_class_tables(lua_State *L, const char *name) {
	// TODO: method: .HideMetatable() or .HiddenMetatable()
	// lua_pushnil(L);
	// rawsetfield(L, -2, "__metatable");


	// ----------------------------- const table
	// create a table and make it a metatable of itself
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);

	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "const %s", name);
	tmp[sizeof(tmp)-1] = 0;
	lua_pushstring(L, tmp);
	rawsetfield(L, -2, "__type");

	set_common_metamethods(L, index_meta_method, class_newindex_meta_method);

	// ----------------------------- class table
	// create a table and make it a metatable of itself
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);

	lua_pushstring(L, name);
	rawsetfield(L, -2, "__type");

	set_common_metamethods(L, index_meta_method, class_newindex_meta_method);

	lua_pushvalue(L, -2);
	rawsetfield(L, -2, "__const");

	// ----------------------------- static table
	// create a table and make it a metatable of itself
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);

	set_common_metamethods(L, index_meta_method, newindex_meta_method);

	lua_pushvalue(L, -2);
	rawsetfield(L, -2, "__class");
}

NSWrapper NSWrapper::Namespace(const char *name) {
	rawgetfield(L, -1, name);
	if (!lua_isnil(L, -1))
		return {L};

	// pop nil left from the rawgetfield call above
	lua_pop(L, 1);

	// create an empty table and make it a metatable of itself
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);

	set_common_metamethods(L, index_meta_method, newindex_meta_method);

	// namespace[name] = table
	lua_pushvalue(L, -1);
	rawsetfield(L, -3, name);
	return {L};
}

Userdata::~Userdata() {}

Userdata *get_userdata(lua_State *L, int idx, void *base_class_key, bool can_be_const) {
	int absidx = lua_absindex(L, idx);
	lua_rawgetp(L, LUA_REGISTRYINDEX, base_class_key); // class metatable
	if (lua_isnil(L, -1)) {
		luaL_argerror(L, idx,
			"trying to get an unregistered base class pointer");
		return nullptr;
	}

	// get the metatable of the 'absidx' arg
	lua_getmetatable(L, absidx);

	// let's see if this metatable is a const table, in case if it has
	// "__const", it's not
	rawgetfield(L, -1, "__const");
	bool is_const = lua_isnil(L, -1);
	lua_pop(L, 1);

	if (is_const && !can_be_const) {
		// if the userdata is const and we need a mutable one, that's
		// an error
		luaL_argerror(L, idx, "mutable class required, got const");
		return nullptr;
	}

	// at this point we have
	//  -1 'absidx' metatable
	//  -2 'base_class_key' metatable
	// let's replace 'base_class_key' metatable with a const one if
	// necessary
	if (is_const) {
		rawgetfield(L, -2, "__const");
		lua_replace(L, -3);
	}

	// go up the class hierarchy starting from 'absidx' metatable and see if
	// there are matching metatables
	for (;;) {
		if (lua_rawequal(L, -1, -2)) {
			// got a match
			lua_pop(L, 2);
			return (Userdata*)lua_touserdata(L, absidx);
		}

		// no match, let's try the parent
		rawgetfield(L, -1, "__parent");
		if (lua_isnil(L, -1)) {
			// no parent, means no match
			luaL_argerror(L, idx, "class mismatch");
			return nullptr;
		}
		lua_remove(L, -2); // remove the child metatable
	}
}

Userdata *get_userdata_unchecked(lua_State *L, int index) {
	return (Userdata*)lua_touserdata(L, index);
}

Error::~Error() {}

void Error::Set(int code, const char*) {
	this->code = code;
}

const char *Error::What() const {
	return "";
}

VerboseError::~VerboseError() {
	if (message)
		std::free(message);
}

void VerboseError::Set(int code, const char *msg) {
	this->code = code;

	if (message)
		std::free(message);
	message = (char*)std::malloc(std::strlen(msg)+1);
	if (!message)
		die("malloc failure");
	std::strcpy(message, msg);
}

const char *VerboseError::What() const {
	return message ? message : "";
}

void AbortError::Set(int code, const char *msg) {
	this->code = code;
	std::fprintf(stderr, "PANIC (%d): %s\n", code, msg);
	std::abort();
}

AbortError DefaultError;

} // namespace InterLua