/**
 * @file dynamic.hpp
 * @brief A C++ reflection system using Field<> template wrappers
 *
 * This file implements a compile-time reflection system that allows structs to be
 * introspected at runtime. By wrapping each struct member in a Field<T, Name> template,
 * you enable:
 *   - Iteration through struct field names and values
 *   - Type-erased access via Value base class
 *   - Visitor pattern support for handling different types
 *   - Change listeners that propagate through nested struct hierarchies
 *
 * Usage example:
 *   struct Point {
 *       Field<float, "x"> x;
 *       Field<float, "y"> y;
 *   };
 *   Record<Point> point;
 *   point("x"_fld) = 3.14f;  // Access field by compile-time name
 */

#pragma once

#ifndef JUCE_SUPPORT
 #define JUCE_SUPPORT (JUCE_MAC || JUCE_LINUX || JUCE_IOS || JUCE_ANDROID || JUCE_WINDOWS)
#endif

#include <cstdint>
#include <string>
#include <memory>
#include <sstream>
#include <type_traits>
#include <concepts>
#include <map>
#include <iostream>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <span>
#include <utility>
#include <variant>
#include "fixed_string.hpp"
#include "CxxUtilities.hpp"
#include "dynamic_detail.hpp"

namespace dynamic
{

/**
 * @brief Represents a path to a nested field within a struct hierarchy
 *
 * ID stores a sequence of field names representing the traversal from a root
 * struct to a nested field. For example, for a struct Line containing Point structs,
 * the path to the x coordinate of the start point would be ["start", "x"].
 *
 * Paths can be constructed from "/" delimited strings and converted back to strings.
 */
struct ID : std::vector<std::string>
{
    /// Default constructor - empty path
    ID() = default;

    /// Copy constructor
    ID(ID const& o) : std::vector<std::string>(o) {}

    /// Move constructor
    ID(ID && o) : std::vector<std::string>(std::move(o)) {}

    /// Construct from "/" delimited string
    ID(std::string const& path) : ID(fromString(path)) {}

    /// Construct from vector of path elements
    ID(std::vector<std::string> && o) : std::vector<std::string>(std::move(o)) {}

    /// Copy assignment operator
    ID& operator=(ID const& o);

    /// Move assignment operator
    ID& operator=(ID && o);

    /**
     * @brief Convert the path to a "/" delimited string
     * @return String representation like "field/subfield/leaf"
     */
    std::string toString() const;

    /**
     * @brief Parse a "/" delimited string into a path
     * @param path String like "field/subfield/leaf"
     * @return ID containing the parsed path elements
     */
    static ID fromString(std::string const& path);
};

/**
 * @brief Compile-time string wrapper for use with the "_fld" literal
 *
 * This struct holds a compile-time fixed string and is used as a tag type
 * for accessing struct fields by name. Created via the "_fld" user-defined literal.
 *
 * @tparam S The compile-time fixed string representing the field name
 *
 * @see operator""_fld
 */
template <fixstr::fixed_string S>
struct CompileTimeString { static constexpr auto value = S; };

// Forward declarations
template <typename T> class Fundamental;
template <typename T> class Record;
class Object;
class Value;
class Invalid;

template <typename T> class Array;
template <typename T> class Map;

// Forward declaration of Field (needed by detail namespace utilities)
template <typename T, fixstr::fixed_string Name> class Field;

// Forward declarations for MetaType system
class MetaType;

/**
 * @brief Describes a single field within a Record's MetaType
 *
 * Provides the field name and a function pointer to lazily obtain the
 * MetaType of the field's underlying type, avoiding static initialization
 * order issues with recursive/nested types.
 */
struct FieldDescriptor
{
    std::string_view fieldname;
    MetaType const& (*metaType)();
};

/**
 * @brief RAII token for managing listener lifetime
 *
 * ListenerToken provides automatic listener removal via RAII. When the token
 * is destroyed, the associated listener is automatically removed.
 *
 * Tokens are move-only and cannot be copied. Destroying a moved-from token
 * has no effect.
 *
 * @code
 * // Listener is active while token is in scope
 * auto token = myValue.addListener([](auto const& val, auto const& newVal) {
 *     std::cout << "Value changed to: " << newVal << std::endl;
 * });
 * // Listener is removed when token goes out of scope
 * @endcode
 */
class ListenerToken
{
public:
    /// Default constructor - creates an empty token
    ListenerToken() = default;

    // Default destructor
    ~ListenerToken() = default;

    /// Move constructor
    ListenerToken(ListenerToken&&) noexcept = default;

    /// Move assignment
    ListenerToken& operator=(ListenerToken&&) noexcept = default;

    /// Deleted copy constructor
    ListenerToken(ListenerToken const&) = delete;

    /// Deleted copy assignment
    ListenerToken& operator=(ListenerToken const&) = delete;

private:
    template <typename> friend class Fundamental;
    template <typename> friend class Array;
    template <typename> friend class Map;
    friend class Object;
    
    struct private_constructor_t {};

    struct Impl {};

    ListenerToken(private_constructor_t) : token(std::make_shared<Impl>()) {}
    std::shared_ptr<Impl> token;
};

//=============================================================================
// Public API classes
//=============================================================================

/**
 * @brief Abstract base class for type-erased values in the reflection system
 *
 * Value provides a common interface for accessing values of unknown
 * type at runtime. It supports:
 *   - Type identification via type() and isStruct()
 *   - Validity checking via isValid() and operator bool()
 *   - Type-safe visitation via visit() with lambda overloads
 *   - Field naming for struct members via fieldname()
 *
 * Derived classes include:
 *   - Invalid: Sentinel for invalid/missing fields
 *   - Fundamental<T>: Concrete wrapper for primitive types
 *   - Object: Base for struct types with fields
 *   - Field<T, Name>: Named field within a struct
 *
 * @note The visitor pattern allows safe access to the underlying value without
 *       knowing its type at compile time. The lambda will be called with the
 *       actual type only if it supports that type.
 */
class Value
{
private:
    /// Tuple of all primitive types supported by the visitor pattern
    using SupportedFundamentalTypes = std::tuple<
        int8_t, int16_t, int32_t, int64_t,
        float, double,
        bool,
        std::string, ID
    >;

public:
    /// Global singleton representing an invalid/missing value
    static Invalid& kInvalid;

    virtual constexpr ~Value() = default;

    /// Returns the std::type_info for the underlying value type
    virtual std::type_info const& type() const = 0;

    /// Returns the MetaType descriptor for this value's type
    virtual MetaType const& metaType() const = 0;

    /// Returns true if this value represents a struct with fields
    virtual bool isStruct() const { return false; }

    /// Returns true if this value is valid (not Invalid)
    virtual bool isValid() const = 0;

    /// Returns the field name if this value is a named field, empty otherwise
    virtual std::string fieldname() const { return {}; }

    /// Converts to bool based on validity (same as isValid())
    operator bool() const { return isValid(); }

    /**
     * @brief Visit the underlying value with a type-safe lambda
     *
     * The lambda will be called with the underlying value if it supports
     * that type. The lambda can accept any subset of the supported types.
     *
     * @tparam Lambda A callable that accepts at least one SupportedPrimitiveType
     * @param lambda The visitor lambda
     * @return The common return type of all lambda invocations, or void
     *
     * @code
     * value.visit([](auto& v) { std::cout << v; });  // Generic visitor
     * value.visit([](int& i) { i *= 2; });           // int-only visitor
     * @endcode
     */
    template <typename Lambda>
    auto visit(this auto&& self, Lambda && lambda) -> decltype(auto);

    /// Returns false if the type is a container or struct or similar *and* contains child
    /// fields of type Field<> below. Otherwise returns true and T will be treated as an
    /// opaque type (i.e. you won't receive callbacks if any of the child fields changed.)
    template <typename T>
    static constexpr bool isOpaque();

    /**
     * @brief Assign another the value of another Value to the recipient
     * 
     * @return Returns true on success. Will assert if the underlying
     * type of other and the recipient do not match.
     */
    virtual bool assign(Value const& other) = 0;

    /** Assignment operator - uses above assign method */
    Value& operator=(Value const&);

protected:
    friend class Invalid;
    friend class Object;
    template <typename T> friend class Fundamental;
    template <typename T> friend class Record;

    using TypesVariant = detail::apply_tuple<std::variant, detail::transform_tuple<SupportedFundamentalTypes, detail::add_reference_wrapper>::type>::type;
    using ConstTypesVariant = detail::apply_tuple<std::variant, detail::transform_tuple<SupportedFundamentalTypes, detail::add_const_reference_wrapper>::type>::type;

    virtual TypesVariant visit_helper();
    virtual ConstTypesVariant visit_helper() const;

    
    constexpr Value() = default;
    // don't copy parent
    Value(Value const&);
    Value(Value&& o);

    /**
     * @brief Binds a listener token to a context for automatic cleanup
     *
     * Associates a ListenerToken with a weak_ptr to a context. When the
     * context expires, the binding can be detected and removed, which
     * destroys the token and removes the listener.
     */
    struct ListenerBinding
    {
        std::weak_ptr<void> context;
        ListenerToken token;
    };

    Object* parent = nullptr;
    static thread_local std::size_t recursiveListenerDisabler;
};

/**
 * @brief Sentinel type representing an invalid or missing field
 *
 * Invalid is returned when a field lookup fails (e.g., accessing a
 * non-existent field name). It always returns false for isValid() and
 * can be used in boolean context to check for lookup failures.
 *
 * @code
 * auto& field = myStruct("nonexistent"_fld);
 * if (!field) {
 *     std::cout << "Field not found!" << std::endl;
 * }
 * @endcode
 */
class Invalid : public Value
{
public:
    /// Compile-time constant indicating this is not a valid value
    static constexpr auto kIsValid = false;

    constexpr Invalid() = default;

    std::type_info const& type() const override { return typeid(void); }
    MetaType const& metaType() const override;
    bool isValid() const override { return false; }
    constexpr operator bool() const { return false; }
    bool assign(Value const&) override { return false; }
};

/**
 * @brief Abstract base class for struct types containing Field<> members
 *
 * Object extends Value to add struct-specific functionality:
 *   - Field iteration via typeErasedFields()
 *   - Runtime field access by name via operator()(string_view)
 *   - Hierarchical path-based field access via getchild()
 *   - Recursive child change listeners via addChildListener()
 *
 * When listeners are attached via addChildListener(), changes to any nested
 * field will bubble up through the parent chain, allowing you to observe
 * changes at any level of the struct hierarchy.
 *
 * @see Record<T> for the concrete implementation
 */
class Object : public Value
{
public:
    /**
     * @brief Type of operation that triggered a listener callback
     */
    enum class Operation
    {
        add,      ///< Element was added to a container
        remove,   ///< Element was removed from a container
        modify    ///< Value was modified
    };

     /// Copy constructor (does not copy listeners !!)
    Object(Object const& o);

    /// Move constructor
    Object(Object&& o);

    bool isStruct() const override { return true; }
    virtual bool isMapOrArray() const { return false; }

    // If this is a map or an array, then this method returns the element type.
    // If this is a record/struct then there is no single element type. Every field
    // could have a different type and so this method returns an invlaid type_info
    // (i.e. the type_info for void).
    virtual std::type_info const& elementType() const { return typeid(void); }

    /**
     * @brief Assign a value to a child element of this Object
     * 
     * Tries to assign a value to a child element of this Object. If underlying object
     * is dynamic (i.e. a Map or Array and not a Record) and a child with this name
     * does not exist, it may be created.
     * 
     * @return True if succesful. There may be multiple reasons why this may not succeeed.
     *         For example, if the underlying Object is a C++ struct and there is no field
     *         with this name. Or if the underlying object is an array but name is not
     *         an integer etc.
     */
    virtual bool assignChild(std::string const& /*name*/, Value const& /*newValue*/) { assert(false); return false; }

    /**
     * @brief Removes a child element from this Object
     * 
     * Tries to remove a child element from an Object.
     * 
     * @return True if succesful. False if the element does not exist or cannot be removed
     * (for example Struct).
     */
    virtual bool removeChild(std::string const& /*name*/) { assert(false); return false; }

    /**
     * @brief Register a listener for changes to any child field (token-based)
     *
     * The listener will be called when any field (including deeply nested fields)
     * changes. The listener remains active until the returned token is destroyed.
     *
     * @param lambda Callback: (ID const& idToChangedObject, Operation operation, Object const& parent, Value const& newValue)
     * @return ListenerToken that removes the listener when destroyed
     */
    template <std::invocable<ID const&, Operation, Object const&, Value const&> Lambda>
    ListenerToken addChildListener(Lambda && lambda);

    /**
     * @brief Register a listener for changes to any child field (auto-cleanup)
     *
     * The listener will be called when any field (including deeply nested fields)
     * changes. The listener is automatically removed when the context expires.
     *
     * @tparam Context Type deriving from std::enable_shared_from_this
     * @param context Pointer to the listener owner (kept via weak_ptr)
     * @param lambda Callback: (ID const& idToChangedObject, Operation operation, Object const& parent, Value const& newValue)
     */
    template <class Context, std::invocable<ID const&, Operation, Object const&, Value const&> Lambda>
    void addChildListener(std::enable_shared_from_this<Context>* context, Lambda && lambda);

    template <class Context, std::invocable<ID const&, Operation, Object const&, Value const&> Lambda>
    void addChildListener(std::weak_ptr<Context>&& context, Lambda && lambda);

   #if JUCE_SUPPORT
    template <class ComponentType, std::invocable<ID const&, Operation, Object const&, Value const&> Lambda>
    void addChildListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>;
   #endif

    /// Returns a vector of references to all fields (const version)
    virtual std::vector<std::reference_wrapper<Value const>> typeErasedFields() const { assert(false); return {}; }

    /// Returns a vector of references to all fields (mutable version)
    virtual std::vector<std::reference_wrapper<Value>> typeErasedFields() { assert(false); return {}; }

    /**
     * @brief Access a field by name at runtime
     *
     * @param fldname The name of the field to access
     * @return Reference to the field, or kInvalid if not found
     */
    auto operator()(this auto&& self, std::string_view fldname)
        -> std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                              Value const&,
                              Value&>;

    /**
     * @brief Access a nested field using a path
     *
     * Traverses the object hierarchy following the path elements.
     * Each element in the path represents a field name at that level.
     *
     * @param subid The path to the desired field (sequence of field names)
     * @return Reference to the field, or kInvalid if the path is invalid
     */
    auto getchild(this auto& self, ID subid) -> std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, Value const&, Value&>;

    // Copy assignment operator (does not copy listeners)
    Object& operator=(Object const&);

    Object& operator=(Object&&);

    // Workaround a compiler bug on macOS: the abstract Object is only created with a std::declval
    // in a required cause. This should not require us to implement virtual base methods, but
    // apparently macOS clang requires this for some reason
    std::type_info const& type() const override { assert(false); return typeid(void); }
    MetaType const& metaType() const override;
    bool isValid() const override { assert(false); return false; }
    bool assign(Value const&) override { assert(false); return false; }

protected:
    Object() = default;

private:
    template <typename T>
    friend class Fundamental;

    template <typename T>
    friend class Array;

    template <typename T>
    friend class Map;

    void callChildListeners(ID const& id, Operation op, Object const& parentOfChangedValue, Value const& newValue) const;

    using ChildListenerFunction = std::function<void(ID const&, Operation, Object const&, Value const&)>;

    mutable std::map<std::weak_ptr<ListenerToken::Impl>, ChildListenerFunction, std::owner_less<std::weak_ptr<ListenerToken::Impl>>> childListeners;
    mutable std::vector<Value::ListenerBinding> managedChildListeners;
};

/**
 * @brief Concrete wrapper for a value of type T with change notification support
 *
 * Fundamental<T> provides a type-safe container for values that:
 *   - Supports the visitor pattern from Value
 *   - Notifies listeners when the value changes
 *   - Can be used both for primitive types and struct types containing Fields
 *
 * For primitive types (int, float, string, etc.), Fundamental<T> derives from Value.
 * For struct types with Field<> members, Fundamental<T> derives from Object.
 *
 * @tparam T The underlying value type (must be a SupportedPrimitiveType or a struct with Fields)
 *
 * @see Record<T> for extended struct functionality
 * @see Field<T, Name> for named struct members
 */
template <typename T>
class Fundamental : public std::conditional_t<Value::isOpaque<T>(), Value, Object>
{
public:
    /// True if T is a struct containing Field<> members
    static constexpr auto kIsOpaque = Value::isOpaque<T>();

    // T must either be a struct with Fields (see Field class below) or it must be one of SupportedFundamentalTypes
    static_assert(
        (! kIsOpaque) ||
        (std::invoke(
            [] <typename... Type> (std::type_identity<std::tuple<Type...>>)
            {
                return (std::is_same_v<T, Type> || ...);
            },
            std::type_identity<Value::SupportedFundamentalTypes>()
        ))
    );

    using Base = std::conditional_t<kIsOpaque, Value, Object>;

    /// Compile-time constant indicating this is always a valid value
    static constexpr auto kIsValid = true;

    /// Default constructor - creates a Fundamental with default-initialized value
    Fundamental();

    /// Construct from underlying value
    Fundamental(T underlying_);

    /// Copy constructor
    Fundamental(Fundamental const& o);

    /// Move constructor
    Fundamental(Fundamental&& o);

    // Destructor
    ~Fundamental() override;

    /// Returns the type_info for the underlying type T
    std::type_info const& type() const override { return typeid(T); }

    /// Returns the MetaType for the underlying type T (static, no instance needed)
    static MetaType const& meta();

    /// Returns the MetaType for this value's underlying type
    MetaType const& metaType() const override;

    /// Always returns true - Fundamental values are always valid
    bool isValid() const override { return true; }

    /// Always returns true - Fundamental values are always valid (disabled for bool to avoid conflict with operator T())
    constexpr operator bool() const requires (!std::is_same_v<T, bool>) { return true; }

    /// Assign new value and notify listeners
    Fundamental& operator=(T const& newValue);

    /// Copy assignment operator with listener notification
    Fundamental& operator=(Fundamental const& newValue);

    /// Move assignment operator with listener notification
    Fundamental& operator=(Fundamental && newValue);

    /// Returns the underlying value (read-only access)
    T const& operator()() const { return underlying; }

    /// Mutable member access for struct types — allows chaining through Field members
    /// while preserving listener safety (mutations go through Field::operator= or set())
    T*       operator->()       requires (!kIsOpaque) { return &underlying; }
    T const* operator->() const requires (!kIsOpaque) { return &underlying; }

    /**
     * @brief Set the value and notify all listeners
     * @param newValue The new value to set
     */
    void set(T const& newValue);

    /// @overload Set with move semantics
    void set(T && newValue);

    /**
     * @brief Modify the value via a lambda and notify listeners
     *
     * Creates a copy of the value, passes it to the lambda for modification,
     * then notifies listeners and updates the underlying value.
     *
     * @param lambda A callable that takes T& and modifies it
     */
    template <std::invocable<T&> Lambda>
    void mutate(Lambda && lambda);

    /// Implicit conversion to the underlying type
    operator T() const { return underlying; }

    /**
     * @brief Register a listener for value changes (token-based)
     *
     * The listener will be called whenever this value changes via set() or mutate().
     * The listener remains active until the returned token is destroyed.
     *
     * @param lambda Callback: (Fundamental<T> const&, T const& newValue)
     * @return ListenerToken that removes the listener when destroyed
     */
    template <std::invocable<Fundamental<T> const&> Lambda>
    ListenerToken addListener(Lambda && lambda) const;

    /**
     * @brief Register a listener for value changes (auto-cleanup)
     *
     * The listener will be called whenever this value changes via set() or mutate().
     * The listener is automatically removed when the context expires.
     *
     * @tparam Context Type deriving from std::enable_shared_from_this
     * @param context Pointer to the listener owner
     * @param lambda Callback: (Fundamental<T> const&, T const& newValue)
     */
    template <class Context, std::invocable<Fundamental<T> const&> Lambda>
    void addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda) const;

    template <class Context, std::invocable<Fundamental<T> const&> Lambda>
    void addListener(std::weak_ptr<Context>&& context, Lambda && lambda) const;

   #if JUCE_SUPPORT
    template <class ComponentType, std::invocable<Fundamental<T> const&> Lambda>
    void addListener(ComponentType* context, Lambda && lambda) const requires std::is_base_of_v<juce::Component, ComponentType>;
   #endif

    // overridden base methods
    bool assign(Value const&) override;

   #if JUCE_SUPPORT
    juce::Value getUnderlyingValue() requires kIsOpaque;
   #endif

protected:
    using ValueListenerFunction = std::function<void(Fundamental<T> const&)>;

    void callListeners();

    typename Value::TypesVariant visit_helper() override;
    typename Value::ConstTypesVariant visit_helper() const override;

    T underlying;
    mutable std::map<std::weak_ptr<ListenerToken::Impl>, ValueListenerFunction, std::owner_less<std::weak_ptr<ListenerToken::Impl>>> valueListeners = {};
    mutable std::vector<Value::ListenerBinding> managedValueListeners = {};
private:
   #if JUCE_SUPPORT
    struct DynamicValueSource : juce::Value::ValueSource
    {
        DynamicValueSource(Fundamental<T>&);
        juce::var getValue () const override;
        void setValue(juce::var const&) override;

        Fundamental<T>* parent = nullptr;
    };

    // Just have an empty member here if this type does not support juce's Value type
    std::conditional_t<kIsOpaque, juce::ReferenceCountedObjectPtr<DynamicValueSource>, std::type_identity<void>> juceValue = {};
   #endif
};

/**
 * @brief User-defined literal for creating compile-time field name tags
 *
 * Use this literal to create CompileTimeString objects for type-safe field access.
 * The syntax "fieldname"_fld creates a tag that can be passed to Record::operator()
 * for compile-time verified field access.
 *
 * @code
 * // Access the "x" field of a Point struct
 * point("x"_fld) = 3.14f;
 *
 * // Chain access for nested structs
 * line("start"_fld)("x"_fld) = 1.0f;
 * @endcode
 *
 * @return CompileTimeString containing the field name
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#ifdef __clang__
#    pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
#endif
template <typename T, T... chars>
constexpr CompileTimeString<fixstr::fixed_string<sizeof...(chars)>({chars...})> operator""_fld();
#pragma GCC diagnostic pop

/**
 * @brief Concrete implementation of Fundamental<T> for struct types with extended field access
 *
 * Record extends Fundamental<T> to provide:
 *   - Compile-time field access via operator()(CompileTimeString) using "_fld" literal
 *   - Field iteration via visitFields() and visitField()
 *   - Static kFieldNames array containing all field names
 *   - Access to typed fields() tuple
 *
 * This is the primary way to work with reflection-enabled structs at the top level.
 *
 * @tparam T A struct type where all members are wrapped in Field<Type, "name">
 *
 * @code
 * struct Point {
 *     Field<float, "x"> x;
 *     Field<float, "y"> y;
 * };
 *
 * Record<Point> point;
 * point("x"_fld) = 3.14f;              // Compile-time field access
 * point.visitFields([](auto name, auto& fld) { ... });  // Iterate all fields
 * @endcode
 */
template <typename T>
class Record : public Fundamental<T>
{
private:
    static auto fields_with(T& u);
    static auto fields_with(T const& u);
public:
    /// Default constructor - creates a Record with default-initialized underlying value
    Record();

    /// Construct from underlying struct value
    Record(T const& underlying_);

    /// Copy constructor
    Record(Record const& o);

    /// Move constructor
    Record(Record&& o);

    /// Copy assignment operator
    Record& operator=(Record const& o);

    /// Move assignment operator
    Record& operator=(Record&& o);

    /// Returns the underlying struct value (const access)
    T const& operator()() const { return Fundamental<T>::operator()(); }

    //=============================================================================
    // Type aliases for field access
    //=============================================================================

    /// Tuple type of references to all fields
    using ReferenceTuple = decltype(fields_with(std::declval<T>()));

    /// Tuple type of all field types (decayed)
    using FieldsAsTuple = detail::decay_tuple<ReferenceTuple>::type;
    static_assert(std::tuple_size_v<FieldsAsTuple> >= 1);

    /// Compile-time array of all field names in declaration order
    static constexpr std::array<std::string_view const, std::tuple_size_v<FieldsAsTuple>> kFieldNames =
        std::invoke([] <typename... Types> (std::type_identity<std::tuple<Types...>>)
        {
            std::array<std::string_view const, std::tuple_size_v<FieldsAsTuple>> returnValue = {{
                std::invoke([] <typename U, fixstr::fixed_string Name> (std::type_identity<Field<U, Name>>)
                    -> std::string_view
                {
                    return Name;
                }, std::type_identity<Types>())...
            }};

            return returnValue;
        }, std::type_identity<FieldsAsTuple>());


    /**
     * @brief Access a field by compile-time name using "_fld" literal
     *
     * @tparam FieldName Compile-time string from "_fld" literal
     * @return Reference to the field (typed as the actual Field<> type)
     */
    template <fixstr::fixed_string FieldName>
    auto&& operator()(this auto& self, CompileTimeString<FieldName>);

    /**
     * @brief Returns a tuple of references to all typed fields
     *
     * Unlike typeErasedFields(), this returns a tuple with the actual Field<> types,
     * allowing compile-time access to field types and names.
     *
     * @return Tuple of references to Field<> members
     */
    auto fields(this auto& self);

    /// Returns type-erased references to all fields (const version)
    std::vector<std::reference_wrapper<Value const>> typeErasedFields() const override;

    /// Returns type-erased references to all fields (mutable version)
    std::vector<std::reference_wrapper<Value>> typeErasedFields() override;

    /**
     * @brief Visit all fields with a lambda
     *
     * @param lambda Callable taking (std::string_view name, Field& field)
     */
    template <typename Lambda>
    void visitFields(this auto& self, Lambda && lambda) noexcept;

    /**
     * @brief Visit a single field by runtime name
     *
     * Returns whatever the lambda returns wrapped in an optional. If no field
     * is found that has the name str, then this method returns an empty optional.
     * If the lambda does not return anything, then this method returns a bool,
     * with true indicating success.
     *
     * @param str The field name to find
     * @param lambda Callable to invoke on the found field
     * @return optional<R> or bool indicating success
     */
    template <typename Lambda>
    auto visitField(this auto& self, std::string_view const& str, Lambda && lambda);

    // overridden base methods
    bool assignChild(std::string const&, Value const&) override;
    bool removeChild(std::string const&) override;

private:
    auto typeErasedFields_internal(this auto& self);

    void init();
};


template <typename T> bool operator==(Array<T> const&, Array<T> const&);
template <typename T> bool operator==(Map<T>   const&, Map<T>   const&);

} //namespace dynamic

namespace dynamic
{
/**
 * @brief Dynamic array container with reflection and change notification support
 *
 * Array<T> provides a vector-like container where elements are automatically
 * wrapped in the reflection system. It extends Object to provide:
 *   - Element access via typeErasedFields()
 *   - Change listeners that fire on add/remove operations
 *   - Hierarchical change propagation to parent structs
 *   - Each element is indexed numerically and accessible by index as a field name
 *
 * When elements are added or removed, listeners are notified with the operation type,
 * the affected element, and its index. Changes also propagate up through parent
 * structures when the Array is part of a larger struct hierarchy.
 *
 * @tparam T The element type (can be primitive, struct with Field<> members, or another container)
 *
 * @code
 * Array<Point> points;
 * points.addElement(Point{1.0f, 2.0f});
 * points.addListener(context, [](Operation op, auto& arr, auto& elem, size_t idx) {
 *     std::cout << "Element " << idx << " was " << (op == Operation::add ? "added" : "removed");
 * });
 * @endcode
 */
template <typename T>
class Array : public Object
{
public:
    using Object::operator();

    Array() = default;
    Array(Array const& o);

    /// Returns the type_info for Array<T>
    std::type_info const& type() const override { return typeid(Array<T>); }

    /// Returns the MetaType for Array<T> (static, no instance needed)
    static MetaType const& meta();

    /// Returns the MetaType for this array type
    MetaType const& metaType() const override;

    /// Always returns true - arrays are always valid containers
    bool isValid() const override { return true; }

    /// Always returns true - arrays are always valid containers
    constexpr operator bool() const { return true; }

    bool isMapOrArray() const override { return true; }
    std::type_info const& elementType() const override { return typeid(T); }

    /// Returns a vector of type-erased references to all elements (const version)
    std::vector<std::reference_wrapper<Value const>> typeErasedFields() const override;

    /// Returns a vector of type-erased references to all elements (mutable version)
    std::vector<std::reference_wrapper<Value>> typeErasedFields() override;

    /// Returns the number of elements in the array
    std::size_t size() const { return elements.size(); }

    /**
     * @brief Add an element to the end of the array
     *
     * Notifies all listeners with Operation::add before adding the element.
     * The element is wrapped in the reflection system and assigned this array as its parent.
     *
     * @param element The element to add (copied)
     */
    void addElement(T const& element);

    /**
     * @brief Add an element to the end of the array (move version)
     *
     * @param element The element to add (moved)
     */
    void addElement(T&& element);

    /**
     * @brief Remove an element at the specified index
     *
     * Notifies all listeners with Operation::remove before removing the element.
     * Asserts if the index is out of bounds.
     *
     * @param idx The index of the element to remove
     */
    void removeElement(std::size_t idx);

    /**
     * @brief Register a listener for array changes (token-based)
     *
     * The listener will be called whenever this value changes via the add or
     * remove methods. The listener remains active until the returned token is destroyed.
     *
     * @param lambda Callback: (Operation, Array<T> const&, T const& element, std::size_t index)
     * @return ListenerToken that removes the listener when destroyed
     */
    template <std::invocable<Operation, Array<T> const&, T const&, std::size_t> Lambda>
    ListenerToken addListener(Lambda && lambda);

    /**
     * @brief Register a listener for array changes (auto-cleanup)
     *
     * The listener will be called whenever this value changes via the add or
     * remove methods. The listener is automatically removed when the context expires.
     *
     * @tparam Context Type deriving from std::enable_shared_from_this
     * @param context Pointer to the listener owner
     * @param lambda Callback: (Operation, Array<T> const&, T const& element, std::size_t index)
     */
    template <class Context, std::invocable<Operation, Array<T> const&, T const&, std::size_t> Lambda>
    void addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda);

    template <class Context, std::invocable<Operation, Array<T> const&, T const&, std::size_t> Lambda>
    void addListener(std::weak_ptr<Context>&& context, Lambda && lambda);

   #if JUCE_SUPPORT
    template <class ComponentType, std::invocable<Operation, Array<T> const&, T const&, std::size_t> Lambda>
    void addListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>;
   #endif

    // overridden base methods
    bool assign(Value const&) override;
    bool assignChild(std::string const&, Value const&) override;
    bool removeChild(std::string const&) override;

    Array& operator=(Array const& o)
    {
        auto status = assign(o);
        assert(status);
        (void)status;
        return *this;
    }

    friend bool operator==<>(Array<T> const&, Array<T> const&);
private:
    auto typeErasedFields_internal(this auto && self);

    /**
     * @brief Internal wrapper for array elements
     *
     * Element wraps each value stored in the array to integrate it with the
     * reflection system. It derives from either Fundamental<T> or Record<T>
     * depending on whether T is a primitive or struct type.
     *
     * Each element reports its index as its field name, allowing the element
     * to participate in path-based addressing and listener notifications.
     */
    struct Element : public detail::BaseTypeFor<T>
    {
    public:
        using Base = detail::BaseTypeFor<T>;

        /// Construct an element with default value
        Element(Array& container_);

        /// Copy constructor
        Element(Element const& o);

        /// Construct element from underlying value (copy)
        Element(Array& container_, T const& underlying_);

        /// Construct element from underlying value (move)
        Element(Array& container_, T && underlying_);

        /// Assignment operator
        Element& operator=(Element const&);

        /// Assign new value to element
        Element& operator=(T const& t);

        /// Assign new value to element (move version)
        Element& operator=(T && t);

        /// Returns the element's index as a string (its "field name")
        std::string fieldname() const override;

    private:
        /// Initialize the element's parent pointer
        void init(Array& container_);
    };

    using ArrayListenerFunction = std::function<void(Operation, Array<T> const&, T const&, std::size_t)>;

    void callListeners(Operation op, T const& newValue, std::size_t idx) const;

    std::vector<Element> elements;
    mutable std::map<std::weak_ptr<ListenerToken::Impl>, ArrayListenerFunction, std::owner_less<std::weak_ptr<ListenerToken::Impl>>> arrayListeners;
    mutable std::vector<Value::ListenerBinding> managedArrayListeners;

public:
    /// The typed element type (Fundamental<T> for primitive T, Record<T> for struct T)
    using ElementType = typename Element::Base;

    /// Typed element access by index
    ElementType& operator[](std::size_t idx)
    {
        assert(idx < elements.size());
        return elements[idx];
    }

    /// Typed element access by index (const)
    ElementType const& operator[](std::size_t idx) const
    {
        assert(idx < elements.size());
        return elements[idx];
    }

    /// Returns true if the array has no elements
    bool empty() const { return elements.empty(); }

    /// Iterator for typed element access
    template <bool IsConst>
    class IteratorImpl
    {
        friend class Array;
        using VecIter = std::conditional_t<IsConst,
            typename std::vector<Element>::const_iterator,
            typename std::vector<Element>::iterator>;
        VecIter it_;
        explicit IteratorImpl(VecIter it) : it_(it) {}
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = std::conditional_t<IsConst, ElementType const, ElementType>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        reference operator*() const { return *it_; }
        pointer operator->() const { return static_cast<pointer>(&(*it_)); }

        IteratorImpl& operator++() { ++it_; return *this; }
        IteratorImpl operator++(int) { auto tmp = *this; ++it_; return tmp; }
        IteratorImpl& operator--() { --it_; return *this; }
        IteratorImpl operator--(int) { auto tmp = *this; --it_; return tmp; }

        IteratorImpl operator+(difference_type n) const { return IteratorImpl(it_ + n); }
        IteratorImpl operator-(difference_type n) const { return IteratorImpl(it_ - n); }
        difference_type operator-(IteratorImpl const& o) const { return it_ - o.it_; }
        IteratorImpl& operator+=(difference_type n) { it_ += n; return *this; }
        IteratorImpl& operator-=(difference_type n) { it_ -= n; return *this; }
        reference operator[](difference_type n) const { return static_cast<reference>(it_[n]); }

        bool operator==(IteratorImpl const& o) const { return it_ == o.it_; }
        bool operator!=(IteratorImpl const& o) const { return it_ != o.it_; }
        bool operator<(IteratorImpl const& o) const { return it_ < o.it_; }
        bool operator>(IteratorImpl const& o) const { return it_ > o.it_; }
        bool operator<=(IteratorImpl const& o) const { return it_ <= o.it_; }
        bool operator>=(IteratorImpl const& o) const { return it_ >= o.it_; }
    };

    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

    iterator begin() { return iterator(elements.begin()); }
    iterator end() { return iterator(elements.end()); }
    const_iterator begin() const { return const_iterator(elements.cbegin()); }
    const_iterator end() const { return const_iterator(elements.cend()); }
    const_iterator cbegin() const { return const_iterator(elements.cbegin()); }
    const_iterator cend() const { return const_iterator(elements.cend()); }
};

/**
 * @brief Dynamic map container with string keys and reflection support
 *
 * Map<T> provides a dictionary-like container where values are indexed by string
 * keys and automatically wrapped in the reflection system. It extends Object to provide:
 *   - Key-value access via typeErasedFields() (values accessible by key name)
 *   - Change listeners that fire on add/remove operations
 *   - Hierarchical change propagation to parent structs
 *   - Each value is accessible as a field with its key as the field name
 *
 * When entries are added or removed, listeners are notified with the operation type,
 * the affected value, and its key. Adding a value with an existing key updates that
 * entry rather than creating a duplicate. Changes propagate up through parent structures.
 *
 * @tparam T The value type (can be primitive, struct with Field<> members, or another container)
 *
 * @code
 * Map<Point> points;
 * points.addElement("origin", Point{0.0f, 0.0f});
 * points.addListener(context, [](Operation op, auto& map, auto& val, auto key) {
 *     std::cout << "Key '" << key << "' was " << (op == Operation::add ? "added" : "removed");
 * });
 * @endcode
 */
template <typename T>
class Map : public Object
{
public:
    Map() = default;
    Map(Map const& o);

    /// Returns the type_info for Map<T>
    std::type_info const& type() const override { return typeid(Map<T>); }

    /// Returns the MetaType for Map<T> (static, no instance needed)
    static MetaType const& meta();

    /// Returns the MetaType for this map type
    MetaType const& metaType() const override;

    /// Always returns true - maps are always valid containers
    bool isValid() const override { return true; }

    /// Always returns true - maps are always valid containers
    constexpr operator bool() const { return true; }

    bool isMapOrArray() const override { return true; }
    std::type_info const& elementType() const override { return typeid(T); }

    /// Returns a vector of type-erased references to all values (const version)
    std::vector<std::reference_wrapper<Value const>> typeErasedFields() const override;

    /// Returns a vector of type-erased references to all values (mutable version)
    std::vector<std::reference_wrapper<Value>> typeErasedFields() override;

    /// Returns the number of key-value pairs in the map
    std::size_t size() const { return elements.size(); }

    /**
     * @brief Add or update a key-value pair in the map
     *
     * If the key already exists, the existing value is updated without notification.
     * If the key is new, listeners are notified with Operation::add before adding.
     * The value is wrapped in the reflection system and assigned this map as its parent.
     *
     * @param key The string key for this entry
     * @param element The value to add or update (copied)
     */
    void addElement(std::string_view key, T const& element);

    /**
     * @brief Add or update a key-value pair in the map (move version)
     *
     * @param key The string key for this entry
     * @param element The value to add or update (moved)
     */
    void addElement(std::string_view key, T&& element);

    /**
     * @brief Remove a key-value pair from the map
     *
     * Notifies all listeners with Operation::remove before removing the entry.
     *
     * @param key The string key of the entry to remove
     * @return true if the key was found and removed, false if the key didn't exist
     */
    bool removeElement(std::string_view key);

    /**
     * @brief Register a listener for map changes (token-based)
     *
     * The listener will be called whenever this value changes via the add or
     * remove methods. The listener remains active until the returned token is destroyed.
     *
     * @param lambda Callback: (Operation, Map<T> const&, T const& value, std::string_view key)
     * @return ListenerToken that removes the listener when destroyed
     */
    template <std::invocable<Operation, Map<T> const&, T const&, std::string_view> Lambda>
    ListenerToken addListener(Lambda && lambda);

    /**
     * @brief Register a listener for map changes (auto-cleanup)
     *
     * The listener will be called whenever this value changes via the add or
     * remove methods. The listener is automatically removed when the context expires.
     *
     * @tparam Context Type deriving from std::enable_shared_from_this
     * @param context Pointer to the listener owner
     * @param lambda Callback: (Operation, Map<T> const&, T const& value, std::string_view key)
     */
    template <class Context, std::invocable<Operation, Map<T> const&, T const&, std::string_view> Lambda>
    void addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda);

    template <class Context, std::invocable<Operation, Map<T> const&, T const&, std::string_view> Lambda>
    void addListener(std::weak_ptr<Context>&& context, Lambda && lambda);

   #if JUCE_SUPPORT
    template <class ComponentType, std::invocable<Operation, Map<T> const&, T const&, std::string_view> Lambda>
    void addListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>;
   #endif

    // assignment operator
    Map& operator=(Map const& o)
    {
        auto status = assign(o);
        assert(status);
        (void)status;
        return *this;
    }

    // overridden base methods
    bool assign(Value const&) override;
    bool assignChild(std::string const&, Value const&) override;
    bool removeChild(std::string const&) override;

    friend bool operator==<>(Map<T> const&, Map<T> const&);
private:
    auto typeErasedFields_internal(this auto && self);

    /**
     * @brief Internal wrapper for map values
     *
     * Element wraps each value stored in the map to integrate it with the
     * reflection system. It derives from either Fundamental<T> or Record<T>
     * depending on whether T is a primitive or struct type.
     *
     * Each element stores and reports its key as its field name, allowing the
     * value to participate in path-based addressing and listener notifications.
     */
    struct Element : public detail::BaseTypeFor<T>
    {
    public:
        using Base = detail::BaseTypeFor<T>;

        /// Construct an element with a key and default value
        Element(std::string_view fieldName_, Map& container_);

        /// Copy constructor
        Element(Element const& o);

        /// Construct element with key and underlying value (copy)
        Element(std::string_view fieldName_, Map& container_, T const& underlying_);

        /// Construct element with key and underlying value (move)
        Element(std::string_view fieldName_, Map& container_, T && underlying_);

        // Assignment operator
        Element& operator=(Element const&);

        /// Assign new value to element
        Element& operator=(T const& t);

        /// Assign new value to element (move version)
        Element& operator=(T && t);

        /// Returns the element's key as its field name
        std::string fieldname() const override;

    private:
        friend bool operator==<>(Map<T> const&, Map<T> const&);
        friend class Map<T>;
        std::string fieldName;

        /// Initialize the element's parent pointer
        void init(Map& container_);
    };

    using MapListenerFunction = std::function<void(Operation, Map<T> const&, T const&, std::string_view)>;

    void callListeners(Operation op, T const& newValue, std::string_view key) const;

    std::vector<Element> elements;
    mutable std::map<std::weak_ptr<ListenerToken::Impl>, MapListenerFunction, std::owner_less<std::weak_ptr<ListenerToken::Impl>>> mapListeners;
    mutable std::vector<Value::ListenerBinding> managedMapListeners;

public:
    /// The typed element type (Fundamental<T> for primitive T, Record<T> for struct T)
    using ElementType = typename Element::Base;

    /// Typed element access by key (asserts if key not found)
    ElementType& operator[](std::string_view key)
    {
        auto it = std::find_if(elements.begin(), elements.end(),
            [key](Element const& elem) { return elem.fieldName == key; });
        assert(it != elements.end());
        return *it;
    }

    /// Typed element access by key (const, asserts if key not found)
    ElementType const& operator[](std::string_view key) const
    {
        auto it = std::find_if(elements.begin(), elements.end(),
            [key](Element const& elem) { return elem.fieldName == key; });
        assert(it != elements.end());
        return *it;
    }

    // Required to fix ambiguity with built-in operator[]
    template<typename U>
    requires std::is_convertible_v<U, std::string_view>
    ElementType& operator[](U&& key) { return (*this)[std::string_view(std::forward<U>(key))]; }

    template<typename U>
    requires std::is_convertible_v<U, std::string_view>
    ElementType const& operator[](U&& key) const { return (*this)[std::string_view(std::forward<U>(key))]; }

    /// Returns true if the map has no elements
    bool empty() const { return elements.empty(); }

    /// Returns true if the map contains an element with the given key
    bool contains(std::string_view key) const
    {
        return std::find_if(elements.begin(), elements.end(),
            [key](Element const& elem) { return elem.fieldName == key; }) != elements.end();
    }

    /// Iterator for typed element access
    template <bool IsConst>
    class IteratorImpl
    {
        friend class Map;
        using VecIter = std::conditional_t<IsConst,
            typename std::vector<Element>::const_iterator,
            typename std::vector<Element>::iterator>;
        VecIter it_;
        explicit IteratorImpl(VecIter it) : it_(it) {}
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::conditional_t<IsConst, ElementType const, ElementType>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        reference operator*() const { return *it_; }
        pointer operator->() const { return static_cast<pointer>(&(*it_)); }

        IteratorImpl& operator++() { ++it_; return *this; }
        IteratorImpl operator++(int) { auto tmp = *this; ++it_; return tmp; }

        bool operator==(IteratorImpl const& o) const { return it_ == o.it_; }
        bool operator!=(IteratorImpl const& o) const { return it_ != o.it_; }
    };

    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

    iterator begin() { return iterator(elements.begin()); }
    iterator end() { return iterator(elements.end()); }
    const_iterator begin() const { return const_iterator(elements.cbegin()); }
    const_iterator end() const { return const_iterator(elements.cend()); }
    const_iterator cbegin() const { return const_iterator(elements.cbegin()); }
    const_iterator cend() const { return const_iterator(elements.cend()); }

    /// Find an element by key
    iterator find(std::string_view key)
    {
        return iterator(std::find_if(elements.begin(), elements.end(),
            [key](Element const& elem) { return elem.fieldName == key; }));
    }

    /// Find an element by key (const)
    const_iterator find(std::string_view key) const
    {
        return const_iterator(std::find_if(elements.begin(), elements.end(),
            [key](Element const& elem) { return elem.fieldName == key; }));
    }
};

/**
 * @brief Named field wrapper for use as struct members
 *
 * Field is the key building block for reflection-enabled structs. Each member
 * of a struct should be wrapped in Field<Type, "name"> to enable:
 *   - Compile-time field name access via fieldname()
 *   - Integration with Record's field iteration
 *   - Listener support and change propagation to parent structs
 *
 * For primitive types, Field derives from Fundamental<T>.
 * For struct types (containing their own Field<> members), Field derives from Record<T>.
 *
 * @tparam T The underlying value type
 * @tparam Name Compile-time string literal for the field name
 *
 * @code
 * struct Point {
 *     Field<float, "x"> x;   // Field "x" of type float
 *     Field<float, "y"> y;   // Field "y" of type float
 * };
 *
 * struct Line {
 *     Field<Point, "start">  start;   // Nested struct field
 *     Field<Point, "finish"> finish;
 * };
 * @endcode
 */
template <typename T, fixstr::fixed_string Name>
class Field : public detail::BaseTypeFor<T>
{
public:
    using Base = detail::BaseTypeFor<T>;

    /// Default constructor - creates a Field with default-initialized value
    Field() = default;

    /// Assign new value to the field
    Field& operator=(T const& t);

    /// Assign new value to the field (move version)
    Field& operator=(T && t);

    /// Returns the compile-time field name as specified in the template parameter
    std::string fieldname() const override;
};

//=============================================================================
// MetaType system - compile-time type reflection without instances
//=============================================================================

/**
 * @brief Abstract base class for compile-time type metadata
 *
 * MetaType provides a type-erased interface for inspecting Dynamic types
 * without requiring an instance. Each Dynamic wrapper type (Fundamental, Record,
 * Array, Map) has a corresponding MetaType that describes:
 *   - The underlying C++ type (via typeInfo())
 *   - Whether it's opaque, a record, array, or map
 *   - For records: field names and their MetaTypes (via fields())
 *   - For containers: the element MetaType (via elementMetaType())
 *   - A factory method to construct instances (via construct())
 *
 * Access MetaType without an instance using the static meta() method:
 * @code
 * auto const& meta = Record<Point>::meta();
 * for (auto const& field : meta.fields())
 *     std::cout << field.name << std::endl;
 *
 * auto instance = meta.construct();  // Creates a Record<Point>
 * @endcode
 *
 * Or from an existing Value reference:
 * @code
 * void inspect(Value const& v) {
 *     for (auto const& field : v.metaType().fields())
 *         std::cout << field.name << std::endl;
 * }
 * @endcode
 */
class MetaType
{
public:
    virtual ~MetaType() = default;

    /// Returns the std::type_info for the underlying type
    virtual std::type_info const& typeInfo() const = 0;

    /// Returns true if this is an opaque/fundamental type (not a struct with Fields)
    virtual bool isOpaque() const = 0;

    /// Returns true if this is a Record type (struct with Field<> members)
    virtual bool isRecord() const { return false; }

    /// Returns true if this is an Array<T> type
    virtual bool isArray() const { return false; }

    /// Returns true if this is a Map<T> type
    virtual bool isMap() const { return false; }

    /**
     * @brief Returns field descriptors for Record types
     *
     * For Record types, returns a span of FieldDescriptor entries describing
     * each field's name and MetaType. For non-Record types, returns an empty span.
     *
     * @return Span of field descriptors (empty for non-Record types)
     */
    virtual std::span<FieldDescriptor const> fields() const { return {}; }

    /**
     * @brief Returns the MetaType of container elements
     *
     * For Array<T> and Map<T>, returns a pointer to the MetaType describing T.
     * For non-container types, returns nullptr.
     *
     * @return Pointer to element MetaType, or nullptr
     */
    virtual MetaType const* elementMetaType() const { return nullptr; }

    /**
     * @brief Construct a new instance of the Dynamic wrapper type
     *
     * Creates a heap-allocated instance of the appropriate wrapper:
     *   - FundamentalMeta<T> creates Fundamental<T>
     *   - RecordMeta<T> creates Record<T>
     *   - ArrayMeta<T> creates Array<T>
     *   - MapMeta<T> creates Map<T>
     *
     * @return unique_ptr to the newly constructed Value, or nullptr for Invalid
     */
    virtual std::unique_ptr<Value> construct() const = 0;
};

/**
 * @brief Get the MetaType for a given C++ type T
 *
 * Maps any supported type to its MetaType singleton:
 *   - Opaque types (int, float, string, etc.) → FundamentalMeta
 *   - Structs with Field<> members → RecordMeta
 *   - Array<T> → ArrayMeta
 *   - Map<T> → MapMeta
 *
 * @tparam T The type to get metadata for
 * @return Reference to the MetaType singleton for T
 */
template <typename T>
MetaType const& metaTypeOf();

// Equality and stream output operators
bool operator==(ID const& lhs, ID const& rhs);
std::ostream& operator<<(std::ostream& o, ID const& id);
std::ostream& operator<<(std::ostream& o, dynamic::Value const& x);
std::ostream& operator<<(std::ostream& o, dynamic::Object const& x);
std::ostream& operator<<(std::ostream& o, dynamic::Invalid const& x);
} // namespace dynamic

// std::formatter specializations
template <>
struct std::formatter<dynamic::Object> : std::formatter<std::string>
{
    auto format(dynamic::Object const& v, format_context& ctx) const;
};

template <typename T>
struct std::formatter<dynamic::Record<T>> : std::formatter<dynamic::Object> {};

template<typename CharT>
struct std::formatter<dynamic::Invalid, CharT>
{
    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext& ctx) { return ctx.begin(); }

    template<class FmtContext>
    FmtContext::iterator format(dynamic::Invalid const&, FmtContext& ctx) const { return ctx.out(); }
};

template<typename CharT>
struct std::formatter<dynamic::Value, CharT>
{
    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext& ctx) { return ctx.begin(); }

    template<class FmtContext>
    FmtContext::iterator format(dynamic::Value const& v, FmtContext& ctx) const;
};

template <typename T, typename CharT>
struct std::formatter<dynamic::Fundamental<T>, CharT> : std::formatter<dynamic::Value, CharT> {};

template <typename T, fixstr::fixed_string Name>
struct std::formatter<dynamic::Field<T, Name>> : std::formatter<dynamic::Value> {};

template <>
struct std::formatter<dynamic::ID> : std::formatter<std::string>
{
    auto format(dynamic::ID const& id, format_context& ctx) const
    {
        return std::formatter<std::string>::format(id.toString(), ctx);
    }
};

// Include template implementations
#include "dynamic.tpp"
