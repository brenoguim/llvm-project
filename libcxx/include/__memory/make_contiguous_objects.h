#ifndef _LIBCPP___MEMORY_MAKE_CONTIGUOUS_OBJECTS_H
#define _LIBCPP___MEMORY_MAKE_CONTIGUOUS_OBJECTS_H

#include <__memory/uninitialized_algorithms.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace xtd
{
template<class... Args>
struct alignas(Args...) aligned_storage {
    struct alignas(Args...) dummy {};
    std::byte buf[alignof(dummy)];
};


template<class T>
struct span
{
    span() = default;
    span(T* b, T* e) : m_begin(b), m_end(e) {}
    using type = T;
    auto begin() const { return m_begin; }
    auto end() const { return m_end; }
    std::size_t size() const { return m_end - m_begin; }
    T& operator[](std::size_t i) { return m_begin[i]; }
    T* m_begin {nullptr};
    T* m_end {nullptr};
};
template<class T> span(T*, T*) -> span<T>;

inline constexpr std::uintptr_t findDistanceOfNextAlignedPosition(uintptr_t pos, std::size_t desiredAlignment)
{
    return pos ? ((pos - 1u + desiredAlignment) & -desiredAlignment) - pos : 0;
}

template<class T>
struct ArraySize
{
    ArraySize(std::size_t count) : m_count(count) {}
    std::size_t numBytes() const { return m_count*sizeof(T); }
    std::size_t m_count;
};

template<class T>
void addRequiredBytes(ArraySize<T>& init, std::size_t& pos)
{
    pos += findDistanceOfNextAlignedPosition(pos, alignof(T)) + init.numBytes();
}

template<class T>
void setRange(ArraySize<T>& init, span<T>& rng, std::byte*& mem)
{
    mem += findDistanceOfNextAlignedPosition((uintptr_t)mem, alignof(T));
    rng.m_begin = reinterpret_cast<T*>(mem);
    mem += init.numBytes();
    rng.m_end = reinterpret_cast<T*>(mem);
}

template<class Alloc, class... Args>
auto make_contiguous_layout(const Alloc& alloc, ArraySize<Args>... args) -> std::tuple<span<Args>...>
{
    std::size_t numBytes = 0;
    ((addRequiredBytes(args, numBytes), ...));

    using Storage = aligned_storage<Args...>;

    numBytes += findDistanceOfNextAlignedPosition(numBytes, alignof(Storage));
    auto numElements = numBytes / sizeof(Storage);

    __allocator_traits_rebind_t<Alloc, Storage> __mem_alloc(alloc);
    auto amem = std::addressof(allocator_traits<decltype(__mem_alloc)>::allocate(__mem_alloc, numElements)[0]);
    auto mem = (std::byte*)amem;

    std::tuple<span<Args>...> r;

    std::apply([&] (auto&... targs) {
        (setRange(args, targs, mem),...);
    }, r);

    return r;
}

struct default_ctor_t{}; static constexpr default_ctor_t default_ctor;
struct ctor_t{}; static constexpr ctor_t ctor;
struct value_ctor_t{}; static constexpr value_ctor_t value_ctor;
struct fill_ctor_t{}; static constexpr fill_ctor_t fill_ctor;
struct aggregate_t{}; static constexpr aggregate_t aggregate;
struct input_iterator_t{}; static constexpr input_iterator_t input_iterator;
struct functor_t{}; static constexpr functor_t functor;

template<class C, class T>
struct InitializerConfiguration
{
    using command = C;
    std::size_t m_count;
    T m_args;
};

template<class... Args>
auto arg(ctor_t, std::size_t count, Args&&... args)
{
    return InitializerConfiguration<ctor_t, decltype(_VSTD::forward_as_tuple(args...))>{count, _VSTD::forward_as_tuple(args...)};
}

template<class... Args>
auto arg(value_ctor_t, std::size_t count, Args&&... args)
{
    return InitializerConfiguration<value_ctor_t, decltype(_VSTD::forward_as_tuple(args...))>{count, _VSTD::forward_as_tuple(args...)};
}

template<class... Args>
auto arg(fill_ctor_t, std::size_t count, Args&&... args)
{
    return InitializerConfiguration<fill_ctor_t, decltype(_VSTD::forward_as_tuple(args...))>{count, _VSTD::forward_as_tuple(args...)};
}

template<class... Args>
auto arg(aggregate_t, std::size_t count, Args&&... args)
{
    return InitializerConfiguration<aggregate_t, decltype(std::forward_as_tuple(args...))>{count, std::forward_as_tuple(args...)};
}

inline auto arg(std::size_t count) { return arg(ctor, count); }
inline auto arg(default_ctor_t, std::size_t count) { return InitializerConfiguration<default_ctor_t, int>{count, 0}; }

template<class It>
auto arg(input_iterator_t, std::size_t count, const It& it) { return InitializerConfiguration<input_iterator_t, It>{count, it}; }

template<class Fn>
auto arg(functor_t, std::size_t count, const Fn& fn) { return InitializerConfiguration<functor_t, const Fn&>{count, fn}; }

template<class Alloc, class It, class... Args>
void uninitialized_allocator_construct_n(Alloc& __alloc, It first, size_t n, Args&&... args)
{
    It current = first;
    auto __guard = std::__make_exception_guard([&]() {
        std::__allocator_destroy_multidimensional(__alloc, first, current);
    });

    for (; n != 0; --n, ++current) {
        allocator_traits<Alloc>::construct(__alloc, std::addressof(*current), args...);
    }

    __guard.__complete();
}

template<class Alloc, class Tup, class T, class... U>
void initRanges(const Alloc& alloc, Tup& t, T&& ac, U&&... acs)
{
    constexpr int tupPos = std::tuple_size_v<Tup> - sizeof...(acs) - 1;

    auto& rng = std::get<tupPos>(t);

    using TheRng = decltype(rng);
    using TheRngNoRef = std::remove_reference_t<TheRng>;
    using TheT = typename TheRngNoRef::type;
    using DT = std::remove_cv_t<std::remove_reference_t<T>>;

    __allocator_traits_rebind_t<Alloc, TheT> __value_alloc(alloc);

    using Command = typename DT::command;

    if constexpr (std::is_same_v<Command, default_ctor_t>)
        std::uninitialized_default_construct_n(rng.begin(), rng.size());

    if constexpr (std::is_same_v<Command, ctor_t>)
        std::apply([&] (auto&&... args) {
            uninitialized_allocator_construct_n(__value_alloc, rng.begin(), rng.size(), args...);
        }, ac.m_args);

    if constexpr (std::is_same_v<Command, value_ctor_t>)
        std::__uninitialized_allocator_value_construct_n_multidimensional(__value_alloc, rng.begin(), rng.size());

    if constexpr (std::is_same_v<Command, fill_ctor_t>)
        std::apply([&] (auto&&... args) {
            ::std::__uninitialized_allocator_fill_n_multidimensional(__value_alloc, rng.begin(), rng.size(), args...);
        }, ac.m_args);

    constexpr bool useAlloc = std::is_same_v<Command, value_ctor_t> || std::is_same_v<Command, fill_ctor_t>;
    auto __guard = std::__make_exception_guard([&]() {
        if constexpr (useAlloc)
            std::__allocator_destroy_multidimensional(__value_alloc, rng.begin(), rng.end());
        else
            std::__reverse_destroy(rng.begin(), rng.end());
    });

    if constexpr (sizeof...(acs)>0)
        initRanges(alloc, t, acs...);

    __guard.__complete();
}


inline std::size_t get_size(std::size_t sz) { return sz; }
template<class T>
std::enable_if_t<!std::is_integral_v<T>, std::size_t> get_size(const T& t) { return t.m_count; }

inline auto convert_arg(std::size_t sz) { return arg(sz); }
template<class T>
std::enable_if_t<!std::is_integral_v<T>, T> convert_arg(const T& t) { return t; }

struct MemGuard
{
    ~MemGuard() { ::operator delete(m_mem); }
    void release() { m_mem = nullptr; }
    void* m_mem;
};

template<class Alloc, class... Args, class... Initializers>
auto make_contiguous_objects(const Alloc& alloc, Initializers... args) -> std::tuple<span<Args>...>
{
    auto layout = make_contiguous_layout<Alloc, Args...>(alloc, get_size(args)...);
    MemGuard mg{std::get<0>(layout).begin()};

    initRanges(alloc, layout, convert_arg(args)...);

    mg.release();
    return layout;
}

template<class T, class U>
T* get_adjacent_address(U* end)
{
    auto endi = (std::uintptr_t)end;
    return (T*) (endi + findDistanceOfNextAlignedPosition(endi, alignof(T)));
}

template<class Alloc, class... Args>
void destroy_contiguous_objects(Alloc& alloc, const std::tuple<span<Args>...>& t)
{
    std::apply([](auto&... sp) {
         (std::__reverse_destroy(sp.begin(), sp.end()), ...);
    }, t);

    using Storage = aligned_storage<Args...>;
    using StorageAlloc = __allocator_traits_rebind_t<Alloc, Storage>;
    using PointerTraits = pointer_traits<typename allocator_traits<StorageAlloc>::pointer>;

    StorageAlloc __mem_alloc(alloc);

    auto begin = get_adjacent_address<Storage>(std::get<0>(t).begin());
    auto end = get_adjacent_address<Storage>(std::get<sizeof...(Args)-1>(t).end());
    auto numBytes = (uintptr_t)end - (uintptr_t)begin;
    auto numElements = numBytes/sizeof(Storage);

    allocator_traits<StorageAlloc>::deallocate(__mem_alloc, PointerTraits::pointer_to(*begin), numElements);
}

}
_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_MAKE_CONTIGUOUS_OBJECTS_H
