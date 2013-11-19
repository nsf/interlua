#include "interlua.hh"
#include <tuple>

namespace InterLua {

template <int I, typename T, bool STOP>
struct tuple_pusher;

template <int I, typename T>
struct tuple_pusher<I, T, true> {
	static inline int push(lua_State*, T&&) { return 0; }
};

template <int I, typename T>
struct tuple_pusher<I, T, false> {
	static inline int push(lua_State *L, T &&r) {
		const int n = StackOps<typename std::tuple_element<I, T>::type>::
			Push(L, std::get<I>(std::forward<T>(r)));
		return n + tuple_pusher<
			I+1,
			T,
			I+1 == std::tuple_size<T>::value
		>::push(L, std::forward<T>(r));
	}
};

template <typename T>
static inline int recursive_tuple_push(lua_State *L, T &&r) {
	return tuple_pusher<
		0,
		T,
		std::tuple_size<T>::value == 0 // shall we stop?
	>::push(L, std::forward<T>(r));
}

template <typename ...Args>
struct StackOps<std::tuple<Args...>> {
	static inline int Push(lua_State *L, std::tuple<Args...> v) {
		return recursive_tuple_push(L, std::move(v));
	}
};

#define _stack_ops_tuple(T)						\
template <typename ...Args>						\
struct StackOps<T> {							\
	static inline int Push(lua_State *L, T v) {			\
		return recursive_tuple_push(L, std::forward<T>(v));	\
	}								\
};

_stack_ops_tuple(std::tuple<Args...>&)
_stack_ops_tuple(std::tuple<Args...>&&)
_stack_ops_tuple(const std::tuple<Args...>&)

#undef _stack_ops_tuple

} // namespace InterLua
