#include "dynamic.hpp"

namespace dynamic
{
//=============================================================================
// ID implementations
//=============================================================================

ID& ID::operator=(ID const& o)
{
    std::vector<std::string>::operator=(o);
    return *this;
}

ID& ID::operator=(ID && o)
{
    std::vector<std::string>::operator=(std::move(o));
    return *this;
}

std::string ID::toString() const
{
    std::ostringstream ss;
    std::copy(begin(), end(), std::ostream_iterator<std::string>(ss, "/"));
    return ss.str().substr(0, ss.str().size() - 1);
}

ID ID::fromString(std::string const& path)
{
    ID elements;
    std::istringstream ss(path);
    for (std::string line; std::getline(ss, line, '/');)
        elements.emplace_back(std::move(line));

    return elements;
}    

//=============================================================================
// Value implementations
//=============================================================================
thread_local std::size_t Value::recursiveListenerDisabler = 0;

Value::Value(Value const&) : parent(nullptr) {}

Value::Value(Value&& o) : parent(nullptr)
{
    std::swap(parent, o.parent);
}

typename Value::TypesVariant Value::visit_helper()
{
    assert(false); /* crash! */
    return *reinterpret_cast<TypesVariant*>(1);
}

typename Value::ConstTypesVariant Value::visit_helper() const
{
    assert(false); /* crash! */
    return *reinterpret_cast<ConstTypesVariant*>(1);
}

Value& Value::operator=(Value const& other)
{
    auto success = assign(other);
    assert(success);
    (void)success;

    return *this;
}

// Initialize the global invalid value singleton
Invalid& Value::kInvalid = std::invoke([] () -> auto&&
{
    static constexpr Invalid invld;
    return const_cast<Invalid&>(invld);
});

//=============================================================================
// Object implementations
//=============================================================================
Object::Object(Object const& o) : Value(o) {}
Object::Object(Object&& o) : Value(std::move(o)), childListeners(std::move(o.childListeners)) {}

void Object::callChildListeners(ID const& id, Operation op, Object const& parentOfChangedValue, Value const& newValue) const
{
    childListeners.erase(std::remove_if(childListeners.begin(), childListeners.end(), [] (auto& ptr) { return ptr->expired(); }), childListeners.end());

    for (auto& listener : childListeners)
        listener->invoke(id, op, parentOfChangedValue, newValue);

    if (parent != nullptr)
    {
        auto newID = id;
        newID.insert(newID.begin(), std::string(this->name()));
        parent->callChildListeners(newID, op, parentOfChangedValue, newValue);
    }
}

Object& Object::operator=(Object const& o)
{
    Value::operator=(static_cast<Value const&>(o));
    return *this;
}

Object& Object::operator=(Object&& o)
{
    Value::operator=(std::move(o));
    return *this;
}
}
