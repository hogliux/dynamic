# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Dynamic** is a C++20 meta-template library that provides runtime reflection and change notification for C++ structs through compile-time field wrapping. It's currently in active development and not ready for production use.

Key capabilities:
- Runtime iteration of struct field names and values
- Type-erased value access with visitor patterns
- Change listeners with automatic propagation through nested hierarchies
- Path-based addressing for nested fields (e.g., "line/start/x")
- Generic containers (Array<T>, Map<T>) with reflection support
- Static MetaType introspection without requiring instances

## Build System

The project uses CMake with Ninja as the build generator.

### Building

```bash
# Configure (defaults to Debug if .git exists, otherwise Release)
cmake -B build -G Ninja

# Build all targets
cmake --build build

# Build specific target
cmake --build build --target dynamic
cmake --build build --target example
```

### Build Targets

- `dynamic` - Static library containing the core reflection system
- `example` - Example executable demonstrating library usage (built from main.cpp)

### Compiler Requirements

- C++23 standard (set via `CMAKE_CXX_STANDARD`)
- Requires a compiler supporting C++20 features at minimum (C++23 used for some extended features)
- Tested with Clang and GCC (extensive warning flags enabled for both)

## Core Architecture

### Type Hierarchy

```
Value (abstract base)
├── Invalid (sentinel for missing/invalid fields)
├── Fundamental<T> (values with change notification)
│   └── Record<T> (structs with field access methods)
│       └── Field<T, Name> (named struct members)
└── Object (abstract base for field containers)
    ├── Record<T> (when T contains Field<> members)
    ├── Array<T> (dynamic array with reflection)
    └── Map<T> (dynamic map with reflection)

MetaType (abstract base - static type introspection)
├── InvalidMeta (void/invalid type)
├── FundamentalMeta<T> (opaque types: int, float, string, etc.)
├── RecordMeta<T> (struct types with Field<> members)
├── ArrayMeta<T> (Array<T> containers)
└── MapMeta<T> (Map<T> containers)
```

### Key Design Patterns

1. **Field Wrapping**: Struct members are wrapped in `Field<Type, "name">` templates to enable reflection
   - Field names are compile-time string literals using `fixstr::fixed_string`
   - Fields automatically integrate into the reflection system

2. **Dual Inheritance Model**: `Fundamental<T>` conditionally derives from either:
   - `Value` for primitive/opaque types
   - `Object` for struct types containing `Field<>` members
   - Determined by `Value::isOpaque<T>()` compile-time check

3. **Type Erasure**: All values derive from `Value` base class
   - Runtime type identification via `type()` method
   - Type-safe visitation via `visit()` with lambda overloads
   - Supported fundamental types: int8/16/32/64_t, float, double, bool, std::string, ID

4. **Listener System**: Token-based RAII listener management with two types:
   - **Value listeners**: Attached to individual fields, fire on direct changes
   - **Child listeners**: Attached to Objects, fire on changes to any nested field
   - Token-based API returns `ListenerToken` for explicit lifetime control
   - Weak_ptr-based API provides automatic cleanup when context expires
   - JUCE Component contexts supported via `Component::SafePointer`

5. **Parent Chain**: Each `Value` maintains a `parent` pointer to its containing `Object`
   - Enables change propagation up the hierarchy
   - Allows path reconstruction for nested field changes

6. **MetaType System**: Static type introspection without requiring instances
   - Each Dynamic wrapper type has a corresponding `MetaType` subclass (singleton)
   - Accessible via static `meta()` method (e.g., `Record<Point>::meta()`) or `metaTypeOf<T>()`
   - Also accessible from instances via `Value::metaType()` virtual method
   - `FieldDescriptor` describes record fields with lazy `metaType()` function pointers to avoid static init order issues
   - Factory pattern: `MetaType::construct()` creates instances without knowing the concrete type
   - `MetaTypeHelper<T>` dispatches to the correct MetaType subclass at compile time
   - Partial specializations handle `Array<T>` and `Map<T>` container types

### Compile-Time Field Access

The `"fieldname"_fld` user-defined literal creates `CompileTimeString<>` tags for type-safe field access:

```cpp
Record<Point> point;
point("x"_fld) = 3.14f;  // Compile-time verified field access
```

This is implemented via a GNU string literal operator template (requires `-Wno-gnu-string-literal-operator-template` or `-Wno-pedantic` on Clang).

## File Structure

### Core Files
- `dynamic.hpp` - Main header with class declarations (~1450 lines)
- `dynamic.tpp` - Template implementations (included at end of dynamic.hpp)
- `dynamic.cpp` - Non-template implementations
- `dynamic_detail.hpp` - Internal helper utilities and type traits (including `field_value_type_t<F>` for extracting value types from `Field<T, Name>`)

### Dependencies
- `fixed_string.hpp` - Compile-time string handling for field names
- `CxxUtilities.hpp` - General utility functions
- `pfr/` - Boost.PFR library submodule for struct introspection

### Examples and Tests
- `main.cpp` - Example usage demonstrating all major features
- `build/*.cpp` - Various test files (test_id.cpp, test_bool_behavior.cpp, etc.)

## Working with the Code

### Adding New Fundamental Types

To support a new primitive type, add it to `Value::SupportedFundamentalTypes` tuple in dynamic.hpp:

```cpp
using SupportedFundamentalTypes = std::tuple<
    int8_t, int16_t, int32_t, int64_t,
    float, double,
    bool,
    std::string, ID,
    YourNewType  // Add here
>;
```

### Listener Implementation Notes

**Architecture:**
- Listeners are stored directly as `std::unique_ptr<std::function<void(Args...)>>`
- No base class abstraction - clean and simple storage
- Token-based removal via `ListenerToken` RAII wrapper

**Two-tier API:**
1. **Token-based** (foundation):
   - `addListener(lambda)` returns `ListenerToken`
   - Listener removed when token destroyed
   - Explicit lifetime control

2. **Weak_ptr-based** (built on top of tokens):
   - `addListener(context, lambda)` for automatic cleanup
   - Uses `ListenerBinding` struct: `{weak_ptr<void>, ListenerToken}`
   - Expired contexts cleaned up when adding new listeners
   - Lambda signature has no context parameter - capture `this` directly

**Implementation details:**
- `std::weak_ptr<void>` used for type-erased context storage
- The `recursiveListenerDisabler` thread_local counter prevents infinite recursion
- JUCE support via `Component::SafePointer` for component-based contexts

### MetaType System

The MetaType system provides compile-time type metadata accessible at runtime without requiring instances.

**Architecture:**
- `MetaType` is an abstract base class with virtual methods: `typeInfo()`, `isOpaque()`, `isRecord()`, `isArray()`, `isMap()`, `fields()`, `elementMetaType()`, `construct()`
- Concrete implementations in `detail` namespace: `InvalidMeta`, `FundamentalMeta<T>`, `RecordMeta<T>`, `ArrayMeta<T>`, `MapMeta<T>`
- `FieldDescriptor` struct holds `string_view name` and a `MetaType const& (*metaType)()` function pointer for lazy resolution
- `detail::MetaTypeHelper<T>` primary template dispatches based on `Value::isOpaque<T>()`; partial specializations handle `Array<T>` and `Map<T>`

**Access patterns:**
1. Static (no instance): `Record<Point>::meta()`, `Array<T>::meta()`, `Map<T>::meta()`
2. Free function: `metaTypeOf<T>()` for any supported type
3. Virtual (from instance): `value.metaType()` on any `Value&`

**Factory method:**
- `MetaType::construct()` returns `std::unique_ptr<Value>` — creates the appropriate wrapper type
- `InvalidMeta::construct()` returns `nullptr`

### Path-Based Addressing

The `ID` class represents field paths as `std::vector<std::string>`:
- Constructed from "/" delimited strings via `ID::fromString()`
- Used by `Object::getchild()` for nested field access
- Automatically built during child listener propagation

### JUCE Integration

When `JUCE_SUPPORT` is defined (auto-detected from JUCE macros):
- Listeners can use `juce::Component*` as context via `Component::SafePointer`
- `Fundamental<T>::getUnderlyingValue()` returns a `juce::Value` for binding
- `DynamicValueSource` bridges between Dynamic values and JUCE's Value system

## Common Patterns

### Creating Reflection-Enabled Structs

```cpp
struct Point {
    Field<float, "x"> x;
    Field<float, "y"> y;
};

struct Line {
    Field<Point, "start"> start;
    Field<Point, "finish"> finish;
};
```

### Accessing Fields at Runtime

```cpp
Record<Point> point;

// Compile-time access (zero overhead)
point("x"_fld) = 1.0f;

// Runtime access by name
point.visitField("x", [](auto& field) {
    std::cout << field() << std::endl;
});

// Type-erased iteration
for (auto& field : point.type_erased_fields()) {
    field.get().visit([](auto& value) {
        std::cout << value << std::endl;
    });
}
```

### Setting Up Listeners

**Token-based (explicit removal):**
```cpp
Record<Point> point;

// Listener active while token is in scope
auto token = point("x"_fld).addListener([](auto const& field, float newValue) {
    std::cout << "x changed to: " << newValue << std::endl;
});
// Listener removed when token destroyed
```

**Weak_ptr-based (automatic cleanup):**
```cpp
struct App : std::enable_shared_from_this<App> {
    void setup() {
        Record<State> state;

        // Listen to all nested changes - auto cleanup when App destroyed
        // Note: lambda captures 'this' directly, no context parameter
        state.addChildListener(this,
            [this](ID const& path, Object::Operation op,
                   Object const& parent, Value const& changed) {
                std::cout << "Changed: " << path.toString() << std::endl;
            }
        );
    }
};
```

**Key differences:**
- Token-based: Explicit control via RAII token
- Weak_ptr-based: Automatic cleanup when context expires
- Lambda signatures: No context parameter - capture `this` instead

### Using MetaType for Static Introspection

```cpp
// Inspect fields without creating an instance
auto const& meta = Record<Line>::meta();
for (auto const& field : meta.fields()) {
    std::cout << field.name;
    if (field.metaType().isRecord())
        std::cout << " (record, " << field.metaType().fields().size() << " fields)";
    std::cout << std::endl;
}

// Construct instances via factory
auto instance = meta.construct();  // Creates a Record<Line>

// Free function access
auto const& floatMeta = metaTypeOf<float>();
std::cout << floatMeta.isOpaque() << std::endl;  // true

// Container element introspection
auto const& arrayMeta = Array<Point>::meta();
auto const* elemMeta = arrayMeta.elementMetaType();
std::cout << elemMeta->isRecord() << std::endl;  // true
```

## Compiler-Specific Notes

### Warning Flags

The project enables extensive warnings:
- Clang: `-Wall -Wshadow-all -Wshorten-64-to-32 -Wunreachable-code` and many more
- GCC: Similar set with some differences (no `-Wshorten-64-to-32`)
- MSVC: `/W4`

Both Clang and GCC use `-Wno-ignored-qualifiers` to suppress specific warnings.

### Linker Flags

- Linux/Android: Sanitizers can conflict with `-Wl,--no-undefined`, so it's disabled on these platforms
- Other platforms: Use `-Wl,--no-undefined` (Clang/GCC) to catch missing symbols

## Design Philosophy

- **Non-intrusive**: Only wrap members you want reflected, no base class requirements
- **Type-safe**: Compile-time field names enable refactoring and prevent typos
- **Zero overhead**: When not using reflection features, wrapped types behave like regular structs
- **Composable**: Nested structs, arrays, and maps work seamlessly together
