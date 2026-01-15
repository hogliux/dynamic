# Dynamic - C++ Reflection and Change Notification Library

A modern C++20 meta-template library that brings runtime reflection and change notification to C++ structs through compile-time field wrapping.

This library is **still in active development**. Do not use in production code just yet.

## Overview

**Dynamic** enables structs, arrays, and maps to support reflection and change listeners by simply wrapping each struct member in a `Field<>` template. This allows you to:

- Iterate through struct field names and values at runtime
- Access type-erased values with safe visitor patterns
- Attach listeners that trigger on value changes
- Track changes through nested struct hierarchies with automatic path propagation
- Use both compile-time and runtime field access

## Key Features

- **Compile-Time Field Names** - Field names are preserved at compile time and accessible at runtime
- **Type-Erased Access** - All fields derive from a common `Value` base class with visitor support
- **Hierarchical Reflection** - Nested structs, arrays, and maps all participate in the reflection system
- **Change Listeners** - Attach callbacks that fire when values change, with automatic propagation through parent structures
- **Path-Based Addressing** - Access nested fields using slash-delimited paths (e.g., `"line/start/x"`)
- **Generic Containers** - Built-in `Array<T>` and `Map<T>` types with reflection and change notification
- **Zero Runtime Overhead** - When not using reflection features, the wrapped types behave like regular structs

## Quick Start

### Basic Struct with Reflection

```cpp
#include "dynamic.hpp"
using namespace dynamic;

// Define a struct with reflection by wrapping members in Field<>
struct Point {
    Field<float, "x"> x;
    Field<float, "y"> y;
};

// Wrap the struct in Record<> to use reflection features
Record<Point> point;

// Compile-time field access using "_fld" literal
point("x"_fld) = 3.14f;
point("y"_fld) = 2.71f;

// Access the underlying value
std::cout << point("x"_fld)() << std::endl;  // prints: 3.14
```

### Nested Structs

```cpp
struct Line {
    Field<Point, "start">  start;
    Field<Point, "finish"> finish;
};

Record<Line> line;

// Chain field access for nested structs
line("start"_fld)("x"_fld) = 1.0f;
line("start"_fld)("y"_fld) = 2.0f;
```

### Runtime Field Iteration

```cpp
Record<Point> point;

// Iterate through all fields
for (auto const& fieldName : Record<Point>::kFieldNames) {
    std::cout << "Field: " << fieldName << std::endl;
}

// Visit each field with a lambda
point.visitFields([](std::string_view name, auto& field) {
    std::cout << name << " = " << field() << std::endl;
});
```

### Type-Erased Access with Visitors

```cpp
Value& value = point("x"_fld);

// Visit with type-safe lambda - only called if the value is float
value.visit([](float& f) {
    f *= 2.0f;
    std::cout << "Doubled value: " << f << std::endl;
});

// Generic visitor for any supported type
value.visit([](auto& v) {
    std::cout << "Value: " << v << std::endl;
});
```

## Change Listeners

### Simple Value Listener

```cpp
struct Application : std::enable_shared_from_this<Application> {
    void setup() {
        Record<Point> point;

        // Listen for changes to a specific field
        point("x"_fld).addListener(this,
            [](std::shared_ptr<Application> ctx, auto const& field, float const& newValue) {
                std::cout << "X changed to: " << newValue << std::endl;
            }
        );

        point("x"_fld) = 42.0f;  // Triggers the listener
    }
};
```

### Hierarchical Child Listeners

Child listeners allow you to observe changes to any field within a struct hierarchy, including deeply nested fields.

```cpp
struct State {
    Field<Line, "line"> line;
    Field<Array<Point>, "points"> points;
};

Record<State> state;

// Listen to all changes within the state hierarchy
state.addChildListener(this,
    [](std::shared_ptr<Application> ctx,
       ID const& path,
       Object::Operation op,
       Object const& parent,
       Value const& changedValue) {

        std::cout << "Change at path: " << path.toString() << std::endl;

        switch (op) {
            case Object::Operation::modify:
                std::cout << "Value modified: " << changedValue << std::endl;
                break;
            case Object::Operation::add:
                std::cout << "Element added" << std::endl;
                break;
            case Object::Operation::remove:
                std::cout << "Element removed" << std::endl;
                break;
        }
    }
);

// These all trigger the child listener with appropriate paths
state("line"_fld)("start"_fld)("x"_fld) = 1.0f;  // path: "line/start/x"
state("points"_fld).addElement(Point{});          // path: "points/0"
```

## Dynamic Containers

### Arrays

```cpp
Array<Point> points;

// Add elements
points.addElement(Point{1.0f, 2.0f});
points.addElement(Point{3.0f, 4.0f});

// Listen for array changes
points.addListener(this,
    [](std::shared_ptr<Application> ctx,
       Object::Operation op,
       Array<Point> const& array,
       Point const& element,
       std::size_t index) {

        if (op == Object::Operation::add) {
            std::cout << "Added element at index " << index << std::endl;
        }
    }
);

// Access elements as fields by index
Object& firstPoint = points("0");
```

### Maps

```cpp
Map<Point> namedPoints;

// Add key-value pairs
namedPoints.addElement("origin", Point{0.0f, 0.0f});
namedPoints.addElement("center", Point{50.0f, 50.0f});

// Listen for map changes
namedPoints.addListener(this,
    [](std::shared_ptr<Application> ctx,
       Object::Operation op,
       Map<Point> const& map,
       Point const& value,
       std::string_view key) {

        std::cout << "Key '" << key << "' was modified" << std::endl;
    }
);

// Access values by key
Value& origin = namedPoints("origin");
```

## Core Classes

### Value

Abstract base class for all values in the reflection system. Provides:
- `type()` - get std::type_info for the underlying type
- `isValid()` - check if this is a valid value (not Invalid)
- `isStruct()` - check if this value is a struct with fields
- `visit(lambda)` - type-safe visitor pattern
- `name()` - get field name (for named fields)

### Object

Extends `Value` for types containing child fields (structs, arrays, maps). Provides:
- `type_erased_fields()` - iterate through all child fields
- `operator()(fieldname)` - runtime field access by name
- `getchild(path)` - access nested fields via path
- `addChildListener()` - listen for changes to any nested field

### Field<T, Name>

Wrapper for struct members that integrates them into the reflection system:
- Template parameters: type `T` and compile-time string literal `Name`
- Derives from `Fundamental<T>` for primitives or `Record<T>` for structs
- Automatically reports its field name via `name()`

### Record<T>

Concrete implementation for struct types with extended features:
- `operator()(CompileTimeString)` - compile-time field access with "_fld" literal
- `fields()` - get typed tuple of all fields
- `visitFields(lambda)` - iterate fields with callback
- `visitField(name, lambda)` - visit specific field by name
- `kFieldNames` - compile-time array of all field names

### Fundamental<T>

Value container with change notification:
- `operator()()` - get underlying value
- `set(value)` - set value and notify listeners
- `mutate(lambda)` - modify value via lambda and notify
- `addListener()` - register change callback

## Path-Based Addressing

The `ID` class represents paths to nested fields:

```cpp
Record<State> state;

// Construct path from string
ID path = ID::fromString("line/start/x");

// Access field via path
Value& field = state.getchild(path);

// Convert path back to string
std::cout << path.toString() << std::endl;  // "line/start/x"
```

## Supported Types

The library supports the following primitive types out of the box:
- `int8_t`, `int16_t`, `int32_t`, `int64_t`
- `float`, `double`
- `std::string`

Custom types can be used as opaque values (no automatic field iteration) or as structs with `Field<>` members for full reflection support.

## Requirements

- C++20 or later
- Dependencies:
  - `fixed_string.hpp` - for compile-time string handling
  - `CxxUtilities.hpp` - utility functions
  - `dynamic_detail.hpp` - implementation details

## Design Philosophy

**Dynamic** is designed to be:
- **Non-intrusive** - wrap only what you need, no base class requirements for your structs
- **Type-safe** - compile-time field names prevent typos and enable refactoring
- **Efficient** - zero overhead when reflection features aren't used
- **Composable** - nested structs, arrays, and maps work seamlessly together

## Advanced Usage

### Recursive Listener Disabling

When modifying values from within a listener, you may want to prevent recursive notifications:

```cpp
// The library uses thread_local recursiveListenerDisabler internally
// to prevent infinite recursion in certain scenarios
```

### Custom Formatters

The library includes `std::formatter` specializations for easy printing:

```cpp
Record<Point> point;
point("x"_fld) = 3.14f;
std::cout << std::format("{}", point) << std::endl;
```

### Mixed Access Patterns

Combine compile-time and runtime access as needed:

```cpp
Record<Point> point;

// Compile-time access (type-safe, zero runtime lookup)
point("x"_fld) = 1.0f;

// Runtime access (dynamic field name)
std::string fieldName = getUserInput();
point.visitField(fieldName, [](auto& field) {
    std::cout << "Value: " << field() << std::endl;
});
```

## License

MIT License

## Contributing

Feel free to report issues and submit pull requests.
