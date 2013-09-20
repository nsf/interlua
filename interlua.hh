#pragma once

#include <lua.hpp>
#include <type_traits>
#include <utility>
#include <new>
#include <cstdint>

//----------------------------------------------------------------------------
// Workarounds for Lua versions prior to 5.2
//----------------------------------------------------------------------------
#if LUA_VERSION_NUM < 502
static inline int _interlua_absindex(lua_State *L, int idx) {
	if (idx > LUA_REGISTRYINDEX && idx < 0)
		return lua_gettop(L) + idx + 1;
	else
		return idx;
}

static inline void _interlua_rawgetp(lua_State *L, int idx, const void *p) {
	idx = _interlua_absindex(L, idx);
	lua_pushlightuserdata(L, const_cast<void*>(p));
	lua_rawget(L, idx);
}

static inline void _interlua_rawsetp(lua_State *L, int idx, const void *p) {
	idx = _interlua_absindex(L, idx);
	lua_pushlightuserdata(L, const_cast<void*>(p));
	lua_insert(L, -2); // put key behind value
	lua_rawset(L, idx);
}

#define _INTERLUA_OPEQ 1
#define _INTERLUA_OPLT 2
#define _INTERLUA_OPLE 3

inline int _interlua_compare(lua_State *L, int idx1, int idx2, int op) {
	switch (op) {
	case _INTERLUA_OPEQ:
		return lua_equal(L, idx1, idx2);
	case _INTERLUA_OPLT:
		return lua_lessthan(L, idx1, idx2);
	case _INTERLUA_OPLE:
		return lua_equal(L, idx1, idx2) || lua_lessthan(L, idx1, idx2);
	default:
		return 0;
	}
}

inline size_t _interlua_rawlen(lua_State *L, int idx) {
	return lua_objlen(L, idx);
}

#define _INTERLUA_OK 0
#define _interlua_pushglobaltable(L) lua_pushvalue(L, LUA_GLOBALSINDEX)
//----------------------------------------------------------------------------
#else
#define _interlua_absindex lua_absindex
#define _interlua_rawgetp lua_rawgetp
#define _interlua_rawsetp lua_rawsetp
#define _INTERLUA_OPEQ LUA_OPEQ
#define _INTERLUA_OPLT LUA_OPLT
#define _INTERLUA_OPLE LUA_OPLE
#define _interlua_compare lua_compare
#define _interlua_rawlen lua_rawlen
#define _INTERLUA_OK LUA_OK
#define _interlua_pushglobaltable lua_pushglobaltable
#endif
//----------------------------------------------------------------------------

namespace InterLua {

//============================================================================
// Helpers
//============================================================================


void die(const char *str, ...);
void stack_dump(lua_State *L);

// pushes t[key] onto the stack, where t is the table at the given index
static inline void rawgetfield(lua_State *L, int index, const char *key) {
	index = _interlua_absindex(L, index);
	lua_pushstring(L, key);
	lua_rawget(L, index);
}

// does t[key] = v, where t is the table at the given index and v is the value
// at the top of the stack, pops the value from the stack
static inline void rawsetfield(lua_State *L, int index, const char *key) {
	index = _interlua_absindex(L, index);
	lua_pushstring(L, key);
	lua_insert(L, -2);
	lua_rawset(L, index);
}

// RAII helper for popping things from the lua stack
class stack_pop {
	lua_State *L;
	int count;

public:
	stack_pop(lua_State *L, int count): L(L), count(count) {}
	~stack_pop() { lua_pop(L, count); }
};

// creates these tables on the stack:
//   -1 static table
//   -2 class table
//   -3 const table
void create_class_tables(lua_State *L, const char *name);

template <typename T>
struct ClassKey {
	static void *Static() { static int value; return &value; }
	static void *Class() { static int value; return &value; }
	static void *Const() { static int value; return &value; }
};

//============================================================================
// Userdata
//============================================================================

static_assert(sizeof(uintptr_t) == 4 || sizeof(uintptr_t) == 8,
	"4 or 8 bytes uintptr_t size expected");
constexpr uintptr_t UserdataMagic =
	(sizeof(uintptr_t) == 4)
	? 0xA3867EF0
	: 0xA3867EFCE93255C0;
constexpr uintptr_t UserdataMagicMask =
	(sizeof(uintptr_t) == 4)
	? 0xFFFFFFF0
	: 0xFFFFFFFFFFFFFFF0;

class Userdata {
	// also contains a constness flag
	uintptr_t magic;

public:
	Userdata(bool constness): magic(UserdataMagic | (uintptr_t)constness) {}
	bool IsValid() const { return (magic & UserdataMagicMask) == UserdataMagic; }
	bool IsConst() const { return (magic & ~UserdataMagicMask); }
	void SetConst(bool constness) { magic = UserdataMagic | (uintptr_t)constness; }

	virtual ~Userdata();
	virtual void *Data() = 0;
};

template <typename T>
class UserdataValue : public Userdata {
	T value;

public:
	template <typename ...Args>
	UserdataValue(bool constness, Args &&...args):
		Userdata(constness), value(std::forward<Args>(args)...) {}
	virtual void *Data() override { return (void*)&value; }
};

template <typename T>
class UserdataPointer : public Userdata {
	T *ptr;

public:
	UserdataPointer(bool constness, T *ptr):
		Userdata(constness), ptr(ptr) {}
	virtual void *Data() override { return (void*)ptr; }
};

Userdata *get_userdata_typeless(lua_State *L, int index);
Userdata *get_userdata(lua_State *L, int index, void *base_class_key, bool can_be_const);
Userdata *get_userdata_unchecked(lua_State *L, int index);

template <typename T>
static inline T *get_class(lua_State *L, int index, bool can_be_const) {
	return (T*)get_userdata(L, index, ClassKey<T>::Class(), can_be_const)->Data();
}

template <typename T>
static inline T *get_class_unchecked(lua_State *L, int index) {
	return (T*)((Userdata*)lua_touserdata(L, index))->Data();
}

//============================================================================
// Stack Operations
//============================================================================

template <typename T>
struct StackOps {
	// There is a need for PURE_T here, because it's perfect
	// forwarding type of template, T can be deduced to lvalue
	// cases (T& and const T&) or rvalue case (T), but in all cases
	// we need pure T as a template parameter for many of the
	// templates used here.
	using PURE_T = typename std::remove_const<
		typename std::remove_reference<T>::type
	>::type;
	using NOREF_T = typename std::remove_reference<T>::type;

	static inline void Push(lua_State *L, T &&value) {
		void *mem = lua_newuserdata(L, sizeof(UserdataValue<PURE_T>));

		// We pass Class() key here, because passing an argument via
		// C++ reference always makes a copy in Lua. The reason for
		// this is because semantically in C++ you can't see the
		// difference between T and T& and we need to leave a
		// reasonable way of making copies when passing values to Lua.
		// Hence, the decision is to always copy ref values, but pass
		// pointers directly as UserdataPointer and respect the
		// constness.
		_interlua_rawgetp(L, LUA_REGISTRYINDEX, ClassKey<PURE_T>::Class());
		if (lua_isnil(L, -1)) {
			die("pushing an unregistered class onto the lua stack");
		}
		lua_setmetatable(L, -2);
		new (mem) UserdataValue<PURE_T>(false, std::forward<T>(value));
	}

	static inline T Get(lua_State *L, int index) {
		// the class cannot be const, when T isn't const
		return *get_class<PURE_T>(L, index, std::is_const<NOREF_T>::value);
	}
};


// pointer cases are covered separately however
template <typename T>
struct StackOps<T*> {
	using PURE_T = typename std::remove_const<T>::type;
	static inline void Push(lua_State *L, T *value) {
		void *mem = lua_newuserdata(L, sizeof(UserdataPointer<T>));
		void *mt = std::is_const<T>::value ?
			ClassKey<PURE_T>::Const() :
			ClassKey<PURE_T>::Class();
		_interlua_rawgetp(L, LUA_REGISTRYINDEX, mt);
		if (lua_isnil(L, -1)) {
			die("pushing an unregistered class onto the lua stack");
		}
		lua_setmetatable(L, -2);
		new (mem) UserdataPointer<T>(std::is_const<T>::value, value);
	}
	static inline T *Get(lua_State *L, int index) {
		// the class cannot be const, when T isn't const
		return get_class<PURE_T>(L, index, std::is_const<T>::value);
	}
};

template <>
struct StackOps<lua_State*> {
	static inline lua_State *Get(lua_State *L, int) {
		return L;
	}
};

#define _stack_ops_integer(TT, T)				\
template <>							\
struct StackOps<TT> {						\
	static inline void Push(lua_State *L, T value) {	\
		lua_pushinteger(L, value);			\
	}							\
	static inline T Get(lua_State *L, int index) {		\
		return luaL_checkinteger(L, index);		\
	}							\
};

_stack_ops_integer(signed char, signed char)
_stack_ops_integer(signed char&, signed char)
_stack_ops_integer(const signed char&, signed char)
_stack_ops_integer(unsigned char, unsigned char)
_stack_ops_integer(unsigned char&, unsigned char)
_stack_ops_integer(const unsigned char&, unsigned char)

_stack_ops_integer(short, short)
_stack_ops_integer(short&, short)
_stack_ops_integer(const short&, short)
_stack_ops_integer(unsigned short, unsigned short)
_stack_ops_integer(unsigned short&, unsigned short)
_stack_ops_integer(const unsigned short&, unsigned short)

_stack_ops_integer(int, int)
_stack_ops_integer(int&, int)
_stack_ops_integer(const int&, int)
_stack_ops_integer(unsigned int, unsigned int)
_stack_ops_integer(unsigned int&, unsigned int)
_stack_ops_integer(const unsigned int&, unsigned int)

_stack_ops_integer(long, long)
_stack_ops_integer(long&, long)
_stack_ops_integer(const long&, long)
_stack_ops_integer(unsigned long, unsigned long)
_stack_ops_integer(unsigned long&, unsigned long)
_stack_ops_integer(const unsigned long&, unsigned long)

#undef _stack_ops_integer


#define _stack_ops_float(TT, T)					\
template <>							\
struct StackOps<TT> {						\
	static inline void Push(lua_State *L, T value) {	\
		lua_pushnumber(L, value);			\
	}							\
	static inline T Get(lua_State *L, int index) {		\
		return luaL_checknumber(L, index);		\
	}							\
};

_stack_ops_float(float, float)
_stack_ops_float(float&, float)
_stack_ops_float(const float&, float)
_stack_ops_float(double, double)
_stack_ops_float(double&, double)
_stack_ops_float(const double&, double)

#undef _stack_ops_float


#define _stack_ops_cstr_impl						\
static inline void Push(lua_State *L, const char *str) {		\
	if (str)							\
		lua_pushstring(L, str);					\
	else								\
		lua_pushnil(L);						\
}									\
static inline const char *Get(lua_State *L, int index) {		\
	return lua_isnil(L, index) ? 0 : luaL_checkstring(L, index);	\
}

template <>      struct StackOps<const char*> { _stack_ops_cstr_impl };
template <int N> struct StackOps<const char (&)[N]> { _stack_ops_cstr_impl };

#undef _stack_ops_cstr_impl


#define _stack_ops_char(T)					\
template <>							\
struct StackOps<T> {						\
	static inline void Push(lua_State *L, char value) {	\
		char str[2] = {value, 0};			\
		lua_pushstring(L, str);				\
	}							\
	static inline char Get(lua_State *L, int index) {	\
		return luaL_checkstring(L, index)[0];		\
	}							\
};

_stack_ops_char(char)
_stack_ops_char(char&)
_stack_ops_char(const char&)

#undef _stack_ops_char

#define _stack_ops_bool(T)					\
template <>							\
struct StackOps<T> {						\
	static inline void Push(lua_State *L, bool value) {	\
		lua_pushboolean(L, value ? 1 : 0);		\
	}							\
	static inline bool Get(lua_State *L, int index) {	\
		return lua_toboolean(L, index) ? true : false;	\
	}							\
};

_stack_ops_bool(bool)
_stack_ops_bool(bool&)
_stack_ops_bool(const bool&)

#undef _stack_ops_bool

//============================================================================
// Errors
//============================================================================

class Error {
protected:
	int code = _INTERLUA_OK;

public:
	virtual ~Error();
	int Code() const { return code; }
	virtual void Set(int code, const char*);
	virtual const char *What() const;
	explicit operator bool() const { return this->code != _INTERLUA_OK; }
};

class VerboseError : public Error {
protected:
	char *message = nullptr;

public:
	~VerboseError() override;
	void Set(int code, const char *msg) override;
	const char *What() const override;
};

class AbortError : public Error {
public:
	void Set(int code, const char *msg) override;
};

extern AbortError DefaultError;


// simply ignore errors when they are passed as an argument
#define _stack_ops_ignore_push(T)			\
template <>						\
struct StackOps<T> {					\
	static inline void Push(lua_State*, T) {	\
	}						\
};

_stack_ops_ignore_push(Error*)
_stack_ops_ignore_push(VerboseError*)
_stack_ops_ignore_push(AbortError*)

#undef _stack_ops_ignore_push


// is_error_ptr<T>::value is true, if T is a pointer to Error or a pointer to a
// class derived from Error.
template <typename T>
struct is_error_ptr :
	public std::integral_constant<bool,
		std::is_pointer<T>::value &&
		std::is_base_of<
			Error,
			typename std::remove_pointer<T>::type
		>::value
	> {};

template <typename T, bool OK = is_error_ptr<T>::value>
struct get_error_or_null;

template <typename T>
struct get_error_or_null<T, true> {
	static inline Error *get(T &&v) { return v; }
};

template <typename T>
struct get_error_or_null<T, false> {
	static inline Error *get(T&&) { return nullptr; }
};

static inline Error *get_last_if_error() {
	return nullptr;
}

template <typename T>
static inline Error *get_last_if_error(T &&last) {
	return get_error_or_null<T>::get(std::forward<T>(last));
}

template <typename T, typename ...Args>
static inline Error *get_last_if_error(T&&, Args &&...args) {
	return get_last_if_error(std::forward<Args>(args)...);
}

//============================================================================
// Function Binding Helpers
//============================================================================

template<int ...I> struct index_tuple_type {
        template<int N> using append = index_tuple_type<I..., N>;
};
template<int N> struct make_index_impl {
        using type = typename make_index_impl<N-1>::type::template append<N-1>;
};
template<> struct make_index_impl<0> { using type = index_tuple_type<>; };
template<int N> using index_tuple = typename make_index_impl<N>::type;

template <typename F, typename IT>
struct func_traits;

template <typename R, typename ...Args, int ...I>
struct func_traits<R (Args...), index_tuple_type<I...>> {
	static R call(lua_State *L, R (*fp)(Args...)) {
		(void)L; // silence notused warning for cases with no arguments
		return (*fp)(StackOps<Args>::Get(L, I+1)...);
	}

	static void construct(lua_State *L, void *mem) {
		(void)L; // silence notused warning for cases with no arguments
		new (mem) UserdataValue<R>(false, StackOps<Args>::Get(L, I+2)...);
	}
};

template <typename T, typename R, typename ...Args, int ...I>
struct func_traits<R (T::*)(Args...), index_tuple_type<I...>> {
	static R call(lua_State *L, T *cls, R (T::*fp)(Args...)) {
		(void)L; // silence notused warning for cases with no arguments
		return (cls->*fp)(StackOps<Args>::Get(L, I+2)...);
	}
};

template <typename T, typename R, typename ...Args, int ...I>
struct func_traits<R (T::*)(Args...) const, index_tuple_type<I...>> {
	static R call(lua_State *L, const T *cls, R (T::*fp)(Args...) const) {
		(void)L; // silence notused warning for cases with no arguments
		return (cls->*fp)(StackOps<Args>::Get(L, I+2)...);
	}
};

template <typename T>
struct is_const_member_function;
template <typename T, typename R, typename ...Args>
struct is_const_member_function<R (T::*)(Args...) const> : std::true_type {};
template <typename T, typename R, typename ...Args>
struct is_const_member_function<R (T::*)(Args...)> : std::false_type {};

template <typename F>
struct call {};

// ordinary function call wrapper
template <typename R, typename ...Args>
struct call<R (*)(Args...)> {
	static int cfunction(lua_State *L) {
		typedef R (*FP)(Args...);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		StackOps<R>::Push(L,
			func_traits<R (Args...), index_tuple<sizeof...(Args)>>::
				call(L, fp)
		);
		return 1;
	}
};

// ordinary function call wrapper (no return value)
template <typename ...Args>
struct call<void (*)(Args...)> {
	static int cfunction(lua_State *L) {
		typedef void (*FP)(Args...);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		func_traits<void (Args...), index_tuple<sizeof...(Args)>>::
			call(L, fp);
		return 0;
	}
};

// member function call wrapper
template <typename T, typename R, typename ...Args>
struct call<R (T::*)(Args...)> {
	static int cfunction(lua_State *L) {
		typedef R (T::*FP)(Args...);
		T *cls = get_class<T>(L, 1, false);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		StackOps<R>::Push(L,
			func_traits<R (T::*)(Args...), index_tuple<sizeof...(Args)>>::
				call(L, cls, fp)
		);
		return 1;
	}
};

// member function call wrapper (no return value)
template <typename T, typename ...Args>
struct call<void (T::*)(Args...)> {
	static int cfunction(lua_State *L) {
		typedef void (T::*FP)(Args...);
		T *cls = get_class<T>(L, 1, false);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		func_traits<void (T::*)(Args...), index_tuple<sizeof...(Args)>>::
			call(L, cls, fp);
		return 0;
	}
};

// const member function call wrapper
template <typename T, typename R, typename ...Args>
struct call<R (T::*)(Args...) const> {
	static int cfunction(lua_State *L) {
		typedef R (T::*FP)(Args...) const;
		const T *cls = get_class<T>(L, 1, true);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		StackOps<R>::Push(L,
			func_traits<R (T::*)(Args...) const, index_tuple<sizeof...(Args)>>::
				call(L, cls, fp)
		);
		return 1;
	}
};

// const member function call wrapper (no return value)
template <typename T, typename ...Args>
struct call<void (T::*)(Args...) const> {
	static int cfunction(lua_State *L) {
		typedef void (T::*FP)(Args...) const;
		T *cls = get_class<T>(L, 1, true);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		func_traits<void (T::*)(Args...) const, index_tuple<sizeof...(Args)>>::
			call(L, cls, fp);
		return 0;
	}
};

// placement constructor wrapper
template <typename T, typename ...Args>
struct construct {
	static int cfunction(lua_State *L) {
		void *mem = lua_newuserdata(L, sizeof(UserdataValue<T>));
		_interlua_rawgetp(L, LUA_REGISTRYINDEX, ClassKey<T>::Class());
		lua_setmetatable(L, -2);
		func_traits<T (Args...), index_tuple<sizeof...(Args)>>::construct(L, mem);
		return 1;
	}
};

template <typename FP>
struct member_cfunction;

template <typename T>
struct member_cfunction<int (T::*)(lua_State*)> {
	static int cfunction(lua_State *L) {
		typedef int (T::*FP)(lua_State*);
		T *cls = get_class<T>(L, 1, false);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		return (cls->*fp)(L);
	}
};

template <typename T>
struct member_cfunction<int (T::*)(lua_State*) const> {
	static int cfunction(lua_State *L) {
		typedef int (T::*FP)(lua_State*) const;
		const T *cls = get_class<T>(L, 1, true);
		auto fp = *(FP*)lua_touserdata(L, lua_upvalueindex(1));
		return (cls->*fp)(L);
	}
};

static inline void recursive_push(lua_State*) {
	// no arguments
}

template <typename T>
static inline void recursive_push(lua_State *L, T &&arg) {
	StackOps<T>::Push(L, std::forward<T>(arg));
}

template <typename T, typename ...Args>
static inline void recursive_push(lua_State *L, T &&arg, Args &&...args) {
	StackOps<T>::Push(L, std::forward<T>(arg));
	recursive_push(L, std::forward<Args>(args)...);
}

int read_only_error(lua_State *L);
int const_read_only_error(lua_State *L);

template <typename T>
int get_variable(lua_State *L) {
	T *ptr = (T*)lua_touserdata(L, lua_upvalueindex(1));
	StackOps<T>::Push(L, *ptr);
	return 1;
}

template <typename T>
int set_variable(lua_State *L) {
	T *ptr = (T*)lua_touserdata(L, lua_upvalueindex(1));
	*ptr = StackOps<T>::Get(L, 1);
	return 0;
}

template <typename T, typename U>
int get_property(lua_State *L) {
	const T *cls = get_class_unchecked<T>(L, 1);
	U T:: *mp = *(U T::**)lua_touserdata(L, lua_upvalueindex(1));
	StackOps<U>::Push(L, cls->*mp);
	return 1;
}

template <typename T, typename U>
int set_property(lua_State *L) {
	T *cls = get_class_unchecked<T>(L, 1);
	U T:: *mp = *(U T::**)lua_touserdata(L, lua_upvalueindex(1));
	cls->*mp = StackOps<U>::Get(L, 2);
	return 0;
}

template <typename T>
int gc_meta_method(lua_State *L) {
	Userdata *ud = get_userdata_unchecked(L, 1);
	ud->~Userdata();
	return 0;
}

//============================================================================
// Variable Access
//============================================================================

enum VariableAccess {
	ReadOnly,
	ReadWrite,
};

//============================================================================
// Class
//============================================================================

class NSWrapper;

template <typename T>
class CWrapper {
	lua_State *L = nullptr;
	NSWrapper &parent;

	template <typename GET, typename SET>
	CWrapper &property(const char *name, GET get, SET set) {
		rawgetfield(L, -3, "__propget"); // __propget from const table
		rawgetfield(L, -3, "__propget"); // __propget from class table
		*(GET*)lua_newuserdata(L, sizeof(GET)) = get;
		lua_pushcclosure(L, call<GET>::cfunction, 1);
		lua_pushvalue(L, -1);
		rawsetfield(L, -3, name);
		rawsetfield(L, -3, name);
		lua_pop(L, 2);

		rawgetfield(L, -3, "__propset"); // __propset from const table
		lua_pushstring(L, name);
		lua_pushcclosure(L, const_read_only_error, 1);
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		rawgetfield(L, -2, "__propset"); // __propset from class table
		if (set != nullptr) {
			*(SET*)lua_newuserdata(L, sizeof(SET)) = set;
			lua_pushcclosure(L, call<SET>::cfunction, 1);
		} else {
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);
		return *this;
	}

public:
	CWrapper() = delete;
	CWrapper(lua_State *L, NSWrapper &parent): L(L), parent(parent) {}
	inline NSWrapper &End() { lua_pop(L, 3); return parent; }

	template <typename ...Args>
	CWrapper &Constructor() {
		lua_pushcclosure(L, construct<T, Args...>::cfunction, 0);
		rawsetfield(L, -2, "__call");
		return *this;
	}

	// I'm not sure if I need these 2 methods, currently they protect from
	// mixing member function getters/setters with proxy function
	// getters/setters, but does it need to be restricted in that way?
	template <typename TG, typename TS = int>
	CWrapper &Property(const char *name, TG (T::*get)() const, void (T::*set)(TS) = nullptr) {
		return property(name, get, set);
	}

	template <typename TG, typename TS = int>
	CWrapper &Property(const char *name, TG (*get)(const T*), void (*set)(T*, TS) = nullptr) {
		return property(name, get, set);
	}

	template <typename U>
	CWrapper &Variable(const char *name, U T:: *mp, VariableAccess va = ReadWrite) {
		typedef U T:: *mp_t;
		rawgetfield(L, -3, "__propget"); // put __propget from const table
		rawgetfield(L, -3, "__propget"); // put __propget from class table
		*(mp_t*)lua_newuserdata(L, sizeof(mp_t)) = mp;
		lua_pushcclosure(L, get_property<T, U>, 1);
		lua_pushvalue(L, -1);
		rawsetfield(L, -3, name);
		rawsetfield(L, -3, name);
		lua_pop(L, 2);

		rawgetfield(L, -3, "__propset"); // put __propset from const table
		rawgetfield(L, -3, "__propset"); // put __propset from class table
		lua_pushstring(L, name);
		lua_pushcclosure(L, const_read_only_error, 1);
		rawsetfield(L, -3, name); // const_table[name] = const_read_only_error
		lua_remove(L, -2); // remove const table from the stack

		switch (va) {
		case ReadWrite:
			*(mp_t*)lua_newuserdata(L, sizeof(mp_t)) = mp;
			lua_pushcclosure(L, set_property<T, U>, 1);
			break;
		case ReadOnly:
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
			break;
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);
		return *this;
	}

	template <typename FP>
	CWrapper &Function(const char *name, FP fp) {
		*(FP*)lua_newuserdata(L, sizeof(fp)) = fp;
		lua_pushcclosure(L, call<FP>::cfunction, 1);
		if (is_const_member_function<FP>::value) {
			lua_pushvalue(L, -1);
			rawsetfield(L, -4, name);
			rawsetfield(L, -4, name);
		} else {
			rawsetfield(L, -3, name);
		}
		return *this;
	}

	template <typename FP>
	CWrapper &CFunction(const char *name, FP fp) {
		*(FP*)lua_newuserdata(L, sizeof(fp)) = fp;
		lua_pushcclosure(L, member_cfunction<FP>::cfunction, 1);
		if (is_const_member_function<FP>::value) {
			lua_pushvalue(L, -1);
			rawsetfield(L, -4, name);
			rawsetfield(L, -4, name);
		} else {
			rawsetfield(L, -3, name);
		}
		return *this;
	}

	template <typename U>
	CWrapper &StaticVariable(const char *name, U *p, VariableAccess va = ReadWrite) {
		rawgetfield(L, -1, "__propget");
		lua_pushlightuserdata(L, p);
		lua_pushcclosure(L, get_variable<U>, 1);
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		rawgetfield(L, -1, "__propset");
		switch (va) {
		case ReadWrite:
			lua_pushlightuserdata(L, p);
			lua_pushcclosure(L, set_variable<U>, 1);
			break;
		case ReadOnly:
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
			break;
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		return *this;
	}

	// "TS = int" is necessary in order to make it possible to leave the
	// second argument as nullptr. Otherwise, if there is no default type,
	// template type deduction will fail.
	template <typename TG, typename TS = int>
	CWrapper &StaticProperty(const char *name, TG (*get)(), void (*set)(TS) = nullptr) {
		typedef TG (*get_t)();
		typedef void (*set_t)(TS);

		rawgetfield(L, -1, "__propget");
		*(get_t*)lua_newuserdata(L, sizeof(get_t)) = get;
		lua_pushcclosure(L, call<get_t>::cfunction, 1);
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		rawgetfield(L, -1, "__propset");
		if (set != nullptr) {
			*(set_t*)lua_newuserdata(L, sizeof(set_t)) = set;
			lua_pushcclosure(L, call<set_t>::cfunction, 1);
		} else {
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		return *this;
	}

	CWrapper &StaticCFunction(const char *name, lua_CFunction fp) {
		lua_pushcfunction(L, fp);
		rawsetfield(L, -2, name);
		return *this;
	}

	template <typename FP>
	CWrapper &StaticFunction(const char *name, FP fp) {
		*(FP*)lua_newuserdata(L, sizeof(fp)) = fp;
		lua_pushcclosure(L, call<FP>::cfunction, 1);
		rawsetfield(L, -2, name);

		return *this;
	}
};

//============================================================================
// Namespace
//============================================================================

class NSWrapper {
	lua_State *L = nullptr;
	bool is_global = false;

	struct base_data {
		void *statickey;
		void *classkey;
		void *constkey;
	};
public:
	NSWrapper() = delete;
	NSWrapper(lua_State *L, bool is_global = false): L(L), is_global(is_global) {}

	NSWrapper Namespace(const char *name);
	inline NSWrapper End() { lua_pop(L, 1); return {L}; }

	template <typename T>
	CWrapper<T> Class(const char *name, base_data *b = nullptr) {
		rawgetfield(L, -1, name);
		if (!lua_isnil(L, -1)) {
			rawgetfield(L, -1, "__class");
			rawgetfield(L, -1, "__const");

			// arrange tables in proper order:
			// -1 static table
			// -2 class table
			// -3 const table
			lua_insert(L, -3);
			lua_insert(L, -2);
			return {L, *this};
		}

		lua_pop(L, 1); // pop nil
		create_class_tables(L, name);

		lua_pushcfunction(L, gc_meta_method<T>);
		rawsetfield(L, -3, "__gc");
		lua_pushcfunction(L, gc_meta_method<T>);
		rawsetfield(L, -4, "__gc");

		// namespace[name] = static_table
		lua_pushvalue(L, -1);
		rawsetfield(L, -5, name);

		lua_pushvalue(L, -1);
		_interlua_rawsetp(L, LUA_REGISTRYINDEX, ClassKey<T>::Static());
		lua_pushvalue(L, -2);
		_interlua_rawsetp(L, LUA_REGISTRYINDEX, ClassKey<T>::Class());
		lua_pushvalue(L, -3);
		_interlua_rawsetp(L, LUA_REGISTRYINDEX, ClassKey<T>::Const());

		if (b) {
			_interlua_rawgetp(L, LUA_REGISTRYINDEX, b->constkey);
			_interlua_rawgetp(L, LUA_REGISTRYINDEX, b->classkey);
			_interlua_rawgetp(L, LUA_REGISTRYINDEX, b->statickey);
			if (lua_isnil(L, -1)) {
				die("trying to register a derived class '%s' "
					"from an unregistered base class",
					name);
			}
			rawsetfield(L, -4, "__parent");
			rawsetfield(L, -4, "__parent");
			rawsetfield(L, -4, "__parent");
		}
		return {L, *this};
	}

	template <typename T, typename Base>
	CWrapper<T> DerivedClass(const char *name) {
		static_assert(std::is_base_of<Base, T>::value,
			"T must be a class derived from Base");

		base_data b = {
			ClassKey<Base>::Static(),
			ClassKey<Base>::Class(),
			ClassKey<Base>::Const(),
		};
		return Class<T>(name, &b);
	}

	NSWrapper &CFunction(const char *name, int (*fp)(lua_State*)) {
		lua_pushcfunction(L, fp);
		rawsetfield(L, -2, name);
		return *this;
	}

	template <typename FP>
	NSWrapper &Function(const char *name, FP fp) {
		*(FP*)lua_newuserdata(L, sizeof(fp)) = fp;
		lua_pushcclosure(L, call<FP>::cfunction, 1);
		rawsetfield(L, -2, name);

		return *this;
	}

	template <typename T>
	NSWrapper &Variable(const char *name, T *p, VariableAccess va = ReadWrite) {
		if (is_global) {
			die("error: variable '%s' cannot be defined in the global namespace, "
				"InterLua doesn't support variables in the global namespace",
				name);
		}
		rawgetfield(L, -1, "__propget");
		lua_pushlightuserdata(L, p);
		lua_pushcclosure(L, get_variable<T>, 1);
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		rawgetfield(L, -1, "__propset");
		switch (va) {
		case ReadWrite:
			lua_pushlightuserdata(L, p);
			lua_pushcclosure(L, set_variable<T>, 1);
			break;
		case ReadOnly:
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
			break;
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		return *this;
	}

	// "TS = int" is necessary in order to make it possible to leave the
	// second argument as nullptr. Otherwise, if there is no default type,
	// template type deduction will fail.
	template <typename TG, typename TS = int>
	NSWrapper &Property(const char *name, TG (*get)(), void (*set)(TS) = nullptr) {
		if (is_global) {
			die("error: property '%s' cannot be defined in the global namespace, "
				"InterLua doesn't support properties in the global namespace",
				name);
		}
		typedef TG (*get_t)();
		typedef void (*set_t)(TS);

		rawgetfield(L, -1, "__propget");
		*(get_t*)lua_newuserdata(L, sizeof(get_t)) = get;
		lua_pushcclosure(L, call<get_t>::cfunction, 1);
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		rawgetfield(L, -1, "__propset");
		if (set != nullptr) {
			*(set_t*)lua_newuserdata(L, sizeof(set_t)) = set;
			lua_pushcclosure(L, call<set_t>::cfunction, 1);
		} else {
			lua_pushstring(L, name);
			lua_pushcclosure(L, read_only_error, 1);
		}
		rawsetfield(L, -2, name);
		lua_pop(L, 1);

		return *this;
	}
};

static inline NSWrapper GlobalNamespace(lua_State *L) {
	_interlua_pushglobaltable(L);
	return {L, true};
}

static inline NSWrapper NewNamespace(lua_State *L) {
	lua_newtable(L);
	return {L};
}

//============================================================================
// Ref
//============================================================================

class Ref {
	lua_State *L = nullptr;
	int ref = LUA_REFNIL;
	int tableref = LUA_REFNIL;

public:
	// no point in having Ref without L
	Ref() = delete;
	Ref(lua_State *L): L(L) {}
	Ref(lua_State *L, int ref): L(L), ref(ref) {}
	Ref(lua_State *L, int ref, int tableref):
		L(L), ref(ref), tableref(tableref) {}

	Ref(const Ref &r): L(r.L) {
		if (r.ref == LUA_REFNIL) {
			return;
		}
		r.Push(L);
		ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	Ref(Ref &&r): L(r.L), ref(r.ref), tableref(r.tableref) {
		r.L = nullptr;
		r.ref = LUA_REFNIL;
		r.tableref = LUA_REFNIL;
	}

	~Ref() {
		if (L) {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			if (tableref != LUA_REFNIL) {
				luaL_unref(L, LUA_REGISTRYINDEX, tableref);
			}
		}
	}

#define _generic_op(op, n1, n2, luaop)					\
	template <typename T>						\
	bool op(T &&r) const {						\
		stack_pop p(L, 2);					\
		Push(L);						\
		StackOps<T>::Push(L, std::forward<T>(r));		\
		return _interlua_compare(L, n1, n2, luaop) == 1;	\
	}

	_generic_op(operator==, -2, -1, _INTERLUA_OPEQ)
	_generic_op(operator<, -2, -1, _INTERLUA_OPLT)
	_generic_op(operator<=, -2, -1, _INTERLUA_OPLE)
	_generic_op(operator>, -1, -2, _INTERLUA_OPLT)
	_generic_op(operator>=, -1, -2, _INTERLUA_OPLE)

#undef _generic_op

	template <typename T>
	Ref &operator=(T &&r) {
		if (tableref != LUA_REFNIL) {
			stack_pop p(L, 1);
			lua_rawgeti(L, LUA_REGISTRYINDEX, tableref);
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			StackOps<T>::Push(L, std::forward<T>(r));
			lua_settable(L, -3);
		} else {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			StackOps<T>::Push(L, std::forward<T>(r));
			ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		return *this;
	}

	Ref &operator=(Ref &&r) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		L = r.L;
		ref = r.ref;
		tableref = r.tableref;
		r.L = nullptr;
		r.ref = LUA_REFNIL;
		r.tableref = LUA_REFNIL;
		return *this;
	}

	Ref &operator=(const Ref &r) {
		if (tableref != LUA_REFNIL) {
			stack_pop p(L, 1);
			lua_rawgeti(L, LUA_REGISTRYINDEX, tableref);
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			r.Push(L);
			lua_rawset(L, -3);
		} else {
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			r.Push(L);
			ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		return *this;
	}

	template <typename ...Args>
	Ref operator()(Args &&...args) const {
		Error *err = get_last_if_error(std::forward<Args>(args)...);
		int nargs = sizeof...(Args) - (err ? 1 : 0);
		if (!err) err = &DefaultError;

		Push(L);
		recursive_push(L, std::forward<Args>(args)...);
		int code = lua_pcall(L, nargs, 1, 0);
		if (code != _INTERLUA_OK) {
			err->Set(code, lua_tostring(L, -1));
			lua_pop(L, 1); // pop the error message from the stack
			return {L};
		}
		return {L, luaL_ref(L, LUA_REGISTRYINDEX)};
	}

	template <typename T>
	Ref operator[](T &&key) const {
		StackOps<T>::Push(L, std::forward<T>(key));
		int keyref = luaL_ref(L, LUA_REGISTRYINDEX);
		Push(L);
		int tableref = luaL_ref(L, LUA_REGISTRYINDEX);
		return {L, keyref, tableref};
	}

	void Push(lua_State *L) const {
		if (tableref != LUA_REFNIL) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, tableref);
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			lua_gettable(L, -2);
			lua_remove(L, -2);
		} else {
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		}
	}

	int Type() const {
		if (ref != LUA_REFNIL) {
			stack_pop p(L, 1);
			Push(L);
			return lua_type(L, -1);
		} else {
			return LUA_TNIL;
		}
	}

	inline bool IsNil() const { return Type() == LUA_TNIL; }
	inline bool IsNumber() const { return Type() == LUA_TNUMBER; }
	inline bool IsString() const { return Type() == LUA_TSTRING; }
	inline bool IsTable() const { return Type() == LUA_TTABLE; }
	inline bool IsFunction() const { return Type() == LUA_TFUNCTION; }
	inline bool IsUserdata() const { return Type() == LUA_TUSERDATA; }
	inline bool IsThread() const { return Type() == LUA_TTHREAD; }
	inline bool IsLightUserdata() const { return Type() == LUA_TLIGHTUSERDATA; }

	template <typename T>
	void Append(T &&v) const {
		Push(L);
		StackOps<T>::Push(L, std::forward<T>(v));
		luaL_ref(L, -2);
		lua_pop(L, 1);
	}

	int Length() const {
		stack_pop p(L, 1);
		Push(L);
		return _interlua_rawlen(L, -1);
	}

	template <typename T>
	inline T As() const {
		stack_pop p(L, 1);
		Push(L);
		return StackOps<T>::Get(L, -1);
	}

	template <typename T>
	inline operator T() const {
		return As<T>();
	}
};

static inline Ref FromStack(lua_State *L, int index) {
	lua_pushvalue(L, index);
	return {L, luaL_ref(L, LUA_REGISTRYINDEX)};
}

template <typename T>
static inline Ref New(lua_State *L, T &&v) {
	StackOps<T>::Push(L, std::forward<T>(v));
	return {L, luaL_ref(L, LUA_REGISTRYINDEX)};
}

static inline Ref NewTable(lua_State *L) {
	lua_newtable(L);
	return {L, luaL_ref(L, LUA_REGISTRYINDEX)};
}

static inline Ref Global(lua_State *L, const char *name) {
	lua_getglobal(L, name);
	return {L, luaL_ref(L, LUA_REGISTRYINDEX)};
}

#define _stack_ops_ref(T)					\
template <>							\
struct StackOps<T> {						\
	static inline void Push(lua_State *L, const Ref &v) {	\
		v.Push(L);					\
	}							\
	static inline Ref Get(lua_State *L, int index) {	\
		return FromStack(L, index);			\
	}							\
};

_stack_ops_ref(Ref)
_stack_ops_ref(Ref&)
_stack_ops_ref(const Ref&)

#undef _stack_ops_ref

} // namespace InterLua
