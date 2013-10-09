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

using cstring = std::unique_ptr<char[]>;

static cstring vasprintf(const char *format, va_list va) {
	va_list va2;
	va_copy(va2, va);

	int n = std::vsnprintf(nullptr, 0, format, va2);
	va_end(va2);
	if (n < 0)
		die("null vsnprintf error");
	if (n == 0) {
		char *buf = new (or_die) char[1];
		buf[0] = '\0';
		return cstring{buf};
	}

	char *buf = new (or_die) char[n+1];
	int nw = std::vsnprintf(buf, n+1, format, va);
	if (n != nw)
		die("vsnprintf failed to write n bytes");

	return cstring{buf};
}

static cstring asprintf(const char *format, ...) {
	va_list va;
	va_start(va, format);
	auto s = vasprintf(format, va);
	va_end(va);
	return s;
}

//============================================================================
// string hash taken from libc++, uses murmur2 and cityhash64
//============================================================================

template <class _Size> inline _Size __loadword(const void* __p) {
	_Size __r;
	std::memcpy(&__r, __p, sizeof(__r));
	return __r;
}

// We use murmur2 when size_t is 32 bits, and cityhash64 when size_t
// is 64 bits.	This is because cityhash64 uses 64bit x 64bit
// multiplication, which can be very slow on 32-bit systems.
template <class _Size, size_t = sizeof(_Size)*8>
struct __murmur2_or_cityhash;

template <class _Size>
struct __murmur2_or_cityhash<_Size, 32> {
	_Size operator()(const void* __key, _Size __len);
};

// murmur2
template <class _Size>
_Size __murmur2_or_cityhash<_Size, 32>::operator()(const void* __key, _Size __len) {
	const _Size __m = 0x5bd1e995;
	const _Size __r = 24;
	_Size __h = __len;
	const unsigned char* __data = static_cast<const unsigned char*>(__key);
	for (; __len >= 4; __data += 4, __len -= 4) {
		_Size __k = __loadword<_Size>(__data);
		__k *= __m;
		__k ^= __k >> __r;
		__k *= __m;
		__h *= __m;
		__h ^= __k;
	}
	switch (__len) {
	case 3:
		__h ^= __data[2] << 16;
	case 2:
		__h ^= __data[1] << 8;
	case 1:
		__h ^= __data[0];
		__h *= __m;
	}
	__h ^= __h >> 13;
	__h *= __m;
	__h ^= __h >> 15;
	return __h;
}

template <class _Size>
struct __murmur2_or_cityhash<_Size, 64>
{
	_Size operator()(const void* __key, _Size __len);

private:
	// Some primes between 2^63 and 2^64.
	static const _Size __k0 = 0xc3a5c85c97cb3127ULL;
	static const _Size __k1 = 0xb492b66fbe98f273ULL;
	static const _Size __k2 = 0x9ae16a3b2f90404fULL;
	static const _Size __k3 = 0xc949d7c7509e6557ULL;

	static _Size __rotate(_Size __val, int __shift) {
		return __shift == 0 ? __val : ((__val >> __shift) | (__val << (64 - __shift)));
	}

	static _Size __rotate_by_at_least_1(_Size __val, int __shift) {
		return (__val >> __shift) | (__val << (64 - __shift));
	}

	static _Size __shift_mix(_Size __val) {
		return __val ^ (__val >> 47);
	}

	static _Size __hash_len_16(_Size __u, _Size __v) {
		const _Size __mul = 0x9ddfea08eb382d69ULL;
		_Size __a = (__u ^ __v) * __mul;
		__a ^= (__a >> 47);
		_Size __b = (__v ^ __a) * __mul;
		__b ^= (__b >> 47);
		__b *= __mul;
		return __b;
	}

	static _Size __hash_len_0_to_16(const char* __s, _Size __len) {
		if (__len > 8) {
			const _Size __a = __loadword<_Size>(__s);
			const _Size __b = __loadword<_Size>(__s + __len - 8);
			return __hash_len_16(__a, __rotate_by_at_least_1(__b + __len, __len)) ^ __b;
		}
		if (__len >= 4) {
			const uint32_t __a = __loadword<uint32_t>(__s);
			const uint32_t __b = __loadword<uint32_t>(__s + __len - 4);
			return __hash_len_16(__len + (__a << 3), __b);
		}
		if (__len > 0) {
			const unsigned char __a = __s[0];
			const unsigned char __b = __s[__len >> 1];
			const unsigned char __c = __s[__len - 1];
			const uint32_t __y = static_cast<uint32_t>(__a) +
				(static_cast<uint32_t>(__b) << 8);
			const uint32_t __z = __len + (static_cast<uint32_t>(__c) << 2);
			return __shift_mix(__y * __k2 ^ __z * __k3) * __k2;
		}
		return __k2;
	}

	static _Size __hash_len_17_to_32(const char *__s, _Size __len) {
		const _Size __a = __loadword<_Size>(__s) * __k1;
		const _Size __b = __loadword<_Size>(__s + 8);
		const _Size __c = __loadword<_Size>(__s + __len - 8) * __k2;
		const _Size __d = __loadword<_Size>(__s + __len - 16) * __k0;
		return __hash_len_16(__rotate(__a - __b, 43) + __rotate(__c, 30) + __d,
			__a + __rotate(__b ^ __k3, 20) - __c + __len);
	}

	// Return a 16-byte hash for 48 bytes.	Quick and dirty.
	// Callers do best to use "random-looking" values for a and b.
	static std::pair<_Size, _Size> __weak_hash_len_32_with_seeds(
		_Size __w, _Size __x, _Size __y, _Size __z, _Size __a, _Size __b)
	{
		__a += __w;
		__b = __rotate(__b + __a + __z, 21);
		const _Size __c = __a;
		__a += __x;
		__a += __y;
		__b += __rotate(__a, 44);
		return std::pair<_Size, _Size>(__a + __z, __b + __c);
	}

	// Return a 16-byte hash for s[0] ... s[31], a, and b.	Quick and dirty.
	static std::pair<_Size, _Size> __weak_hash_len_32_with_seeds(
		const char* __s, _Size __a, _Size __b)
	{
		return __weak_hash_len_32_with_seeds(
			__loadword<_Size>(__s),
			__loadword<_Size>(__s + 8),
			__loadword<_Size>(__s + 16),
			__loadword<_Size>(__s + 24),
			__a,
			__b
		);
	}

	// Return an 8-byte hash for 33 to 64 bytes.
	static _Size __hash_len_33_to_64(const char *__s, size_t __len) {
		_Size __z = __loadword<_Size>(__s + 24);
		_Size __a = __loadword<_Size>(__s) +
			(__len + __loadword<_Size>(__s + __len - 16)) * __k0;
		_Size __b = __rotate(__a + __z, 52);
		_Size __c = __rotate(__a, 37);
		__a += __loadword<_Size>(__s + 8);
		__c += __rotate(__a, 7);
		__a += __loadword<_Size>(__s + 16);
		_Size __vf = __a + __z;
		_Size __vs = __b + __rotate(__a, 31) + __c;
		__a = __loadword<_Size>(__s + 16) + __loadword<_Size>(__s + __len - 32);
		__z += __loadword<_Size>(__s + __len - 8);
		__b = __rotate(__a + __z, 52);
		__c = __rotate(__a, 37);
		__a += __loadword<_Size>(__s + __len - 24);
		__c += __rotate(__a, 7);
		__a += __loadword<_Size>(__s + __len - 16);
		_Size __wf = __a + __z;
		_Size __ws = __b + __rotate(__a, 31) + __c;
		_Size __r = __shift_mix((__vf + __ws) * __k2 + (__wf + __vs) * __k0);
		return __shift_mix(__r * __k0 + __vs) * __k2;
	}
};

// cityhash64
template <class _Size>
_Size __murmur2_or_cityhash<_Size, 64>::operator()(const void* __key, _Size __len) {
	const char* __s = static_cast<const char*>(__key);
	if (__len <= 32) {
		if (__len <= 16) {
			return __hash_len_0_to_16(__s, __len);
		} else {
			return __hash_len_17_to_32(__s, __len);
		}
	} else if (__len <= 64) {
		return __hash_len_33_to_64(__s, __len);
	}

	// For strings over 64 bytes we hash the end first, and then as we
	// loop we keep 56 bytes of state: v, w, x, y, and z.
	_Size __x = __loadword<_Size>(__s + __len - 40);
	_Size __y = __loadword<_Size>(__s + __len - 16) +
		__loadword<_Size>(__s + __len - 56);
	_Size __z = __hash_len_16(__loadword<_Size>(__s + __len - 48) + __len,
		__loadword<_Size>(__s + __len - 24));
	std::pair<_Size, _Size> __v = __weak_hash_len_32_with_seeds(__s + __len - 64, __len, __z);
	std::pair<_Size, _Size> __w = __weak_hash_len_32_with_seeds(__s + __len - 32, __y + __k1, __x);
	__x = __x * __k1 + __loadword<_Size>(__s);

	// Decrease len to the nearest multiple of 64, and operate on 64-byte chunks.
	__len = (__len - 1) & ~static_cast<_Size>(63);
	do {
		__x = __rotate(__x + __y + __v.first + __loadword<_Size>(__s + 8), 37) * __k1;
		__y = __rotate(__y + __v.second + __loadword<_Size>(__s + 48), 42) * __k1;
		__x ^= __w.second;
		__y += __v.first + __loadword<_Size>(__s + 40);
		__z = __rotate(__z + __w.first, 33) * __k1;
		__v = __weak_hash_len_32_with_seeds(__s, __v.second * __k1, __x + __w.first);
		__w = __weak_hash_len_32_with_seeds(__s + 32, __z + __w.second,
			__y + __loadword<_Size>(__s + 16));
		std::swap(__z, __x);
		__s += 64;
		__len -= 64;
	} while (__len != 0);
	return __hash_len_16(
		__hash_len_16(__v.first, __w.first) + __shift_mix(__y) * __k1 + __z,
		__hash_len_16(__v.second, __w.second) + __x
	);
}

//============================================================================
// string
//============================================================================

// Our own string class, it's not fancy at all, because all we need is a
// storage for hash map keys. The reason to use it instead of std::string is
// because if you use std::string, every hash map lookup may end up doing an
// allocation. Our string class supports temporary strings which are then
// copied to a persistent storage on C++ copy or move condition.
class string {
	// size contains persistence bit if the string is persistent, otherwise
	// it's temporary
	static constexpr size_t persistence_bit = (size_t)1 << (sizeof(size_t)*8-1);
	static constexpr size_t size_mask = ~persistence_bit;

	const char *mdata = 0;
	size_t msize = 0;
	size_t mhash = 0;

public:
	class temporary_tag {};
	static constexpr temporary_tag temporary = {};

	bool is_persistent() const { return msize & persistence_bit; }
	size_t size() const { return msize & size_mask; }
	const char *c_str() const { return mdata; }
	size_t hash() const { return mhash; }

	void clear() {
		if (is_persistent()) {
			delete[] mdata;
		}
		mdata = nullptr;
		msize = 0;
		mhash = 0;
	}

	string(const char *str) {
		if (str == nullptr)
			return;

		size_t len = strlen(str);
		if (len == 0)
			return;

		char *newdata = new (or_die) char[len+1];
		strcpy(newdata, str);

		msize = len | persistence_bit;
		mdata = newdata;
	}

	string(const char *str, temporary_tag) {
		if (str == nullptr)
			return;

		size_t len = strlen(str);
		if (len == 0)
			return;

		msize = len;
		mdata = str;
	}

	string(const char *str, size_t len) {
		if (str == nullptr)
			return;

		if (len <= 0)
			return;

		char *newdata = new (or_die) char[len+1];
		strncpy(newdata, str, len);
		newdata[len-1] = '\0';

		msize = len | persistence_bit;
		mdata = newdata;
	}

	string(const char *str, size_t len, temporary_tag) {
		if (str == nullptr)
			return;

		if (len <= 0)
			return;

		msize = len;
		mdata = str;
	}

	string(const string &r): string(r.c_str(), r.size()) {}
	string(string &&r) {
		if (r.mdata == nullptr)
			return;
		if (r.msize == 0)
			return;

		if (r.is_persistent()) {
			mdata = r.mdata;
			msize = r.msize;
			r.mdata = nullptr;
			r.msize = 0;
		} else {
			char *newdata = new (or_die) char[r.msize+1];
			strncpy(newdata, r.mdata, r.msize);
			newdata[r.msize-1] = '\0';

			msize = r.msize | persistence_bit;
			mdata = newdata;
		}
	}

	~string() {
		if (is_persistent()) {
			delete[] mdata;
		}
	}

	string &operator=(const string &r) {
		if (this == &r)
			return *this;

		clear();
		if (r.mdata == nullptr)
			return *this;
		if (r.msize == 0)
			return *this;

		const size_t s = r.size();
		char *newdata = new (or_die) char[s+1];
		strncpy(newdata, r.mdata, s);
		newdata[s-1] = '\0';

		msize = s | persistence_bit;
		mdata = newdata;
		return *this;
	}

	string &operator=(string &&r) {
		if (this == &r)
			return *this;

		clear();
		if (r.mdata == nullptr)
			return *this;
		if (r.msize == 0)
			return *this;

		if (r.is_persistent()) {
			mdata = r.mdata;
			msize = r.msize;
			r.mdata = nullptr;
			r.msize = 0;
		} else {
			char *newdata = new (or_die) char[r.msize+1];
			strncpy(newdata, r.mdata, r.msize);
			newdata[r.msize-1] = '\0';

			msize = r.msize | persistence_bit;
			mdata = newdata;
		}
		return *this;
	}

	bool operator==(const string &r) const {
		size_t s1 = size();
		size_t s2 = r.size();
		if (s1 != s2)
			return false;
		for (size_t i = 0; i < s1; i++) {
			if (mdata[i] != r.mdata[i])
				return false;
		}
		return true;
	}

	bool operator!=(const string &r) const {
		return !operator==(r);
	}

	void prehash() {
		mhash = __murmur2_or_cityhash<size_t>()(
			(const void*)mdata, size()
		);
	}
};

struct string_hash {
	size_t operator()(const string &s) const {
		size_t h = s.hash();
		if (h)
			return h;
		return __murmur2_or_cityhash<size_t>()(
			(const void*)s.c_str(), s.size()
		);
	}
};

template <typename T>
using string_map = std::unordered_map<string, T, string_hash>;

//============================================================================
// method_info and class_info
//============================================================================

static_assert(sizeof(uintptr_t) == 4 || sizeof(uintptr_t) == 8,
	"4 or 8 bytes uintptr_t size expected");
constexpr uintptr_t class_info_magic =
	(sizeof(uintptr_t) == 4)
	? 0xA3867EF9
	: 0xA3867EFCE93255CE;

// Method is a bit weird here, because we store plain functions in that
// structure as well, but mostly it's for methods and properties, so...
enum method_type {
	method_function,

	// tter means both setter or getter, can you come up with a better name?
	method_tter,
	method_read_only,
	method_const_read_only,
};

struct method_info {
	method_type type;
	int (*tter)(lua_State*, funcdata);
	union {
		int func_index;
		funcdata data;
	};
};

struct class_info {
	uintptr_t magic = class_info_magic;
	void *class_id;
	class_info *parent;
	string_map<method_info> get_table;
	string_map<method_info> set_table;
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

// given metatable 'index', get class_info* from it
static class_info *get_class_info(lua_State *L, int index) {
	lua_rawgeti(L, index, 1);
	auto cip = to_class_info(L, -1);
	lua_pop(L, 1);
	return cip;
}

static void class_info_mt_add_setter(lua_State *L, int index,
	const char *name, method_info mi)
{
	auto cip = get_class_info(L, index);
	cip->set_table[name] = mi;
}

static void class_info_add_setter(lua_State *L, int index,
	const char *name, method_info mi)
{
	lua_getmetatable(L, index);
	class_info_mt_add_setter(L, -1, name, mi);
	lua_pop(L, 1);
}

// -----
void class_info_add_var_getter(lua_State *L, int index,
	const char *name, int (*fp)(lua_State*, funcdata), funcdata data)
{
	lua_getmetatable(L, index);
	class_info_mt_add_var_getter(L, -1, name, fp, data);
	lua_pop(L, 1);
}

void class_info_mt_add_var_getter(lua_State *L, int index,
	const char *name, int (*fp)(lua_State*, funcdata), funcdata data)
{
	auto cip = get_class_info(L, index);
	method_info mi;
	mi.type = method_tter;
	mi.data = data;
	mi.tter = fp;
	cip->get_table[name] = mi;
}

// -----
void class_info_add_var_setter(lua_State *L, int index,
	const char *name, int (*fp)(lua_State*, funcdata), funcdata data)
{
	method_info mi;
	mi.type = method_tter;
	mi.data = data;
	mi.tter = fp;
	class_info_add_setter(L, index, name, mi);
}

void class_info_mt_add_var_setter(lua_State *L, int index,
	const char *name, int (*fp)(lua_State*, funcdata), funcdata data)
{
	method_info mi;
	mi.type = method_tter;
	mi.data = data;
	mi.tter = fp;
	class_info_mt_add_setter(L, index, name, mi);
}

// -----
void class_info_add_read_only(lua_State *L, int index, const char *name) {
	method_info mi;
	mi.type = method_read_only;
	class_info_add_setter(L, index, name, mi);
}

void class_info_mt_add_read_only(lua_State *L, int index, const char *name) {
	method_info mi;
	mi.type = method_read_only;
	class_info_mt_add_setter(L, index, name, mi);
}

// -----
void class_info_add_const_read_only(lua_State *L, int index, const char *name) {
	method_info mi;
	mi.type = method_const_read_only;
	class_info_add_setter(L, index, name, mi);
}

void class_info_mt_add_const_read_only(lua_State *L, int index, const char *name) {
	method_info mi;
	mi.type = method_const_read_only;
	class_info_mt_add_setter(L, index, name, mi);
}

// -----
void class_info_add_function(lua_State *L, int index, const char *name) {
	lua_getmetatable(L, index);
	lua_insert(L, -2);
	class_info_mt_add_function(L, -2, name);
	lua_pop(L, 1);
}

void class_info_mt_add_function(lua_State *L, int index, const char *name) {
	// assumes the function is on top of the stack
	auto cip = get_class_info(L, index);
	method_info mi;
	mi.type = method_function;
	mi.func_index = luaL_ref(L, index);
	cip->get_table[name] = mi;
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

// universal index metamethod, works for namespaces and all kinds of classes
static int index_meta_method(lua_State *L) {
	// get arg 2 contents and the class info from the metatable
	size_t len = 0;
	const char *strdata = lua_tolstring(L, 2, &len);
	if (!strdata) {
		lua_pushnil(L);
		return 1;
	}
	string str {strdata, len, string::temporary};
	str.prehash();
	auto cip = to_class_info(L, lua_upvalueindex(1));

	// use class info to retrieve the function or execute the getter
	bool parent = false;
	for (;;) {
		if (!cip) {
			lua_pushnil(L);
			return 1;
		}
		auto it = cip->get_table.find(str);
		if (it == cip->get_table.end()) {
			cip = cip->parent;
			parent = true;
			continue;
		}

		const auto &mi = it->second;
		switch (mi.type) {
		case method_function:
			if (parent) {
				_interlua_rawgetp(L, LUA_REGISTRYINDEX, cip->class_id);
				lua_rawgeti(L, -1, mi.func_index);
			} else {
				lua_rawgeti(L, lua_upvalueindex(2),
					mi.func_index);
			}
			return 1;
		case method_tter:
			return (*mi.tter)(L, mi.data);
		default:
			die("never happens");
		}
	}
}

static int newindex_meta_method(lua_State *L) {
	// get arg 2 contents and the class info from the metatable
	size_t len = 0;
	const char *strdata = lua_tolstring(L, 2, &len);
	if (!strdata) {
		return luaL_argerror(L, 2, "invalid string argument");
	}
	string str {strdata, len, string::temporary};
	str.prehash();
	auto cip = to_class_info(L, lua_upvalueindex(1));

	for (;;) {
		if (!cip) {
			return luaL_error(L, "no writable variable '%s'", strdata);
		}
		auto it = cip->set_table.find(str);
		if (it == cip->set_table.end()) {
			cip = cip->parent;
			continue;
		}

		const auto &mi = it->second;
		switch (mi.type) {
		case method_tter:
			return (*mi.tter)(L, mi.data);
		case method_read_only:
			return luaL_error(L, "'%s' is read-only", strdata);
		case method_const_read_only:
			return luaL_error(L, "'%s' is a read-only member of a const class instance",
				strdata);
		default:
			die("never happens");
		}
	}
}

// assumes that top of the stack looks like this:
// -1: class_info
// -2: metatable
static void set_common_metamethods(lua_State *L) {
	lua_pushvalue(L, -1);
	lua_pushvalue(L, -3);
	lua_pushcclosure(L, index_meta_method, 2);
	rawsetfield(L, -3, "__index");
	lua_pushvalue(L, -1);
	lua_pushvalue(L, -3);
	lua_pushcclosure(L, newindex_meta_method, 2);
	rawsetfield(L, -3, "__newindex");
}

static class_info *get_class_info_for(lua_State *L, void *key) {
	if (key == nullptr)
		return nullptr;

	_interlua_rawgetp(L, LUA_REGISTRYINDEX, key);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
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

void register_class_tables(lua_State *L, const char *name, const class_keys &keys) {
	// TODO: method: .HideMetatable() or .HiddenMetatable()
	// lua_pushnil(L);
	// rawsetfield(L, -2, "__metatable");

	// -- const table --

	lua_newtable(L);
	lua_pushfstring(L, "const %s", name);
	rawsetfield(L, -2, "__type");
	push_class_info(L, {
		keys.const_key,
		keys.parent
			? get_class_info_for(L, keys.parent->const_key)
			: nullptr,
		true,
	});
	set_common_metamethods(L);
	lua_rawseti(L, -2, 1);

	// -- class table --

	lua_newtable(L);
	lua_pushstring(L, name);
	rawsetfield(L, -2, "__type");
	push_class_info(L, {
		keys.class_key,
		keys.parent
			? get_class_info_for(L, keys.parent->class_key)
			: nullptr,
		false,
	});
	set_common_metamethods(L);
	lua_rawseti(L, -2, 1);

	// a pointer to the const table, mutable value can become a const value
	lua_pushvalue(L, -2);
	rawsetfield(L, -2, "__const");

	// -- static table --

	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	push_class_info(L, {
		keys.static_key,
		keys.parent
			? get_class_info_for(L, keys.parent->static_key)
			: nullptr,
		false,
	});
	set_common_metamethods(L);
	lua_rawseti(L, -2, 1);

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

void set_namespace_metatable(lua_State *L) {
	if (lua_getmetatable(L, -1)) {
		// if metatable is ours, we're done here
		if (get_class_info(L, -1))
			return;

		// hostile metatable, we'll replace it
		lua_pop(L, 1);
	}

	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);

	push_class_info(L, {nullptr, nullptr, nullptr});
	set_common_metamethods(L);
	lua_rawseti(L, -2, 1);
}

NSWrapper NSWrapper::Namespace(const char *name) {
	rawgetfield(L, -1, name);
	if (!lua_isnil(L, -1))
		return {L};

	// pop nil left from the rawgetfield call above
	lua_pop(L, 1);

	// create an empty table and make it a metatable of itself
	lua_newtable(L);
	set_namespace_metatable(L);

	// namespace[name] = table
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
static cstring where(lua_State *L) {
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
