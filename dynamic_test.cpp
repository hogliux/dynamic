#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "3rdparty/doctest/doctest.h"
#include "dynamic.hpp"
#include <format>
#include <sstream>

using namespace dynamic;

//=============================================================================
// Test struct definitions
//=============================================================================

struct Point {
    Field<float, "x"> x;
    Field<float, "y"> y;
};

struct Line {
    Field<Point, "start"> start;
    Field<Point, "finish"> finish;
};

struct State {
    Field<Line, "line"> line;
    Field<int32_t, "count"> count;
    Field<std::string, "name"> name;
    Field<bool, "active"> active;
};

struct WithArray {
    Field<std::string, "label"> label;
};

struct WithContainers {
    Field<std::string, "name"> name;
};

//=============================================================================
// ID tests
//=============================================================================

TEST_SUITE("ID") {

TEST_CASE("default construction") {
    ID id;
    CHECK(id.empty());
    CHECK(id.size() == 0);
}

TEST_CASE("fromString and toString") {
    auto id = ID::fromString("field/subfield/leaf");
    CHECK(id.size() == 3);
    CHECK(id[0] == "field");
    CHECK(id[1] == "subfield");
    CHECK(id[2] == "leaf");
    CHECK(id.toString() == "field/subfield/leaf");
}

TEST_CASE("fromString single element") {
    auto id = ID::fromString("single");
    CHECK(id.size() == 1);
    CHECK(id[0] == "single");
    CHECK(id.toString() == "single");
}

TEST_CASE("equality") {
    ID a = ID::fromString("a/b/c");
    ID b = ID::fromString("a/b/c");
    ID c = ID::fromString("x/y/z");

    CHECK(a == b);
    CHECK_FALSE(a == c);
}

TEST_CASE("copy and move") {
    ID original = ID::fromString("a/b");
    ID copy(original);
    CHECK(copy == original);

    ID moved(std::move(copy));
    CHECK(moved == original);
}

TEST_CASE("assignment") {
    ID a = ID::fromString("a/b");
    ID b;
    b = a;
    CHECK(b == a);

    ID c;
    c = std::move(b);
    CHECK(c == a);
}

TEST_CASE("stream output") {
    auto id = ID::fromString("path/to/field");
    std::ostringstream ss;
    ss << id;
    CHECK(ss.str() == "path/to/field");
}

TEST_CASE("std::format") {
    auto id = ID::fromString("a/b/c");
    CHECK(std::format("{}", id) == "a/b/c");
}

TEST_CASE("construction from string") {
    ID id("hello/world");
    CHECK(id.size() == 2);
    CHECK(id.toString() == "hello/world");
}

} // TEST_SUITE("ID")

//=============================================================================
// Invalid tests
//=============================================================================

TEST_SUITE("Invalid") {

TEST_CASE("kInvalid is not valid") {
    CHECK_FALSE(Value::kInvalid.isValid());
    CHECK_FALSE(static_cast<bool>(Value::kInvalid));
}

TEST_CASE("kInvalid type is void") {
    CHECK(Value::kInvalid.type() == typeid(void));
}

TEST_CASE("assign to kInvalid fails") {
    Fundamental<int32_t> val(42);
    CHECK_FALSE(Value::kInvalid.assign(val));
}

TEST_CASE("kInvalid is not a struct") {
    CHECK_FALSE(Value::kInvalid.isStruct());
}

} // TEST_SUITE("Invalid")

//=============================================================================
// Fundamental<T> tests for each supported type
//=============================================================================

TEST_SUITE("Fundamental") {

TEST_CASE("int32_t") {
    Fundamental<int32_t> val;
    CHECK(val() == 0);
    CHECK(val.isValid());
    CHECK(val.type() == typeid(int32_t));
    CHECK_FALSE(val.isStruct());

    val = 42;
    CHECK(val() == 42);

    // operator T()
    int32_t raw = val;
    CHECK(raw == 42);
}

TEST_CASE("int8_t") {
    Fundamental<int8_t> val;
    val = static_cast<int8_t>(127);
    CHECK(val() == 127);
    CHECK(val.type() == typeid(int8_t));
}

TEST_CASE("int16_t") {
    Fundamental<int16_t> val;
    val = static_cast<int16_t>(1000);
    CHECK(val() == 1000);
}

TEST_CASE("int64_t") {
    Fundamental<int64_t> val;
    val = static_cast<int64_t>(999999999999LL);
    CHECK(val() == 999999999999LL);
}

TEST_CASE("float") {
    Fundamental<float> val;
    val = 3.14f;
    CHECK(val() == doctest::Approx(3.14f));
    CHECK(val.type() == typeid(float));
}

TEST_CASE("double") {
    Fundamental<double> val;
    val = 2.718281828;
    CHECK(val() == doctest::Approx(2.718281828));
    CHECK(val.type() == typeid(double));
}

TEST_CASE("std::string") {
    Fundamental<std::string> val;
    CHECK(val() == "");
    val = std::string("hello");
    CHECK(val() == "hello");
    CHECK(val.type() == typeid(std::string));
}

TEST_CASE("ID as fundamental") {
    Fundamental<ID> val;
    val = ID::fromString("path/to/field");
    CHECK(val().toString() == "path/to/field");
    CHECK(val.type() == typeid(ID));
}

TEST_CASE("copy construction") {
    Fundamental<int32_t> original(42);
    Fundamental<int32_t> copy(original);
    CHECK(copy() == 42);
}

TEST_CASE("move construction") {
    Fundamental<std::string> original(std::string("hello"));
    Fundamental<std::string> moved(std::move(original));
    CHECK(moved() == "hello");
}

TEST_CASE("copy assignment") {
    Fundamental<int32_t> a(10);
    Fundamental<int32_t> b(20);
    b = a;
    CHECK(b() == 10);
}

TEST_CASE("move assignment") {
    Fundamental<std::string> a(std::string("hello"));
    Fundamental<std::string> b(std::string("world"));
    b = std::move(a);
    CHECK(b() == "hello");
}

TEST_CASE("set does not fire if value unchanged") {
    Fundamental<int32_t> val(42);
    int callCount = 0;
    auto token = val.addListener([&callCount](auto const&, auto const&) { callCount++; });
    val = 42; // same value
    CHECK(callCount == 0);
    val = 43; // different value
    CHECK(callCount == 1);
}

TEST_CASE("float set with epsilon comparison") {
    Fundamental<float> val(1.0f);
    int callCount = 0;
    auto token = val.addListener([&callCount](auto const&, auto const&) { callCount++; });

    // Setting the exact same value should not fire
    val = 1.0f;
    CHECK(callCount == 0);

    // Setting a different value should fire
    val = 2.0f;
    CHECK(callCount == 1);
}

TEST_CASE("mutate") {
    Fundamental<int32_t> val(10);
    int callCount = 0;
    int32_t notifiedValue = 0;
    auto token = val.addListener([&](auto const&, int32_t const& newVal) {
        callCount++;
        notifiedValue = newVal;
    });

    val.mutate([](int32_t& v) { v += 5; });
    CHECK(val() == 15);
    CHECK(callCount == 1);
    CHECK(notifiedValue == 15);
}

TEST_CASE("mutate does not fire if value unchanged") {
    Fundamental<int32_t> val(10);
    int callCount = 0;
    auto token = val.addListener([&callCount](auto const&, auto const&) { callCount++; });

    val.mutate([](int32_t& v) { (void)v; /* no change */ });
    CHECK(callCount == 0);
}

TEST_CASE("operator bool for non-bool types checks validity") {
    Fundamental<int32_t> val(0);
    // Even with value 0, Fundamental is always valid
    CHECK(static_cast<bool>(val) == true);
}

TEST_CASE("assign between same types") {
    Fundamental<int32_t> a(42);
    Fundamental<int32_t> b(0);
    CHECK(b.assign(a));
    CHECK(b() == 42);
}

TEST_CASE("assign between different types fails") {
    Fundamental<int32_t> a(42);
    Fundamental<float> b(0.0f);
    CHECK_FALSE(b.assign(a));
}

} // TEST_SUITE("Fundamental")

//=============================================================================
// Fundamental<bool> special behavior
//=============================================================================

TEST_SUITE("Fundamental<bool>") {

TEST_CASE("implicit conversion checks actual value") {
    Fundamental<bool> val;
    val = true;
    // For bool, operator T() returns the actual value
    CHECK(static_cast<bool>(val) == true);

    val = false;
    CHECK(static_cast<bool>(val) == false);
}

TEST_CASE("isValid is always true") {
    Fundamental<bool> val;
    val = false;
    CHECK(val.isValid());
}

TEST_CASE("in conditional context") {
    Fundamental<bool> flag;
    flag = true;

    std::string result = flag ? "enabled" : "disabled";
    CHECK(result == "enabled");

    flag = false;
    result = flag ? "enabled" : "disabled";
    CHECK(result == "disabled");
}

TEST_CASE("logical operations") {
    Fundamental<bool> flag;
    flag = true;
    CHECK((flag && true) == true);
    CHECK((flag || false) == true);

    flag = false;
    CHECK((flag && true) == false);
    CHECK((flag || false) == false);
}

TEST_CASE("comparison") {
    Fundamental<bool> flag;
    flag = true;
    CHECK(flag == true);
    CHECK_FALSE(flag == false);
}

} // TEST_SUITE("Fundamental<bool>")

//=============================================================================
// Record tests
//=============================================================================

TEST_SUITE("Record") {

TEST_CASE("default construction") {
    Record<Point> point;
    CHECK(point("x"_fld)() == 0.0f);
    CHECK(point("y"_fld)() == 0.0f);
    CHECK(point.isValid());
    CHECK(point.isStruct());
}

TEST_CASE("compile-time field access") {
    Record<Point> point;
    point("x"_fld) = 3.14f;
    point("y"_fld) = 2.72f;
    CHECK(point("x"_fld)() == doctest::Approx(3.14f));
    CHECK(point("y"_fld)() == doctest::Approx(2.72f));
}

TEST_CASE("runtime field access by name") {
    Record<Point> point;
    point("x"_fld) = 5.0f;

    Value& xField = static_cast<Object&>(point)("x");
    CHECK(xField.isValid());
    CHECK(xField.type() == typeid(float));
}

TEST_CASE("runtime access to nonexistent field returns kInvalid") {
    Record<Point> point;
    Value& bad = static_cast<Object&>(point)("nonexistent");
    CHECK_FALSE(bad.isValid());
}

TEST_CASE("kFieldNames") {
    CHECK(Record<Point>::kFieldNames.size() == 2);
    CHECK(Record<Point>::kFieldNames[0] == "x");
    CHECK(Record<Point>::kFieldNames[1] == "y");
}

TEST_CASE("type_erased_fields") {
    Record<Point> point;
    point("x"_fld) = 1.0f;
    point("y"_fld) = 2.0f;

    auto fields = static_cast<Object&>(point).type_erased_fields();
    CHECK(fields.size() == 2);
    CHECK(fields[0].get().name() == "x");
    CHECK(fields[1].get().name() == "y");
}

TEST_CASE("visitFields") {
    Record<Point> point;
    point("x"_fld) = 10.0f;
    point("y"_fld) = 20.0f;

    std::vector<std::string> names;
    point.visitFields([&names](std::string_view name, auto& fld) {
        names.emplace_back(name);
        (void)fld;
    });
    CHECK(names.size() == 2);
    CHECK(names[0] == "x");
    CHECK(names[1] == "y");
}

TEST_CASE("visitField") {
    Record<Point> point;
    point("x"_fld) = 7.5f;

    auto result = point.visitField("x", [](auto& fld) { return fld(); });
    CHECK(result.has_value());
    CHECK(*result == doctest::Approx(7.5f));

    auto missing = point.visitField("z", [](auto& fld) { return fld(); });
    CHECK_FALSE(missing.has_value());
}

TEST_CASE("nested records") {
    Record<Line> line;
    line("start"_fld)("x"_fld) = 1.0f;
    line("start"_fld)("y"_fld) = 2.0f;
    line("finish"_fld)("x"_fld) = 3.0f;
    line("finish"_fld)("y"_fld) = 4.0f;

    CHECK(line("start"_fld)("x"_fld)() == doctest::Approx(1.0f));
    CHECK(line("finish"_fld)("y"_fld)() == doctest::Approx(4.0f));
}

TEST_CASE("copy construction") {
    Record<Point> original;
    original("x"_fld) = 5.0f;
    original("y"_fld) = 10.0f;

    Record<Point> copy(original);
    CHECK(copy("x"_fld)() == doctest::Approx(5.0f));
    CHECK(copy("y"_fld)() == doctest::Approx(10.0f));
}

TEST_CASE("copy does not copy listeners") {
    Record<Point> original;
    int callCount = 0;
    auto token = original("x"_fld).addListener([&callCount](auto const&, auto const&) { callCount++; });

    Record<Point> copy(original);
    copy("x"_fld) = 99.0f;
    CHECK(callCount == 0); // copy should not have the listener
}

TEST_CASE("copy assignment") {
    Record<Point> a;
    a("x"_fld) = 1.0f;
    a("y"_fld) = 2.0f;

    Record<Point> b;
    b = a;
    CHECK(b("x"_fld)() == doctest::Approx(1.0f));
    CHECK(b("y"_fld)() == doctest::Approx(2.0f));
}

TEST_CASE("assignChild on record") {
    Record<Point> point;
    Fundamental<float> newX(42.0f);
    CHECK(static_cast<Object&>(point).assignChild("x", newX));
    CHECK(point("x"_fld)() == doctest::Approx(42.0f));
}

TEST_CASE("assignChild with invalid field name") {
    Record<Point> point;
    Fundamental<float> val(1.0f);
    CHECK_FALSE(static_cast<Object&>(point).assignChild("nonexistent", val));
}

TEST_CASE("removeChild from record always fails") {
    Record<Point> point;
    CHECK_FALSE(static_cast<Object&>(point).removeChild("x"));
}

TEST_CASE("record with mixed types") {
    Record<State> state;
    state("count"_fld) = 42;
    state("name"_fld) = std::string("test");
    state("active"_fld) = true;
    state("line"_fld)("start"_fld)("x"_fld) = 1.0f;

    CHECK(state("count"_fld)() == 42);
    CHECK(state("name"_fld)() == "test");
    CHECK(state("active"_fld)() == true);
    CHECK(state("line"_fld)("start"_fld)("x"_fld)() == doctest::Approx(1.0f));
}

} // TEST_SUITE("Record")

//=============================================================================
// Visitor pattern tests
//=============================================================================

TEST_SUITE("Visitor") {

TEST_CASE("visit with matching type") {
    Fundamental<int32_t> val(42);
    Value& ref = val;
    bool visited = false;
    ref.visit([&visited](int32_t const& v) {
        CHECK(v == 42);
        visited = true;
    });
    CHECK(visited);
}

TEST_CASE("visit with auto lambda") {
    Fundamental<float> val(3.14f);
    Value& ref = val;
    std::string result;
    ref.visit([&result](auto const& v) {
        std::ostringstream ss;
        ss << v;
        result = ss.str();
    });
    CHECK_FALSE(result.empty());
}

TEST_CASE("visit on bool type") {
    Fundamental<bool> val;
    val = true;
    Value& ref = val;
    bool visitedBool = false;
    ref.visit([&visitedBool](bool b) {
        CHECK(b == true);
        visitedBool = true;
    });
    CHECK(visitedBool);
}

TEST_CASE("visit on ID type") {
    Fundamental<ID> val;
    val = ID::fromString("a/b");
    Value& ref = val;
    bool visitedID = false;
    ref.visit([&visitedID](ID const& id) {
        CHECK(id.toString() == "a/b");
        visitedID = true;
    });
    CHECK(visitedID);
}

TEST_CASE("const visit") {
    Fundamental<int32_t> val(42);
    Value const& ref = val;
    bool visited = false;
    ref.visit([&visited](int32_t const& v) {
        CHECK(v == 42);
        visited = true;
    });
    CHECK(visited);
}

TEST_CASE("mutable visit modifies value") {
    Fundamental<int32_t> val(10);
    Value& ref = val;
    ref.visit([](int32_t& v) { v = 20; });
    CHECK(val() == 20);
}

TEST_CASE("visit Object") {
    Record<Point> point;
    point("x"_fld) = 5.0f;
    Value& ref = static_cast<Object&>(point);
    bool visitedObject = false;
    ref.visit([&visitedObject](Object const&) {
        visitedObject = true;
    });
    CHECK(visitedObject);
}

TEST_CASE("visit Invalid") {
    bool visitedInvalid = false;
    Value& ref = Value::kInvalid;
    ref.visit([&visitedInvalid](Invalid const&) {
        visitedInvalid = true;
    });
    CHECK(visitedInvalid);
}

} // TEST_SUITE("Visitor")

//=============================================================================
// Listener tests (token-based)
//=============================================================================

TEST_SUITE("Listeners") {

TEST_CASE("token-based value listener fires on change") {
    Fundamental<int32_t> val(0);
    int callCount = 0;
    int32_t lastValue = 0;

    auto token = val.addListener([&](Fundamental<int32_t> const&, int32_t const& newVal) {
        callCount++;
        lastValue = newVal;
    });

    val = 10;
    CHECK(callCount == 1);
    CHECK(lastValue == 10);

    val = 20;
    CHECK(callCount == 2);
    CHECK(lastValue == 20);
}

TEST_CASE("token destruction removes listener") {
    Fundamental<int32_t> val(0);
    int callCount = 0;

    {
        auto token = val.addListener([&callCount](auto const&, auto const&) { callCount++; });
        val = 1;
        CHECK(callCount == 1);
    } // token destroyed here

    val = 2;
    CHECK(callCount == 1); // should not have incremented
}

TEST_CASE("ListenerToken move semantics") {
    Fundamental<int32_t> val(0);
    int callCount = 0;

    ListenerToken token1 = val.addListener([&callCount](auto const&, auto const&) { callCount++; });
    ListenerToken token2 = std::move(token1);

    val = 1;
    CHECK(callCount == 1);

    // token2 still holds the listener
}

TEST_CASE("multiple listeners") {
    Fundamental<int32_t> val(0);
    int count1 = 0, count2 = 0;

    auto token1 = val.addListener([&count1](auto const&, auto const&) { count1++; });
    auto token2 = val.addListener([&count2](auto const&, auto const&) { count2++; });

    val = 5;
    CHECK(count1 == 1);
    CHECK(count2 == 1);
}

TEST_CASE("weak_ptr-based listener fires") {
    struct Context : std::enable_shared_from_this<Context> {
        int callCount = 0;
    };

    auto ctx = std::make_shared<Context>();
    Fundamental<int32_t> val(0);

    val.addListener(ctx.get(), [&ctx](auto const&, auto const&) {
        ctx->callCount++;
    });

    val = 10;
    CHECK(ctx->callCount == 1);
}

TEST_CASE("weak_ptr-based listener stops when context destroyed") {
    Fundamental<int32_t> val(0);
    int callCount = 0;

    {
        struct Context : std::enable_shared_from_this<Context> {};
        auto ctx = std::make_shared<Context>();

        val.addListener(ctx.get(), [&callCount](auto const&, auto const&) {
            callCount++;
        });

        val = 1;
        CHECK(callCount == 1);
    } // ctx destroyed

    val = 2;
    // Listener may still be registered but wrappedLambda checks the weak_ptr
    // The actual removal happens lazily on next addListener call
    // So we just check the count didn't increment (the lambda checks weak_ptr)
    CHECK(callCount == 1);
}

TEST_CASE("child listener on Record") {
    Record<Point> point;
    int callCount = 0;
    ID lastId;

    auto token = static_cast<Object&>(point).addChildListener(
        [&](ID const& id, Object::Operation op, Object const&, Value const&) {
            callCount++;
            lastId = id;
            CHECK(op == Object::Operation::modify);
        });

    point("x"_fld) = 5.0f;
    CHECK(callCount == 1);
    CHECK(lastId.toString() == "x");

    point("y"_fld) = 10.0f;
    CHECK(callCount == 2);
    CHECK(lastId.toString() == "y");
}

TEST_CASE("child listener propagation through nested hierarchy") {
    Record<Line> line;
    int callCount = 0;
    std::string lastPath;

    auto token = static_cast<Object&>(line).addChildListener(
        [&](ID const& id, Object::Operation, Object const&, Value const&) {
            callCount++;
            lastPath = id.toString();
        });

    line("start"_fld)("x"_fld) = 1.0f;
    CHECK(callCount == 1);
    CHECK(lastPath == "start/x");

    line("finish"_fld)("y"_fld) = 2.0f;
    CHECK(callCount == 2);
    CHECK(lastPath == "finish/y");
}

TEST_CASE("child listener token removal") {
    Record<Point> point;
    int callCount = 0;

    {
        auto token = static_cast<Object&>(point).addChildListener(
            [&callCount](ID const&, Object::Operation, Object const&, Value const&) {
                callCount++;
            });
        point("x"_fld) = 1.0f;
        CHECK(callCount == 1);
    } // token destroyed

    point("x"_fld) = 2.0f;
    CHECK(callCount == 1); // should not increment
}

TEST_CASE("listener on bool value") {
    Fundamental<bool> val;
    val = false;
    int callCount = 0;
    bool lastValue = false;

    auto token = val.addListener([&](auto const&, bool const& newVal) {
        callCount++;
        lastValue = newVal;
    });

    val = true;
    CHECK(callCount == 1);
    CHECK(lastValue == true);

    val = false;
    CHECK(callCount == 2);
    CHECK(lastValue == false);
}

TEST_CASE("listener on ID value") {
    Fundamental<ID> val;
    val = ID::fromString("initial");
    int callCount = 0;

    auto token = val.addListener([&callCount](auto const&, ID const&) {
        callCount++;
    });

    val = ID::fromString("changed");
    CHECK(callCount == 1);
}

TEST_CASE("listener on string value") {
    Fundamental<std::string> val;
    val = std::string("hello");
    std::string lastValue;

    auto token = val.addListener([&lastValue](auto const&, std::string const& newVal) {
        lastValue = newVal;
    });

    val = std::string("world");
    CHECK(lastValue == "world");
}

} // TEST_SUITE("Listeners")

//=============================================================================
// Path-based addressing tests
//=============================================================================

TEST_SUITE("Path addressing") {

TEST_CASE("getchild single level") {
    Record<Point> point;
    point("x"_fld) = 7.0f;

    auto id = ID::fromString("x");
    Value& child = static_cast<Object&>(point).getchild(id);
    CHECK(child.isValid());
    CHECK(child.type() == typeid(float));
}

TEST_CASE("getchild nested") {
    Record<Line> line;
    line("start"_fld)("x"_fld) = 42.0f;

    auto id = ID::fromString("start/x");
    Value& child = static_cast<Object&>(line).getchild(id);
    CHECK(child.isValid());

    child.visit([](float const& v) {
        CHECK(v == doctest::Approx(42.0f));
    });
}

TEST_CASE("getchild with empty path returns self") {
    Record<Point> point;
    ID emptyId;
    Value& result = static_cast<Object&>(point).getchild(emptyId);
    CHECK(&result == static_cast<Value*>(static_cast<Object*>(&point)));
}

TEST_CASE("getchild with invalid path returns kInvalid") {
    Record<Point> point;
    auto id = ID::fromString("nonexistent/field");
    Value& child = static_cast<Object&>(point).getchild(id);
    CHECK_FALSE(child.isValid());
}

TEST_CASE("runtime operator() field access") {
    Record<Point> point;
    point("x"_fld) = 3.0f;

    Value& x = static_cast<Object&>(point)("x");
    CHECK(x.isValid());
    CHECK(x.name() == "x");
}

TEST_CASE("const getchild") {
    Record<Line> line;
    line("start"_fld)("x"_fld) = 5.0f;

    Object const& constLine = static_cast<Object const&>(line);
    auto id = ID::fromString("start/x");
    Value const& child = constLine.getchild(id);
    CHECK(child.isValid());
}

} // TEST_SUITE("Path addressing")

//=============================================================================
// Array tests
//=============================================================================

TEST_SUITE("Array") {

TEST_CASE("default construction") {
    Array<int32_t> arr;
    CHECK(arr.size() == 0);
    CHECK(arr.isValid());
    CHECK(arr.isStruct());
    CHECK(arr.isMapOrArray());
    CHECK(arr.elementType() == typeid(int32_t));
}

TEST_CASE("addElement and size") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);
    CHECK(arr.size() == 3);
}

TEST_CASE("access elements by index") {
    Array<float> arr;
    arr.addElement(1.0f);
    arr.addElement(2.0f);

    Value& elem0 = static_cast<Object&>(arr)("0");
    CHECK(elem0.isValid());
    elem0.visit([](float const& v) { CHECK(v == doctest::Approx(1.0f)); });

    Value& elem1 = static_cast<Object&>(arr)("1");
    CHECK(elem1.isValid());
}

TEST_CASE("type_erased_fields") {
    Array<int32_t> arr;
    arr.addElement(100);
    arr.addElement(200);

    auto fields = static_cast<Object&>(arr).type_erased_fields();
    CHECK(fields.size() == 2);
    CHECK(fields[0].get().name() == "0");
    CHECK(fields[1].get().name() == "1");
}

TEST_CASE("removeElement") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);

    arr.removeElement(1); // remove middle element
    CHECK(arr.size() == 2);
}

TEST_CASE("array listener fires on add") {
    Array<int32_t> arr;
    int callCount = 0;
    Object::Operation lastOp;

    auto token = arr.addListener([&](Object::Operation op, Array<int32_t> const&, int32_t const&, std::size_t) {
        callCount++;
        lastOp = op;
    });

    arr.addElement(42);
    CHECK(callCount == 1);
    CHECK(lastOp == Object::Operation::add);
}

TEST_CASE("array listener fires on remove") {
    Array<int32_t> arr;
    arr.addElement(10);

    Object::Operation lastOp;
    auto token = arr.addListener([&lastOp](Object::Operation op, auto const&, auto const&, std::size_t) {
        lastOp = op;
    });

    arr.removeElement(0);
    CHECK(lastOp == Object::Operation::remove);
}

TEST_CASE("array of bools") {
    Array<bool> arr;
    arr.addElement(true);
    arr.addElement(false);
    arr.addElement(true);
    CHECK(arr.size() == 3);
}

TEST_CASE("array of IDs") {
    Array<ID> arr;
    arr.addElement(ID::fromString("a/b"));
    arr.addElement(ID::fromString("c/d"));
    CHECK(arr.size() == 2);
}

TEST_CASE("array of structs") {
    Array<Point> arr;
    Point p1; p1.x = 1.0f; p1.y = 2.0f;
    Point p2; p2.x = 3.0f; p2.y = 4.0f;
    arr.addElement(p1);
    arr.addElement(p2);
    CHECK(arr.size() == 2);
}

TEST_CASE("array equality") {
    Array<int32_t> a, b;
    a.addElement(1);
    a.addElement(2);
    b.addElement(1);
    b.addElement(2);
    CHECK(a == b);

    b.addElement(3);
    CHECK_FALSE(a == b);
}

TEST_CASE("array copy construction") {
    Array<int32_t> original;
    original.addElement(10);
    original.addElement(20);

    Array<int32_t> copy(original);
    CHECK(copy.size() == 2);
    CHECK(copy == original);
}

TEST_CASE("array assign") {
    Array<int32_t> a;
    a.addElement(1);
    a.addElement(2);

    Array<int32_t> b;
    b.addElement(100);

    b = a;
    CHECK(b.size() == 2);
    CHECK(a == b);
}

TEST_CASE("array assignChild") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);

    Fundamental<int32_t> newVal(99);
    CHECK(static_cast<Object&>(arr).assignChild("0", newVal));

    Value& elem = static_cast<Object&>(arr)("0");
    elem.visit([](int32_t const& v) { CHECK(v == 99); });
}

TEST_CASE("array assignChild adds new elements") {
    Array<int32_t> arr;
    Fundamental<int32_t> newVal(42);
    CHECK(static_cast<Object&>(arr).assignChild("0", newVal));
    CHECK(arr.size() == 1);
}

TEST_CASE("array removeChild") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);

    CHECK(static_cast<Object&>(arr).removeChild("0"));
    CHECK(arr.size() == 1);
}

TEST_CASE("array removeChild with invalid index") {
    Array<int32_t> arr;
    arr.addElement(10);
    CHECK_FALSE(static_cast<Object&>(arr).removeChild("5"));
    CHECK_FALSE(static_cast<Object&>(arr).removeChild("abc"));
}

TEST_CASE("array child listener propagation") {
    Array<Point> arr;
    Point p; p.x = 0.0f; p.y = 0.0f;
    arr.addElement(p);

    int childCallCount = 0;
    auto token = static_cast<Object&>(arr).addChildListener(
        [&childCallCount](ID const&, Object::Operation, Object const&, Value const&) {
            childCallCount++;
        });

    // Modifying a nested field of an array element should fire child listener
    auto& elem = static_cast<Object&>(arr)("0");
    CHECK(elem.isStruct());
    auto& xField = static_cast<Object&>(elem)("x");
    xField.visit([](float& v) { v = 99.0f; });
    CHECK(childCallCount == 1);
}

TEST_CASE("array type") {
    Array<int32_t> arr;
    CHECK(arr.type() == typeid(Array<int32_t>));
}

} // TEST_SUITE("Array")

//=============================================================================
// Map tests
//=============================================================================

TEST_SUITE("Map") {

TEST_CASE("default construction") {
    Map<int32_t> map;
    CHECK(map.size() == 0);
    CHECK(map.isValid());
    CHECK(map.isStruct());
    CHECK(map.isMapOrArray());
    CHECK(map.elementType() == typeid(int32_t));
}

TEST_CASE("addElement and size") {
    Map<int32_t> map;
    map.addElement("one", 1);
    map.addElement("two", 2);
    CHECK(map.size() == 2);
}

TEST_CASE("access elements by key") {
    Map<std::string> map;
    map.addElement("greeting", std::string("hello"));

    Value& elem = static_cast<Object&>(map)("greeting");
    CHECK(elem.isValid());
    CHECK(elem.name() == "greeting");
}

TEST_CASE("type_erased_fields") {
    Map<int32_t> map;
    map.addElement("a", 1);
    map.addElement("b", 2);

    auto fields = static_cast<Object&>(map).type_erased_fields();
    CHECK(fields.size() == 2);
}

TEST_CASE("addElement with existing key updates value") {
    Map<int32_t> map;
    map.addElement("key", 10);
    CHECK(map.size() == 1);

    map.addElement("key", 20);
    CHECK(map.size() == 1); // should not add duplicate
}

TEST_CASE("removeElement") {
    Map<int32_t> map;
    map.addElement("a", 1);
    map.addElement("b", 2);

    CHECK(map.removeElement("a"));
    CHECK(map.size() == 1);
    CHECK_FALSE(map.removeElement("nonexistent"));
}

TEST_CASE("map listener fires on add") {
    Map<int32_t> map;
    int callCount = 0;
    Object::Operation lastOp;
    std::string lastKey;

    auto token = map.addListener([&](Object::Operation op, Map<int32_t> const&, int32_t const&, std::string_view key) {
        callCount++;
        lastOp = op;
        lastKey = std::string(key);
    });

    map.addElement("test", 42);
    CHECK(callCount == 1);
    CHECK(lastOp == Object::Operation::add);
    CHECK(lastKey == "test");
}

TEST_CASE("map listener fires on remove") {
    Map<int32_t> map;
    map.addElement("a", 1);

    Object::Operation lastOp;
    auto token = map.addListener([&lastOp](Object::Operation op, auto const&, auto const&, std::string_view) {
        lastOp = op;
    });

    map.removeElement("a");
    CHECK(lastOp == Object::Operation::remove);
}

TEST_CASE("map of structs") {
    Map<Point> map;
    Point p; p.x = 1.0f; p.y = 2.0f;
    map.addElement("origin", p);
    CHECK(map.size() == 1);

    Value& elem = static_cast<Object&>(map)("origin");
    CHECK(elem.isStruct());
}

TEST_CASE("map equality") {
    Map<int32_t> a, b;
    a.addElement("x", 1);
    a.addElement("y", 2);
    b.addElement("x", 1);
    b.addElement("y", 2);
    CHECK(a == b);

    b.addElement("z", 3);
    CHECK_FALSE(a == b);
}

TEST_CASE("map copy construction") {
    Map<int32_t> original;
    original.addElement("a", 10);
    original.addElement("b", 20);

    Map<int32_t> copy(original);
    CHECK(copy.size() == 2);
    CHECK(copy == original);
}

TEST_CASE("map assign") {
    Map<int32_t> a;
    a.addElement("x", 1);

    Map<int32_t> b;
    b.addElement("y", 2);
    b.addElement("z", 3);

    b = a;
    CHECK(b.size() == 1);
    CHECK(a == b);
}

TEST_CASE("map assignChild new key") {
    Map<int32_t> map;
    Fundamental<int32_t> val(42);
    CHECK(static_cast<Object&>(map).assignChild("newkey", val));
    CHECK(map.size() == 1);
}

TEST_CASE("map assignChild existing key") {
    Map<int32_t> map;
    map.addElement("key", 10);

    Fundamental<int32_t> val(20);
    CHECK(static_cast<Object&>(map).assignChild("key", val));
    CHECK(map.size() == 1);
}

TEST_CASE("map removeChild") {
    Map<int32_t> map;
    map.addElement("a", 1);
    CHECK(static_cast<Object&>(map).removeChild("a"));
    CHECK(map.size() == 0);
}

TEST_CASE("map removeChild nonexistent") {
    Map<int32_t> map;
    CHECK_FALSE(static_cast<Object&>(map).removeChild("nope"));
}

TEST_CASE("map type") {
    Map<int32_t> map;
    CHECK(map.type() == typeid(Map<int32_t>));
}

} // TEST_SUITE("Map")

//=============================================================================
// Stream and format output tests
//=============================================================================

TEST_SUITE("Stream output") {

TEST_CASE("Value stream output") {
    Fundamental<int32_t> val(42);
    std::ostringstream ss;
    ss << static_cast<Value const&>(val);
    CHECK(ss.str() == "42");
}

TEST_CASE("Object stream output") {
    Record<Point> point;
    point("x"_fld) = 1.0f;
    point("y"_fld) = 2.0f;
    std::ostringstream ss;
    ss << static_cast<Object const&>(point);
    std::string result = ss.str();
    CHECK(result.find(".x = ") != std::string::npos);
    CHECK(result.find(".y = ") != std::string::npos);
}

TEST_CASE("std::format with Value") {
    Fundamental<int32_t> val(123);
    CHECK(std::format("{}", static_cast<Value const&>(val)) == "123");
}

TEST_CASE("std::format with Object") {
    Record<Point> point;
    point("x"_fld) = 1.0f;
    point("y"_fld) = 2.0f;
    auto result = std::format("{}", static_cast<Object const&>(point));
    CHECK(result.find(".x = ") != std::string::npos);
}

TEST_CASE("std::format with ID") {
    auto id = ID::fromString("a/b/c");
    CHECK(std::format("{}", id) == "a/b/c");
}

TEST_CASE("bool stream output") {
    Fundamental<bool> val;
    val = true;
    std::ostringstream ss;
    ss << static_cast<Value const&>(val);
    CHECK(ss.str() == "1");
}

TEST_CASE("string stream output") {
    Fundamental<std::string> val(std::string("hello"));
    std::ostringstream ss;
    ss << static_cast<Value const&>(val);
    CHECK(ss.str() == "hello");
}

} // TEST_SUITE("Stream output")

//=============================================================================
// MetaType tests
//=============================================================================

TEST_SUITE("MetaType") {

TEST_CASE("opaque type meta") {
    auto const& meta = metaTypeOf<float>();
    CHECK(meta.isOpaque());
    CHECK_FALSE(meta.isRecord());
    CHECK_FALSE(meta.isArray());
    CHECK_FALSE(meta.isMap());
    CHECK(meta.typeInfo() == typeid(float));
    CHECK(meta.fields().empty());
    CHECK(meta.elementMetaType() == nullptr);
}

TEST_CASE("all supported fundamental types are opaque") {
    CHECK(metaTypeOf<int8_t>().isOpaque());
    CHECK(metaTypeOf<int16_t>().isOpaque());
    CHECK(metaTypeOf<int32_t>().isOpaque());
    CHECK(metaTypeOf<int64_t>().isOpaque());
    CHECK(metaTypeOf<float>().isOpaque());
    CHECK(metaTypeOf<double>().isOpaque());
    CHECK(metaTypeOf<bool>().isOpaque());
    CHECK(metaTypeOf<std::string>().isOpaque());
    CHECK(metaTypeOf<ID>().isOpaque());
}

TEST_CASE("record type meta") {
    auto const& meta = metaTypeOf<Point>();
    CHECK_FALSE(meta.isOpaque());
    CHECK(meta.isRecord());
    CHECK_FALSE(meta.isArray());
    CHECK_FALSE(meta.isMap());
    CHECK(meta.typeInfo() == typeid(Point));

    auto fields = meta.fields();
    CHECK(fields.size() == 2);
    CHECK(fields[0].name == "x");
    CHECK(fields[1].name == "y");
    CHECK(fields[0].metaType().isOpaque());
    CHECK(fields[0].metaType().typeInfo() == typeid(float));
}

TEST_CASE("nested record meta") {
    auto const& meta = metaTypeOf<Line>();
    CHECK(meta.isRecord());
    auto fields = meta.fields();
    CHECK(fields.size() == 2);
    CHECK(fields[0].name == "start");
    CHECK(fields[1].name == "finish");

    // Nested field types should be records
    auto const& startMeta = fields[0].metaType();
    CHECK(startMeta.isRecord());
    CHECK(startMeta.typeInfo() == typeid(Point));
    CHECK(startMeta.fields().size() == 2);
}

TEST_CASE("array meta") {
    auto const& meta = Array<Point>::meta();
    CHECK_FALSE(meta.isOpaque());
    CHECK_FALSE(meta.isRecord());
    CHECK(meta.isArray());
    CHECK_FALSE(meta.isMap());
    CHECK(meta.typeInfo() == typeid(Array<Point>));

    auto* elemMeta = meta.elementMetaType();
    CHECK(elemMeta != nullptr);
    CHECK(elemMeta->isRecord());
    CHECK(elemMeta->typeInfo() == typeid(Point));
}

TEST_CASE("map meta") {
    auto const& meta = Map<int32_t>::meta();
    CHECK_FALSE(meta.isOpaque());
    CHECK_FALSE(meta.isRecord());
    CHECK_FALSE(meta.isArray());
    CHECK(meta.isMap());
    CHECK(meta.typeInfo() == typeid(Map<int32_t>));

    auto* elemMeta = meta.elementMetaType();
    CHECK(elemMeta != nullptr);
    CHECK(elemMeta->isOpaque());
    CHECK(elemMeta->typeInfo() == typeid(int32_t));
}

TEST_CASE("construct via MetaType") {
    auto const& pointMeta = Record<Point>::meta();
    auto instance = pointMeta.construct();
    CHECK(instance != nullptr);
    CHECK(instance->isValid());
    CHECK(instance->isStruct());
    CHECK(instance->type() == typeid(Point));
}

TEST_CASE("construct fundamental via MetaType") {
    auto const& meta = metaTypeOf<int32_t>();
    auto instance = meta.construct();
    CHECK(instance != nullptr);
    CHECK(instance->isValid());
    CHECK(instance->type() == typeid(int32_t));
}

TEST_CASE("construct array via MetaType") {
    auto const& meta = Array<float>::meta();
    auto instance = meta.construct();
    CHECK(instance != nullptr);
    CHECK(instance->isStruct());
}

TEST_CASE("construct map via MetaType") {
    auto const& meta = Map<std::string>::meta();
    auto instance = meta.construct();
    CHECK(instance != nullptr);
    CHECK(instance->isStruct());
}

TEST_CASE("Invalid metaType") {
    auto const& meta = Value::kInvalid.metaType();
    CHECK(meta.isOpaque());
    CHECK(meta.typeInfo() == typeid(void));
    CHECK(meta.construct() == nullptr);
}

TEST_CASE("metaType from instance matches static meta") {
    Record<Point> point;
    CHECK(&point.metaType() == &Record<Point>::meta());

    Fundamental<int32_t> val;
    CHECK(&val.metaType() == &Fundamental<int32_t>::meta());

    Array<float> arr;
    CHECK(&arr.metaType() == &Array<float>::meta());

    Map<int32_t> map;
    CHECK(&map.metaType() == &Map<int32_t>::meta());
}

TEST_CASE("meta via Fundamental::meta") {
    auto const& meta = Fundamental<float>::meta();
    CHECK(meta.isOpaque());
    CHECK(meta.typeInfo() == typeid(float));
}

TEST_CASE("metaTypeOf returns singletons") {
    auto const& a = metaTypeOf<Point>();
    auto const& b = metaTypeOf<Point>();
    CHECK(&a == &b);
}

} // TEST_SUITE("MetaType")

//=============================================================================
// Field tests
//=============================================================================

TEST_SUITE("Field") {

TEST_CASE("field name") {
    Record<Point> point;
    CHECK(point("x"_fld).name() == "x");
    CHECK(point("y"_fld).name() == "y");
}

TEST_CASE("field assignment") {
    Record<Point> point;
    point("x"_fld) = 3.14f;
    CHECK(point("x"_fld)() == doctest::Approx(3.14f));
}

TEST_CASE("nested field names via kFieldNames") {
    CHECK(Record<Line>::kFieldNames[0] == "start");
    CHECK(Record<Line>::kFieldNames[1] == "finish");
}

TEST_CASE("field type info") {
    Record<Point> point;
    CHECK(point("x"_fld).type() == typeid(float));
}

} // TEST_SUITE("Field")

//=============================================================================
// Edge cases and integration tests
//=============================================================================

TEST_SUITE("Integration") {

TEST_CASE("deeply nested path addressing") {
    Record<State> state;
    state("line"_fld)("start"_fld)("x"_fld) = 42.0f;

    auto id = ID::fromString("line/start/x");
    Value& child = static_cast<Object&>(state).getchild(id);
    CHECK(child.isValid());
    child.visit([](float const& v) { CHECK(v == doctest::Approx(42.0f)); });
}

TEST_CASE("deeply nested child listener") {
    Record<State> state;
    std::string lastPath;

    auto token = static_cast<Object&>(state).addChildListener(
        [&lastPath](ID const& id, Object::Operation, Object const&, Value const&) {
            lastPath = id.toString();
        });

    state("line"_fld)("start"_fld)("x"_fld) = 1.0f;
    CHECK(lastPath == "line/start/x");

    state("count"_fld) = 99;
    CHECK(lastPath == "count");

    state("name"_fld) = std::string("hello");
    CHECK(lastPath == "name");
}

TEST_CASE("array within record addressing") {
    // Simulate a more complex struct with array
    Array<Point> points;
    Point p; p.x = 5.0f; p.y = 10.0f;
    points.addElement(p);

    // Access through array
    Value& elem = static_cast<Object&>(points)("0");
    CHECK(elem.isStruct());

    Value& xField = static_cast<Object&>(elem)("x");
    CHECK(xField.isValid());
}

TEST_CASE("map within record addressing") {
    Map<Point> pointMap;
    Point p; p.x = 1.0f; p.y = 2.0f;
    pointMap.addElement("origin", p);

    Value& elem = static_cast<Object&>(pointMap)("origin");
    CHECK(elem.isStruct());

    Value& yField = static_cast<Object&>(elem)("y");
    CHECK(yField.isValid());
}

TEST_CASE("Value assign between Records") {
    Record<Point> a;
    a("x"_fld) = 1.0f;
    a("y"_fld) = 2.0f;

    Record<Point> b;
    Value& aRef = static_cast<Object&>(a);
    Value& bRef = static_cast<Object&>(b);

    CHECK(bRef.assign(aRef));
    CHECK(b("x"_fld)() == doctest::Approx(1.0f));
    CHECK(b("y"_fld)() == doctest::Approx(2.0f));
}

TEST_CASE("multiple child listeners on same object") {
    Record<Point> point;
    int count1 = 0, count2 = 0;

    auto token1 = static_cast<Object&>(point).addChildListener(
        [&count1](ID const&, Object::Operation, Object const&, Value const&) { count1++; });
    auto token2 = static_cast<Object&>(point).addChildListener(
        [&count2](ID const&, Object::Operation, Object const&, Value const&) { count2++; });

    point("x"_fld) = 5.0f;
    CHECK(count1 == 1);
    CHECK(count2 == 1);
}

TEST_CASE("listener not called for unchanged values") {
    Record<Point> point;
    point("x"_fld) = 5.0f;

    int callCount = 0;
    auto token = static_cast<Object&>(point).addChildListener(
        [&callCount](ID const&, Object::Operation, Object const&, Value const&) { callCount++; });

    point("x"_fld) = 5.0f; // same value
    CHECK(callCount == 0);

    point("x"_fld) = 6.0f; // different value
    CHECK(callCount == 1);
}

TEST_CASE("construct record from underlying struct") {
    Point p;
    p.x = 10.0f;
    p.y = 20.0f;
    Record<Point> point(p);
    CHECK(point("x"_fld)() == doctest::Approx(10.0f));
    CHECK(point("y"_fld)() == doctest::Approx(20.0f));
}

TEST_CASE("isOpaque for various types") {
    CHECK(Value::isOpaque<int32_t>());
    CHECK(Value::isOpaque<float>());
    CHECK(Value::isOpaque<std::string>());
    CHECK(Value::isOpaque<bool>());
    CHECK(Value::isOpaque<ID>());
    CHECK_FALSE(Value::isOpaque<Point>());
    CHECK_FALSE(Value::isOpaque<Line>());
}

TEST_CASE("elementType for containers") {
    Array<int32_t> arr;
    CHECK(arr.elementType() == typeid(int32_t));

    Map<float> map;
    CHECK(map.elementType() == typeid(float));
}

TEST_CASE("Record elementType is void") {
    Record<Point> point;
    CHECK(static_cast<Object&>(point).elementType() == typeid(void));
}

} // TEST_SUITE("Integration")

//=============================================================================
// ListenerToken RAII tests
//=============================================================================

TEST_SUITE("ListenerToken") {

TEST_CASE("default constructed token does nothing on destruction") {
    ListenerToken token;
    // Should not crash on destruction
}

// TEST_CASE("move assignment replaces and cleans up") {
//     int removeCount1 = 0, removeCount2 = 0;

//     ListenerToken token1(ListenerToken([&removeCount1]() { removeCount1++; }));
//     ListenerToken token2(ListenerToken([&removeCount2]() { removeCount2++; }));

//     token1 = std::move(token2);
//     CHECK(removeCount1 == 1); // token1's original callback was invoked
//     CHECK(removeCount2 == 0); // token2's callback moved to token1
// }

// TEST_CASE("moved-from token does nothing") {
//     int removeCount = 0;
//     ListenerToken token1(ListenerToken([&removeCount]() { removeCount++; }));
//     ListenerToken token2(std::move(token1));

//     // destroying token1 should do nothing
//     // token2 holds the callback now
// }

} // TEST_SUITE("ListenerToken")

//=============================================================================
// Array typed access and iterator tests
//=============================================================================

TEST_SUITE("Array typed access") {

TEST_CASE("operator() for primitive type") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);

    CHECK(arr(0)() == 10);
    CHECK(arr(1)() == 20);
    CHECK(arr(2)() == 30);
}

TEST_CASE("operator() for struct type") {
    Array<Point> arr;
    Point p1; p1.x = 1.0f; p1.y = 2.0f;
    Point p2; p2.x = 3.0f; p2.y = 4.0f;
    arr.addElement(p1);
    arr.addElement(p2);

    CHECK(arr(0)("x"_fld)() == doctest::Approx(1.0f));
    CHECK(arr(0)("y"_fld)() == doctest::Approx(2.0f));
    CHECK(arr(1)("x"_fld)() == doctest::Approx(3.0f));
    CHECK(arr(1)("y"_fld)() == doctest::Approx(4.0f));
}

TEST_CASE("operator() mutation") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr(0) = 99;
    CHECK(arr(0)() == 99);
}

TEST_CASE("operator() mutation on struct") {
    Array<Point> arr;
    Point p; p.x = 0.0f; p.y = 0.0f;
    arr.addElement(p);

    arr(0)("x"_fld) = 42.0f;
    CHECK(arr(0)("x"_fld)() == doctest::Approx(42.0f));
}

TEST_CASE("const operator()") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);

    Array<int32_t> const& constArr = arr;
    CHECK(constArr(0)() == 10);
    CHECK(constArr(1)() == 20);
}

TEST_CASE("empty") {
    Array<int32_t> arr;
    CHECK(arr.empty());
    arr.addElement(1);
    CHECK_FALSE(arr.empty());
}

TEST_CASE("range-for loop with primitive type") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);

    std::vector<int32_t> values;
    for (auto const& elem : arr) {
        values.push_back(elem());
    }
    CHECK(values.size() == 3);
    CHECK(values[0] == 10);
    CHECK(values[1] == 20);
    CHECK(values[2] == 30);
}

TEST_CASE("range-for loop with struct type") {
    Array<Point> arr;
    Point p1; p1.x = 1.0f; p1.y = 2.0f;
    Point p2; p2.x = 3.0f; p2.y = 4.0f;
    arr.addElement(p1);
    arr.addElement(p2);

    std::vector<float> xValues;
    for (auto const& elem : arr) {
        xValues.push_back(elem("x"_fld)());
    }
    CHECK(xValues.size() == 2);
    CHECK(xValues[0] == doctest::Approx(1.0f));
    CHECK(xValues[1] == doctest::Approx(3.0f));
}

TEST_CASE("mutable range-for loop") {
    Array<int32_t> arr;
    arr.addElement(1);
    arr.addElement(2);
    arr.addElement(3);

    for (auto& elem : arr) {
        elem = elem() * 10;
    }

    CHECK(arr(0)() == 10);
    CHECK(arr(1)() == 20);
    CHECK(arr(2)() == 30);
}

TEST_CASE("const range-for loop") {
    Array<int32_t> arr;
    arr.addElement(5);
    arr.addElement(10);

    Array<int32_t> const& constArr = arr;
    int32_t sum = 0;
    for (auto const& elem : constArr) {
        sum += elem();
    }
    CHECK(sum == 15);
}

TEST_CASE("iterator arithmetic") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);

    auto it = arr.begin();
    CHECK((*it)() == 10);
    ++it;
    CHECK((*it)() == 20);
    --it;
    CHECK((*it)() == 10);

    auto it2 = it + 2;
    CHECK((*it2)() == 30);
    CHECK(it2 - it == 2);

    CHECK(it < it2);
    CHECK(it2 > it);
    CHECK(it <= it);
    CHECK(it >= it);
}

TEST_CASE("iterator operator[]") {
    Array<int32_t> arr;
    arr.addElement(10);
    arr.addElement(20);
    arr.addElement(30);

    auto it = arr.begin();
    CHECK(it[0]() == 10);
    CHECK(it[1]() == 20);
    CHECK(it[2]() == 30);
}

TEST_CASE("cbegin/cend") {
    Array<int32_t> arr;
    arr.addElement(1);
    arr.addElement(2);

    auto it = arr.cbegin();
    CHECK((*it)() == 1);
    ++it;
    CHECK((*it)() == 2);
    ++it;
    CHECK(it == arr.cend());
}

TEST_CASE("empty array iteration") {
    Array<int32_t> arr;
    CHECK(arr.begin() == arr.end());
    int count = 0;
    for (auto const& elem : arr) {
        (void)elem;
        count++;
    }
    CHECK(count == 0);
}

TEST_CASE("iterator with std::distance") {
    Array<int32_t> arr;
    arr.addElement(1);
    arr.addElement(2);
    arr.addElement(3);

    CHECK(std::distance(arr.begin(), arr.end()) == 3);
}

TEST_CASE("operator() triggers listeners") {
    Array<Point> arr;
    Point p; p.x = 0.0f; p.y = 0.0f;
    arr.addElement(p);

    int childCallCount = 0;
    auto token = static_cast<Object&>(arr).addChildListener(
        [&childCallCount](ID const&, Object::Operation, Object const&, Value const&) {
            childCallCount++;
        });

    arr(0)("x"_fld) = 5.0f;
    CHECK(childCallCount == 1);
}

} // TEST_SUITE("Array typed access")

//=============================================================================
// Map typed access and iterator tests
//=============================================================================

TEST_SUITE("Map typed access") {

TEST_CASE("operator() for primitive type") {
    Map<int32_t> map;
    map.addElement("a", 10);
    map.addElement("b", 20);

    CHECK(map("a")() == 10);
    CHECK(map("b")() == 20);
}

TEST_CASE("operator() for struct type") {
    Map<Point> map;
    Point p; p.x = 1.0f; p.y = 2.0f;
    map.addElement("origin", p);

    CHECK(map("origin")("x"_fld)() == doctest::Approx(1.0f));
    CHECK(map("origin")("y"_fld)() == doctest::Approx(2.0f));
}

TEST_CASE("operator() mutation") {
    Map<int32_t> map;
    map.addElement("key", 10);
    map("key") = 99;
    CHECK(map("key")() == 99);
}

TEST_CASE("operator() mutation on struct") {
    Map<Point> map;
    Point p; p.x = 0.0f; p.y = 0.0f;
    map.addElement("pt", p);

    map("pt")("x"_fld) = 42.0f;
    CHECK(map("pt")("x"_fld)() == doctest::Approx(42.0f));
}

TEST_CASE("const operator()") {
    Map<int32_t> map;
    map.addElement("x", 10);

    Map<int32_t> const& constMap = map;
    CHECK(constMap("x")() == 10);
}

TEST_CASE("empty") {
    Map<int32_t> map;
    CHECK(map.empty());
    map.addElement("a", 1);
    CHECK_FALSE(map.empty());
}

TEST_CASE("contains") {
    Map<int32_t> map;
    CHECK_FALSE(map.contains("a"));
    map.addElement("a", 1);
    CHECK(map.contains("a"));
    CHECK_FALSE(map.contains("b"));
}

TEST_CASE("range-for loop with primitive type") {
    Map<int32_t> map;
    map.addElement("a", 10);
    map.addElement("b", 20);
    map.addElement("c", 30);

    std::vector<std::string> keys;
    std::vector<int32_t> values;
    for (auto const& elem : map) {
        keys.push_back(elem.name());
        values.push_back(static_cast<int32_t>(elem));
    }
    CHECK(keys.size() == 3);
    CHECK(keys[0] == "a");
    CHECK(keys[1] == "b");
    CHECK(keys[2] == "c");
    CHECK(values[0] == 10);
    CHECK(values[1] == 20);
    CHECK(values[2] == 30);
}

TEST_CASE("range-for loop with struct type") {
    Map<Point> map;
    Point p1; p1.x = 1.0f; p1.y = 2.0f;
    Point p2; p2.x = 3.0f; p2.y = 4.0f;
    map.addElement("first", p1);
    map.addElement("second", p2);

    std::vector<float> xValues;
    for (auto const& elem : map) {
        xValues.push_back(elem("x"_fld)());
    }
    CHECK(xValues.size() == 2);
    CHECK(xValues[0] == doctest::Approx(1.0f));
    CHECK(xValues[1] == doctest::Approx(3.0f));
}

TEST_CASE("mutable range-for loop") {
    Map<int32_t> map;
    map.addElement("a", 1);
    map.addElement("b", 2);

    for (auto& elem : map) {
        auto val = elem();
        elem = val * 10;
    }

    CHECK(map("a")() == 10);
    CHECK(map("b")() == 20);
}

TEST_CASE("const range-for loop") {
    Map<int32_t> map;
    map.addElement("x", 5);
    map.addElement("y", 10);

    Map<int32_t> const& constMap = map;
    int32_t sum = 0;
    for (auto const& elem : constMap) {
        sum += elem();
    }
    CHECK(sum == 15);
}

TEST_CASE("find existing key") {
    Map<int32_t> map;
    map.addElement("hello", 42);

    auto it = map.find("hello");
    CHECK(it != map.end());
    CHECK((*it)() == 42);
}

TEST_CASE("find nonexistent key") {
    Map<int32_t> map;
    map.addElement("hello", 42);

    auto it = map.find("missing");
    CHECK(it == map.end());
}

TEST_CASE("const find") {
    Map<int32_t> map;
    map.addElement("key", 99);

    Map<int32_t> const& constMap = map;
    auto it = constMap.find("key");
    CHECK(it != constMap.end());
    CHECK((*it)() == 99);

    auto it2 = constMap.find("nope");
    CHECK(it2 == constMap.end());
}

TEST_CASE("empty map iteration") {
    Map<int32_t> map;
    CHECK(map.begin() == map.end());
    int count = 0;
    for (auto const& elem : map) {
        (void)elem;
        count++;
    }
    CHECK(count == 0);
}

TEST_CASE("cbegin/cend") {
    Map<int32_t> map;
    map.addElement("a", 1);
    map.addElement("b", 2);

    auto it = map.cbegin();
    CHECK((*it)() == 1);
    ++it;
    CHECK((*it)() == 2);
    ++it;
    CHECK(it == map.cend());
}

TEST_CASE("operator() triggers listeners") {
    Map<Point> map;
    Point p; p.x = 0.0f; p.y = 0.0f;
    map.addElement("pt", p);

    int childCallCount = 0;
    auto token = static_cast<Object&>(map).addChildListener(
        [&childCallCount](ID const&, Object::Operation, Object const&, Value const&) {
            childCallCount++;
        });

    map("pt")("x"_fld) = 5.0f;
    CHECK(childCallCount == 1);
}

TEST_CASE("find and modify") {
    Map<int32_t> map;
    map.addElement("key", 10);

    auto it = map.find("key");
    CHECK(it != map.end());
    *it = 42;
    CHECK(map("key")() == 42);
}

TEST_CASE("iterator name() returns key") {
    Map<int32_t> map;
    map.addElement("mykey", 10);

    auto it = map.begin();
    CHECK(it->name() == "mykey");
}

TEST_CASE("contains after remove") {
    Map<int32_t> map;
    map.addElement("a", 1);
    CHECK(map.contains("a"));
    map.removeElement("a");
    CHECK_FALSE(map.contains("a"));
}

} // TEST_SUITE("Map typed access")
