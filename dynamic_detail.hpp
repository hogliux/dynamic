#pragma once

#include <type_traits>
#include <tuple>
#include <utility>
#include <concepts>
#include "boost/pfr.hpp"
#include "fixed_string.hpp"

namespace dynamic
{

// Forward declarations needed by detail namespace
template <typename T, fixstr::fixed_string Name> class Field;
template <typename T> class Fundamental;
template <typename T> class Record;
template <typename T> class Array;
template <typename T> class Map;
class Value;

//=============================================================================
// Implementation details - not part of the public API
//=============================================================================
namespace detail
{
/**
 * @brief Filters a tuple, keeping only elements that satisfy the Predicate
 *
 * @tparam Predicate A template that provides a ::value bool for each type
 * @param tp The tuple to filter
 * @return A new tuple containing only elements where Predicate<T>::value is true
 */
template <template<typename> class Predicate, typename Tuple>
auto filter_tuple(Tuple&& tp)
{
    return std::apply([]<typename... Ts>(Ts&&... args) {
        // Helper that returns either a single-element tuple or empty tuple
        auto maybe_keep = []<typename T>(T&& arg) {
            if constexpr (Predicate<T>::value)
                return std::tuple<T>(std::forward<T>(arg));
            else
                return std::tuple<>();
        };
        return std::tuple_cat(maybe_keep(std::forward<Ts>(args))...);
    }, std::forward<Tuple>(tp));
}

/**
 * @brief Trait to check if a lambda can be invoked with a given type
 *
 * Provides a nested Predicate template that evaluates to true_type if
 * Lambda can be called with an argument of type T.
 */
template <typename Lambda>
struct DoesLambdaSupportType
{
    template <typename T>
    struct Predicate : std::bool_constant<requires { std::declval<Lambda>()(std::declval<T>()); }> {};
};

//-----------------------------------------------------------------------------
// Field type detection
//-----------------------------------------------------------------------------

template <typename T> struct is_field_helper : std::false_type {};
template <typename T, fixstr::fixed_string Name> struct is_field_helper<Field<T, Name>> : std::true_type {};

/// Predicate that is true if T is a Field<> specialization
template <typename T> struct is_field { static constexpr auto value = is_field_helper<std::decay_t<T>>::value; };

/// Returns the number of Field<> members in struct T
template <typename T>
constexpr std::size_t num_fields()
{
    if constexpr (std::is_aggregate_v<T>)
        return std::tuple_size_v<decltype(filter_tuple<is_field>(boost::pfr::structure_tie(std::declval<T&>())))>;

    return 0;
}

/// Helper to decay all types in a tuple
template <typename T> struct decay_tuple;
template <typename... Types> struct decay_tuple<std::tuple<Types...>>
{
    using type = std::tuple<std::decay_t<Types>...>;
};

//-----------------------------------------------------------------------------
// Compile-time field lookup by name
//-----------------------------------------------------------------------------

/**
 * @brief Helper to find a field by compile-time name in a parameter pack
 *
 * @tparam FieldName The compile-time string name to search for
 * @tparam TypeErasedValueT Deferred type parameter (defaults to Value) to allow
 *         this template to be defined before Value is complete
 */
template <fixstr::fixed_string FieldName, typename TypeErasedValueT = class Value>
struct FindFieldHelper
{
    /// Base case: no fields left, return invalid sentinel
    static constexpr auto& eval() { return TypeErasedValueT::kInvalid; }

    /// Recursive case: check if field0's name matches, else recurse
    template <typename Field0, typename... Fields>
    static constexpr auto&& eval(Field0 && field0, Fields && ...fields)
    {
        // Extract the name from the Field type at compile-time
        static auto constexpr fieldname = std::invoke(
            [] <typename T, fixstr::fixed_string Name> (std::type_identity<Field<T, Name>>)
            { return Name; },
            std::type_identity<std::remove_cvref_t<Field0>>()
        );

        // Dispatch based on whether the name matches
        return std::invoke(
            cxxutils::multilambda(
                // Name matches: return this field
                [] <typename Field0_, typename... Fields_>
                (std::bool_constant<true>,  Field0_ && field0_, Fields_ && ...) -> auto&&
                { return field0_; },
                // Name doesn't match: recurse to next field
                [] <typename Field0_, typename... Fields_>
                (std::bool_constant<false>, Field0_ &&        , Fields_ && ...fields_) -> auto&&
                { return FindFieldHelper::eval(std::forward<Fields_>(fields_)...); }
            ),
            std::bool_constant<fieldname == FieldName>(),
            std::forward<Field0>(field0),
            std::forward<Fields>(fields)...
        );
    }
};

template <typename T>
using BaseTypeFor = std::conditional_t<requires (T t) { [] <typename U> (Array<U>&){}(t); } || requires (T t) { [] <typename U> (Map<U>&){}(t); }, T,
                                       std::conditional_t<num_fields<T>() >= 1, Record<T>, Fundamental<T>>>;

template<template<typename, typename> class Cls, typename T>
struct BindFirst
{
    template <typename U>
    struct Result
    {
        using type = Cls<T, U>;
    };
};

/// Helper to transform tuple element types
template <typename Tuple, template<typename> class Transform>
struct transform_tuple;

template <typename... Ts, template<typename> class Transform>
struct transform_tuple<std::tuple<Ts...>, Transform> {
    using type = std::tuple<typename Transform<Ts>::type...>;
};

// Helper class to transform a tuple to a variant
template <template<typename...> class Transform, typename Tuple>
struct apply_tuple;

template <template<typename...> class Transform, typename... Ts>
struct apply_tuple<Transform, std::tuple<Ts...>> {
    using type = Transform<Ts...>;
};

template <typename T> struct add_lvalue_ref { using type = T&; };
template <typename T> struct add_const_lvalue_ref { using type = T const&; };
template <typename T> struct add_reference_wrapper { using type = std::reference_wrapper<T>; };
template <typename T> struct add_const_reference_wrapper { using type = std::reference_wrapper<T const>; };
} // namespace detail

} // namespace dynamic
