#pragma once

namespace dynamic
{

//=============================================================================
// Value implementations
//=============================================================================
template <typename Lambda>
auto Value::visit(this auto&& self, Lambda && lambda) -> decltype(auto)
{
    static constexpr auto kIsConst = std::is_const_v<std::remove_reference_t<decltype(self)>>;

    using NonPrimitiveArgumentTypes = std::tuple<Invalid, Object>;
    using AllArgumentTypes = decltype(std::tuple_cat(std::declval<NonPrimitiveArgumentTypes>(), std::declval<SupportedFundamentalTypes>()));
    using AllArgumentRefs = std::conditional_t<kIsConst, detail::transform_tuple<AllArgumentTypes, detail::add_const_lvalue_ref>::type,
                                                         detail::transform_tuple<AllArgumentTypes, detail::add_lvalue_ref>::type>;
    using SupportedArgumentsByLambda = decltype(detail::filter_tuple<detail::DoesLambdaSupportType<Lambda>::template Predicate>(std::declval<AllArgumentRefs>()));
    static_assert(std::tuple_size_v<SupportedArgumentsByLambda> >= 1, "Your lambda must be callable with at least one of the types in SupportedFundamentalTypes");

    using LambdaReturnTypes = detail::transform_tuple<SupportedArgumentsByLambda, detail::BindFirst<std::invoke_result_t, Lambda>::template Result>::type;
    using LambdaReturnType = detail::apply_tuple<std::common_type, LambdaReturnTypes>::type::type;

    using InvalidRef = std::conditional_t<kIsConst, Invalid const, Invalid>&;
    using TypeErasedObjectRef = std::conditional_t<kIsConst, Object const, Object>&;

    if constexpr (std::is_invocable_v<Lambda, InvalidRef>)
    {
        if (! self.isValid())
            return lambda(static_cast<InvalidRef>(self));
    }

    if constexpr (std::is_invocable_v<Lambda, TypeErasedObjectRef>)
    {
        if (self.isStruct())
            return lambda(static_cast<TypeErasedObjectRef>(self));
    }

    return std::visit([lambda_ = std::move(lambda), &self] <typename T> (std::reference_wrapper<T> v) -> LambdaReturnType
    {
        if constexpr (std::is_invocable_v<Lambda, T&>)
        {
            using Type = std::remove_const_t<T>;
            static constexpr auto kIsPrimitive = (! std::is_same_v<Object, Type>) && (! std::is_same_v<Invalid, Type>);

            if constexpr (kIsPrimitive && (! kIsConst))
            {
                Type copy(v.get());

                if constexpr (! std::is_void_v<std::invoke_result_t<Lambda, T&>>)
                {
                    auto returnValue = lambda_(copy);
                    static_cast<Fundamental<Type>&>(self) = copy;
                    return returnValue;
                }
                else
                {
                    lambda_(copy);
                    static_cast<Fundamental<Type>&>(self) = copy;
                    if constexpr (! std::is_void_v<LambdaReturnType>)
                    {
                        assert(false);
                        // this should never reach this code: crash !
                        return *reinterpret_cast<LambdaReturnType*>(1);
                    }
                    else
                        return;
                }
            }

            return lambda_(v.get());
        }

        if constexpr (! std::is_void_v<LambdaReturnType>)
        {
            assert(false);
            // this should never reach this code: crash !
            return *reinterpret_cast<LambdaReturnType*>(1);
        }
        
    }, self.visit_helper());
}

template <typename T>
constexpr bool Value::isOpaque()
{
    if constexpr (requires(T t) { []<typename U>(Map<U>&){}(t); })
        return false;

    if constexpr (requires(T t) { []<typename U>(Array<U>&){}(t); })
        return false;

    if constexpr (std::is_aggregate_v<T>)
        return detail::num_fields<T>() == 0;

    return true;
}

template <typename Context, typename... Args>
template <std::invocable<std::shared_ptr<Context>&&, Args...> Lambda>
Value::ListenerPair<Context, Args...>::ListenerPair(std::weak_ptr<Context>&& context_, Lambda && lambda_)
    : context(std::move(context_)), lambda(std::move(lambda_))
{}

template <typename Context, typename... Args>
void Value::ListenerPair<Context, Args...>::invoke(Args... args)
{
    if (lambda)
    {
        if (auto ptr = context.lock())
            lambda(std::move(ptr), args...);
    }
}

#if JUCE_SUPPORT
template <typename ComponentType, typename... Args>
template <std::invocable<ComponentType&, Args...> Lambda>
Value::ListenerPairJUCE<ComponentType, Args...>::ListenerPairJUCE(ComponentType* context_, Lambda && lambda_)
    : context(context_), lambda(std::move(lambda_))
{}

template <typename ComponentType, typename... Args>
void Value::ListenerPairJUCE<ComponentType, Args...>::invoke(Args... args)
{
    if (lambda)
    {
        if (auto ptr = context.getComponent())
            lambda(*ptr, args...);
    }
}
#endif


//=============================================================================
// Object implementations
//=============================================================================
template <class Context, std::invocable<std::shared_ptr<Context>&&, ID const&, Object::Operation, Object const&, Value const&> Lambda>
void Object::addChildListener(std::enable_shared_from_this<Context>* context, Lambda && lambda)
{
    childListeners.emplace_back(std::make_unique<ChildListenerPair<Context>>(context->weak_from_this(), std::move(lambda)));
}

template <class Context, std::invocable<std::shared_ptr<Context>&&, ID const&, Object::Operation, Object const&, Value const&> Lambda>
void Object::addChildListener(std::weak_ptr<Context>&& context, Lambda && lambda)
{
    childListeners.emplace_back(std::make_unique<ChildListenerPair<Context>>(std::move(context), std::move(lambda)));
}

#if JUCE_SUPPORT
template <class ComponentType, std::invocable<ComponentType&, ID const&, Object::Operation, Object const&, Value const&> Lambda>
void Object::addChildListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>
{
    childListeners.emplace_back(std::make_unique<ChildListenerPairJUCE<ComponentType>>(context, std::move(lambda)));
}
#endif

auto Object::operator()(this auto&& self, std::string_view fldname)
    -> std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                          Value const&,
                          Value&>
{
    auto flds = self.type_erased_fields();
    auto it = std::find_if(flds.begin(), flds.end(), [&fldname] (auto const& fld) { return fld.get().name() == fldname; });

    if (it == flds.end()) // no field with this name?
        return Value::kInvalid;

    return it->get();
}

auto Object::getchild(this auto& self, ID subid) -> std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, Value const&, Value&>
{
    using ReturnType = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, Value const&, Value&>;
    using StructType = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, Object const, Object>;

    if (subid.empty())
        return static_cast<ReturnType>(self);

    auto next = subid.front();
    subid.erase(subid.begin());

    auto& fld = self.operator()(next);

    if (! subid.empty()) // we didn't use all of the ID? Then call us recursively
    {
        if (! fld.isStruct()) // this is not a struct, but there are still ID elements. Something is wrong!
            return Value::kInvalid;


        // recurse!
        return static_cast<StructType&>(fld).getchild(subid);
    }

    return fld;
}

//=============================================================================
// Fundamental implementations
//=============================================================================

template <typename T>
Fundamental<T>::Fundamental() : underlying() { }

template <typename T>
Fundamental<T>::Fundamental(T underlying_) : underlying(underlying_) {}

template <typename T>
Fundamental<T>::Fundamental(Fundamental const& o) : Value(o), underlying(o.underlying) {}

template <typename T>
Fundamental<T>::Fundamental(Fundamental&& o) : Value(std::move(o)), underlying(std::move(o.underlying)) {}

template <typename T>
Fundamental<T>::~Fundamental()
{
#if JUCE_SUPPORT
    if constexpr (kIsOpaque)
    {
        if (juceValue != nullptr)
            juceValue->parent = nullptr;
    }
#endif
}

template <typename T>
Fundamental<T>& Fundamental<T>::operator=(T const& newValue)
{
    set(newValue);
    return *this;
}

template <typename T>
Fundamental<T>& Fundamental<T>::operator=(Fundamental const& newValue)
{
    set(newValue.underlying);
    return *this;
}

template <typename T>
Fundamental<T>& Fundamental<T>::operator=(Fundamental && newValue)
{
    set(std::move(newValue.underlying));
    return *this;
}

template <typename T>
void Fundamental<T>::set(T const& newValue)
{
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
    {
        if (std::fabs(underlying - newValue) <= std::numeric_limits<T>::epsilon())
            return;
    }
    else
    {
        if (underlying == newValue)
            return;
    }

    if (Value::recursiveListenerDisabler == 0)
        callListeners(newValue);

    ++Value::recursiveListenerDisabler;
    auto raiiDecrementer = cxxutils::callAtEndOfScope(std::false_type(),
                                                      [] (std::false_type)
                                                      {
                                                          --Value::recursiveListenerDisabler;
                                                      });
    underlying = newValue;
}

template <typename T>
void Fundamental<T>::set(T && newValue)
{
    if (Value::recursiveListenerDisabler == 0)
        callListeners(newValue);

    ++Value::recursiveListenerDisabler;
    auto raiiDecrementer = cxxutils::callAtEndOfScope(std::false_type(),
                                                      [] (std::false_type)
                                                      {
                                                          --Value::recursiveListenerDisabler;
                                                      });
    underlying = std::move(newValue);
}

template <typename T>
template <std::invocable<T&> Lambda>
void Fundamental<T>::mutate(Lambda && lambda)
{
    T copy(underlying);
    lambda(copy);

    if (copy != underlying)
    {
        if (Value::recursiveListenerDisabler == 0)
            callListeners(copy);

        ++Value::recursiveListenerDisabler;
        auto raiiDecrementer = cxxutils::callAtEndOfScope(std::false_type(),
                                                        [] (std::false_type)
                                                        {
                                                            --Value::recursiveListenerDisabler;
                                                        });
        underlying = std::move(copy);
    }
}

template <typename T>
template <class Context, std::invocable<std::shared_ptr<Context>&&, Fundamental<T> const&, T const&> Lambda>
void Fundamental<T>::addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda) const
{
    valueListeners.emplace_back(std::make_unique<ValueListenerPair<Context>>(context->weak_from_this(), std::move(lambda)));
}

template <typename T>
bool Fundamental<T>::assign(Value const& other)
{
    if (type() != other.type())
        return false;
    
    auto newValue = static_cast<T>(static_cast<Fundamental<T> const&>(other));
    set(newValue);
    return true;
}

#if JUCE_SUPPORT
template <typename T>
juce::Value Fundamental<T>::getUnderlyingValue() requires Fundamental<T>::kIsOpaque
{
    if (juceValue == nullptr)
        juceValue = new DynamicValueSource(*this);

    return juce::Value(juceValue);
}

template <typename T>
Fundamental<T>::DynamicValueSource::DynamicValueSource(Fundamental<T>& p)
    : parent(&p)
{}

template <typename T>
juce::var Fundamental<T>::DynamicValueSource::getValue() const
{
    if constexpr (kIsOpaque)
    {
        if constexpr (std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::int32_t>) return juce::var(static_cast<int>(parent->underlying));
        if constexpr (std::is_same_v<T, std::int64_t>) return juce::var(static_cast<juce::int64>(parent->underlying));
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) return juce::var(static_cast<double>(parent->underlying));
        if constexpr (std::is_same_v<T, bool>) return juce::var(parent->underlying);
        if constexpr (std::is_same_v<T, std::string>) return juce::var(juce::String(parent->underlying));
        if constexpr (std::is_same_v<T, ID>) return juce::var(juce::String(parent->underlying.toString()));
    }

    return {};
}

template <typename T>
void Fundamental<T>::DynamicValueSource::setValue(juce::var const& newVar)
{
    if constexpr (kIsOpaque)
    {
        if constexpr (std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::int32_t>) parent->set(static_cast<T>(static_cast<int>(newVar)));
        else if constexpr (std::is_same_v<T, std::int64_t>) parent->set(static_cast<std::int64_t>(static_cast<juce::int64>(newVar)));
        else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) parent->set(static_cast<T>(static_cast<double>(newVar)));
        else if constexpr (std::is_same_v<T, bool>) parent->set(static_cast<bool>(newVar));
        else if constexpr (std::is_same_v<T, std::string>) parent->set(static_cast<juce::String>(newVar).toStdString());
        else if constexpr (std::is_same_v<T, ID>) parent->set(ID::fromString(static_cast<juce::String>(newVar).toStdString()));
    }
}
#endif

template <typename T>
void Fundamental<T>::callListeners(T newValue)
{
    valueListeners.erase(std::remove_if(valueListeners.begin(), valueListeners.end(), [] (auto& ptr) { return ptr->expired(); }), valueListeners.end());

    for (auto& listener : valueListeners)
        listener->invoke(*this, newValue);

    if (Base::parent != nullptr)
    {
        std::conditional_t<kIsOpaque, Fundamental<T>, Record<T>> newValueTypeErased(newValue);
        Base::parent->callChildListeners(std::vector<std::string>(1, std::string(this->name())), Object::Operation::modify, *Base::parent, newValueTypeErased);
    }

#if JUCE_SUPPORT
    if constexpr (kIsOpaque)
    {
        if (juceValue != nullptr)
            juceValue->sendChangeMessage(false);
    }
#endif
}

template <typename T>
typename Value::TypesVariant Fundamental<T>::visit_helper()
{
    if constexpr (kIsOpaque)
        return {std::reference_wrapper<T>(underlying)};
    return Base::visit_helper();
}

template <typename T>
typename Value::ConstTypesVariant Fundamental<T>::visit_helper() const
{
    if constexpr (kIsOpaque)
        return {std::reference_wrapper<T const>(underlying)};
    return Base::visit_helper();
}

//=============================================================================
// operator""_fld implementation
//=============================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#ifdef __clang__
#    pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
#endif
template <typename T, T... chars>
constexpr CompileTimeString<fixstr::fixed_string<sizeof...(chars)>({chars...})> operator""_fld()
{
    return { };
}
#pragma GCC diagnostic pop

//=============================================================================
// Record implementations
//=============================================================================

template <typename T>
auto Record<T>::fields_with(T& u)
{
    return detail::filter_tuple<detail::is_field>(boost::pfr::structure_tie(u));
}

template <typename T>
auto Record<T>::fields_with(T const& u)
{
    return detail::filter_tuple<detail::is_field>(boost::pfr::structure_tie(u));
}

template <typename T>
Record<T>::Record() : Fundamental<T>()
{
    init();
}

template <typename T>
Record<T>::Record(T const& underlying_) : Fundamental<T>(underlying_)
{
    init();
}

template <typename T>
Record<T>::Record(Record const& o) : Fundamental<T>(o.underlying)
{
    init();
}

template <typename T>
Record<T>::Record(Record&& o) : Fundamental<T>(std::move(o.underlying))
{
    init();
}

template <typename T>
Record<T>& Record<T>::operator=(Record const& o)
{
    Fundamental<T>::operator=(o);
    return *this;
}

template <typename T>
Record<T>& Record<T>::operator=(Record&& o)
{
    Fundamental<T>::operator=(std::move(o));
    return *this;
}

template <typename T>
template <fixstr::fixed_string FieldName>
auto&& Record<T>::operator()(this auto& self, CompileTimeString<FieldName>)
{
    return std::apply([] <typename... Types> (Types &&... fields) -> auto&&
    {
        return detail::FindFieldHelper<FieldName>::eval(std::forward<Types>(fields)...);
    }, self.fields());
}

template <typename T>
auto Record<T>::fields(this auto& self)
{
    return fields_with(self.underlying);
}

template <typename T>
std::vector<std::reference_wrapper<Value const>> Record<T>::type_erased_fields() const
{
    return type_erased_fields_internal();
}

template <typename T>
std::vector<std::reference_wrapper<Value>> Record<T>::type_erased_fields()
{
    return type_erased_fields_internal();
}

template <typename T>
template <typename Lambda>
void Record<T>::visitFields(this auto& self, Lambda && lambda) noexcept
{
    std::apply([lambda_ = std::move(lambda)] <typename... Types> (Types &&... fields)
    {
        (lambda_(fields.name(), fields), ...);
    }, self.fields());
}

template <typename T>
template <typename Lambda>
auto Record<T>::visitField(this auto& self, std::string_view const& str, Lambda && lambda)
{
    static constexpr auto kIsConst = std::is_const_v<std::remove_reference_t<decltype(self)>>;
    using FirstTupleElement = std::tuple_element_t<0, FieldsAsTuple>;
    using LambdaReturnType = std::invoke_result_t<Lambda, std::conditional_t<kIsConst, FirstTupleElement const&, FirstTupleElement&>>;
    static constexpr auto kLambdaReturnsVoid = std::is_same_v<LambdaReturnType, void>;

    using ReturnType = std::conditional_t<kLambdaReturnsVoid, bool, std::optional<LambdaReturnType>>;

    ReturnType returnValue = {};

    std::apply([lambda_ = std::move(lambda), &str, &returnValue] <typename... Types> (Types &&... fields)
    {
        (std::invoke([&lambda_, &str, &returnValue] (auto& fld)
        {
            if ((! returnValue) && fld.name() == str)
            {
                if constexpr (kLambdaReturnsVoid)
                {
                    returnValue = true;
                    lambda_(fld);
                }
                else
                {
                    returnValue = lambda_(fld);
                }
            }
        }, fields), ...);
    }, self.fields());

    return returnValue;
}

template <typename T>
auto Record<T>::type_erased_fields_internal(this auto& self)
{
    static constexpr auto kIsConst = std::is_const_v<std::remove_reference_t<decltype(self)>>;
    using ElementType = std::conditional_t<kIsConst, Value const, Value>;
    std::vector<std::reference_wrapper<ElementType>> returnValue;

    std::apply([&returnValue] <typename... Fields> (Fields && ...flds) mutable
    {
         (returnValue.emplace_back(static_cast<ElementType&>(flds)), ...);
    }, self.fields());

    return returnValue;
}

template <typename T>
void Record<T>::init()
{
    std::apply([this] (auto &&... flds)
    {
        (std::invoke([this, &flds] { flds.parent = this; }), ...);
    }, fields());
}

// overridden base methods
template <typename T>
bool Record<T>::assignChild(std::string const& name, Value const& newValue)
{
    auto result = visitField(name, [&newValue] (auto& fld) { return fld.assign(newValue); });

    if (! result.has_value())
        return false;
    
    return *result;
}

template <typename T>
bool Record<T>::removeChild(std::string const&)
{
    // you can never remove a field from a struct
    return false;
}

//=============================================================================
// Array implementations
//=============================================================================

template <typename T>
Array<T>::Array(Array const& o) : Object(o), elements()
{
    // Copy elements but not arrayListeners
    for (auto const& elem : o.elements)
        elements.emplace_back(*this, elem());
}

template <typename T>
std::vector<std::reference_wrapper<Value const>> Array<T>::type_erased_fields() const
{
    return type_erased_fields_internal();
}

template <typename T>
std::vector<std::reference_wrapper<Value>> Array<T>::type_erased_fields()
{
    return type_erased_fields_internal();
}

template <typename T>
void Array<T>::addElement(T const& element)
{
    callListeners(Operation::add, element, elements.size());
    elements.emplace_back(*this, element);
}

template <typename T>
void Array<T>::addElement(T&& element)
{
    callListeners(Operation::add, element, elements.size());
    elements.emplace_back(*this, std::move(element));
}

template <typename T>
void Array<T>::removeElement(std::size_t idx)
{
    assert(idx < elements.size());
    callListeners(Operation::remove, elements[idx], elements.size());
    elements.erase(elements.begin() + static_cast<int>(idx));
}

template <typename T>
template <class Context, std::invocable<std::shared_ptr<Context>&&, Object::Operation, Array<T> const&, T const&, std::size_t> Lambda>
void Array<T>::addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda)
{
    arrayListeners.emplace_back(std::make_unique<ArrayListenerPair<Context>>(context->weak_from_this(), std::move(lambda)));
}

template <typename T>
template <class Context, std::invocable<std::shared_ptr<Context>&&, Object::Operation, Array<T> const&, T const&, std::size_t> Lambda>
void Array<T>::addListener(std::weak_ptr<Context> && context, Lambda && lambda)
{
    arrayListeners.emplace_back(std::make_unique<ArrayListenerPair<Context>>(std::move(context), std::move(lambda)));
}

#if JUCE_SUPPORT
template <typename T>
template <class ComponentType, std::invocable<ComponentType&, Object::Operation, Array<T> const&, T const&, std::size_t> Lambda>
void Array<T>::addListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>
{
    arrayListeners.emplace_back(std::make_unique<ArrayListenerPairJUCE<ComponentType>>(context, std::move(lambda)));
}
#endif

template <typename T>
auto Array<T>::type_erased_fields_internal(this auto && self)
{
    static constexpr auto kIsConst = std::is_const_v<std::remove_reference_t<decltype(self)>>;
    using ElementType = std::conditional_t<kIsConst, Value const, Value>;
    using ReturnType = std::vector<std::reference_wrapper<ElementType>>;

    ReturnType returnValue;
    for (auto& element : self.elements)
        returnValue.emplace_back(element);

    return returnValue;
}

template <typename T>
Array<T>::Element::Element(Array& container_)
{
    init(container_);
}

template <typename T>
Array<T>::Element::Element(Element const& o) : Base(o)
{
    init(static_cast<Array<T>&>(*o.parent));
}

template <typename T>
Array<T>::Element::Element(Array& container_, T const& underlying_) : Base(underlying_)
{
    init(container_);
}

template <typename T>
Array<T>::Element::Element(Array& container_, T && underlying_) : Base(std::move(underlying_))
{
    init(container_);
}

template <typename T>
typename Array<T>::Element& Array<T>::Element::operator=(Element const& o)
{
    Base::operator=(o);
    return *this;
}

template <typename T>
typename Array<T>::Element& Array<T>::Element::operator=(T const& t)
{
    Base::operator=(t);
    return *this;
}

template <typename T>
typename Array<T>::Element& Array<T>::Element::operator=(T && t)
{
    Base::operator=(std::move(t));
    return *this;
}

template <typename T>
std::string Array<T>::Element::name() const
{
    auto const idx = this - static_cast<Array*>(Base::parent)->elements.data();
    return std::to_string(idx);
}

template <typename T>
void Array<T>::Element::init(Array& container_)
{
    Base::parent = &container_;
}

template <typename T>
void Array<T>::callListeners(Operation op, T const& newValue, std::size_t idx) const
{
    arrayListeners.erase(std::remove_if(arrayListeners.begin(), arrayListeners.end(), [] (auto& ptr) { return ptr->expired(); }), arrayListeners.end());

    for (auto& listener : arrayListeners)
        listener->invoke(op, *this, newValue, idx);

    {
        std::conditional_t<Fundamental<T>::kIsOpaque, Fundamental<T>, Record<T>> newValueTypeErased(newValue);
        callChildListeners(std::vector<std::string>(1, std::to_string(idx)), op, *this, newValueTypeErased);
    }
}

// overridden base methods
template <typename T>
bool Array<T>::assign(Value const& unsafeOther)
{
    if (type() != unsafeOther.type())
        return false;
    
    auto const& other = static_cast<Array const&>(unsafeOther);

    while (elements.size())
        removeElement(elements.size() - 1);

    for (auto const& otherElement : other.elements)
        addElement(static_cast<T>(otherElement));

    return true;
}

template <typename T>
bool Array<T>::assignChild(std::string const& name, Value const& newValue)
{
    if (newValue.type() != typeid(T))
        return false;

    int index = 0;
    try
    {
        index = std::stoi(name);
    }
    catch (...)
    {
        return false;
    }

    if (index < 0)
        return false;

    if (static_cast<std::size_t>(index) < elements.size())
        return elements[static_cast<std::size_t>(index)].assign(newValue);

    while (elements.size() < static_cast<std::size_t>(index))
        addElement({});

    addElement(static_cast<T>(static_cast<Fundamental<T> const&>(newValue)));
    return true;
}

template <typename T>
bool Array<T>::removeChild(std::string const& name)
{
    int index = 0;
    try
    {
        index = std::stoi(name);
    }
    catch (...)
    {
        return false;
    }

    if (index < 0 || static_cast<std::size_t>(index) >= elements.size())
        return false;
    
    removeElement(static_cast<std::size_t>(index));
    return true;
}
} //namespace dynamic

template <typename T>
bool operator==(dynamic::Array<T> const& aarray, dynamic::Array<T> const& barray)
{
    auto const n = aarray.elements.size();

    if (n != barray.elements.size())
        return false;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto const& a = aarray.elements[i];
        auto const& b = barray.elements[i];

        if (a != b)
            return false;
    }

    return true;
}

namespace dynamic
{

//=============================================================================
// Map implementations
//=============================================================================

template <typename T>
Map<T>::Map(Map const& o) : Object(o), elements()
{
    // Copy elements but not mapListeners
    for (auto const& elem : o.elements)
        elements.emplace_back(elem.fieldName, *this, elem());
}

template <typename T>
std::vector<std::reference_wrapper<Value const>> Map<T>::type_erased_fields() const
{
    return type_erased_fields_internal();
}

template <typename T>
std::vector<std::reference_wrapper<Value>> Map<T>::type_erased_fields()
{
    return type_erased_fields_internal();
}

template <typename T>
void Map<T>::addElement(std::string_view key, T const& element)
{
    if (auto it = std::find_if(elements.begin(), elements.end(), [key] (Element const& elem) { return elem.fieldName == key; }); it != elements.end())
    {
        *it = element;
        return;
    }

    callListeners(Operation::add, element, key);
    elements.emplace_back(key, *this, element);
}

template <typename T>
void Map<T>::addElement(std::string_view key, T&& element)
{
    if (auto it = std::find_if(elements.begin(), elements.end(), [key] (Element const& elem) { return elem.fieldName == key; }); it != elements.end())
    {
        *it = std::move(element);
        return;
    }

    callListeners(Operation::add, element, key);
    elements.emplace_back(key, *this, std::move(element));
}

template <typename T>
bool Map<T>::removeElement(std::string_view key)
{
    auto it = std::find_if(elements.begin(), elements.end(), [key] (Element const& elem) { return elem.fieldName == key; });

    if (it == elements.end())
        return false;

    callListeners(Operation::remove, *it, key);
    elements.erase(it);
    return true;
}

template <typename T>
template <class Context, std::invocable<std::shared_ptr<Context>&&, Object::Operation, Map<T> const&, T const&, std::string_view> Lambda>
void Map<T>::addListener(std::enable_shared_from_this<Context>* context, Lambda && lambda)
{
    mapListeners.emplace_back(std::make_unique<MapListenerPair<Context>>(context->weak_from_this(), std::move(lambda)));
}

template <typename T>
template <class Context, std::invocable<std::shared_ptr<Context>&&, Object::Operation, Map<T> const&, T const&, std::string_view> Lambda>
void Map<T>::addListener(std::weak_ptr<Context> && context, Lambda && lambda)
{
    mapListeners.emplace_back(std::make_unique<MapListenerPair<Context>>(std::move(context), std::move(lambda)));
}

#if JUCE_SUPPORT
template <typename T>
template <class ComponentType, std::invocable<ComponentType&, Object::Operation, Map<T> const&, T const&, std::string_view> Lambda>
void Map<T>::addListener(ComponentType* context, Lambda && lambda) requires std::is_base_of_v<juce::Component, ComponentType>
{
    mapListeners.emplace_back(std::make_unique<MapListenerPairJUCE<ComponentType>>(context, std::move(lambda)));
}
#endif

template <typename T>
auto Map<T>::type_erased_fields_internal(this auto && self)
{
    static constexpr auto kIsConst = std::is_const_v<std::remove_reference_t<decltype(self)>>;
    using ElementType = std::conditional_t<kIsConst, Value const, Value>;
    using ReturnType = std::vector<std::reference_wrapper<ElementType>>;

    ReturnType returnValue;
    for (auto& element : self.elements)
        returnValue.emplace_back(element);

    return returnValue;
}

template <typename T>
Map<T>::Element::Element(std::string_view fieldName_, Map& container_) : fieldName(fieldName_)
{
    init(container_);
}

template <typename T>
Map<T>::Element::Element(Element const& o) : Base(o),  fieldName(o.fieldName)
{
    init(static_cast<Map<T>&>(*o.parent));
}

template <typename T>
Map<T>::Element::Element(std::string_view fieldName_, Map& container_, T const& underlying_) : Base(underlying_), fieldName(fieldName_)
{
    init(container_);
}

template <typename T>
Map<T>::Element::Element(std::string_view fieldName_, Map& container_, T && underlying_) : Base(std::move(underlying_)), fieldName(fieldName_)
{
    init(container_);
}

template <typename T>
typename Map<T>::Element& Map<T>::Element::operator=(Element const& o)
{
    Base::operator=(o);
    fieldName = o.fieldName;
    return *this;
}

template <typename T>
typename Map<T>::Element& Map<T>::Element::operator=(T const& t)
{
    Base::operator=(t);
    return *this;
}

template <typename T>
typename Map<T>::Element& Map<T>::Element::operator=(T && t)
{
    Base::operator=(std::move(t));
    return *this;
}

template <typename T>
std::string Map<T>::Element::name() const
{
    return fieldName;
}

template <typename T>
void Map<T>::Element::init(Map& container_)
{
    Base::parent = &container_;
}

template <typename T>
void Map<T>::callListeners(Operation op, T const& newValue, std::string_view key) const
{
    mapListeners.erase(std::remove_if(mapListeners.begin(), mapListeners.end(), [] (auto& ptr) { return ptr->expired(); }), mapListeners.end());

    for (auto& listener : mapListeners)
        listener->invoke(op, *this, newValue, key);

    {
        std::conditional_t<Fundamental<T>::kIsOpaque, Fundamental<T>, Record<T>> newValueTypeErased(newValue);
        callChildListeners(std::vector<std::string>(1, std::string(key)), op, *this, newValueTypeErased);
    }
}

// overridden base methods
template <typename T>
bool Map<T>::assign(Value const& unsafeOther)
{
    if (type() != unsafeOther.type())
        return false;
    
    auto const& other = static_cast<Map const&>(unsafeOther);

    while (elements.size())
        removeElement(elements[elements.size() - 1].fieldName);

    for (auto const& otherElement : other.elements)
        addElement(otherElement.fieldName, static_cast<T>(otherElement));

    return true;
}

template <typename T>
bool Map<T>::assignChild(std::string const& name, Value const& newValue)
{
    if (newValue.type() != typeid(T))
        return false;

    auto it = std::find_if(elements.begin(), elements.end(), [name] (Element const& elem) { return elem.fieldName == name; });

    if (it != elements.end())
        return it->assign(newValue);
    
    addElement(name, static_cast<T>(static_cast<Fundamental<T> const&>(newValue)));
    return true;
}

template <typename T>
bool Map<T>::removeChild(std::string const& name)
{
    auto it = std::find_if(elements.begin(), elements.end(), [name] (Element const& elem) { return elem.fieldName == name; });
    if (it == elements.end())
        return false;

    removeElement(name);
    return true;
}
} //namespace dynamic

template <typename T>
bool operator==(dynamic::Map<T> const& amap, dynamic::Map<T> const& bmap)
{
    auto const n = amap.elements.size();

    if (n != bmap.elements.size())
        return false;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto const& a = amap.elements[i];
        auto const& b = bmap.elements[i];

        if (a.fieldName != b.fieldName)
            return false;

        if (a != b)
            return false;
    }

    return true;
}

namespace dynamic
{

//=============================================================================
// Field implementations
//=============================================================================

template <typename T, fixstr::fixed_string Name>
Field<T, Name>& Field<T, Name>::operator=(T const& t)
{
    Base::operator=(t);
    return *this;
}

template <typename T, fixstr::fixed_string Name>
Field<T, Name>& Field<T, Name>::operator=(T && t)
{
    Base::operator=(std::move(t));
    return *this;
}

template <typename T, fixstr::fixed_string Name>
std::string Field<T, Name>::name() const
{
    return std::string(std::string_view(Name));
}

//=============================================================================
// Stream operators implementations
//=============================================================================

inline std::ostream& operator<<(std::ostream& o, Value const& x)
{
    x.visit([&o] (auto const& underlying) { o << underlying; });
    return o;
}

inline std::ostream& operator<<(std::ostream& o, Object const& x)
{
    o << "{ ";
    auto first = true;
    auto const& fields = x.type_erased_fields();

    for (auto const& fld : fields)
    {
        if (! std::exchange(first, false))
            o << ", ";

        o << "." << fld.get().name() << " = " << fld.get();
    }

    o << " }";
    return o;
}

inline std::ostream& operator<<(std::ostream& o, Invalid const&)
{
    return o;
}

} // namespace dynamic

//=============================================================================
// std::formatter implementations
//=============================================================================

inline auto std::formatter<dynamic::Object>::format(dynamic::Object const& v, format_context& ctx) const
{
    std::ostringstream ss;
    ss << v;
    return std::formatter<string>::format(ss.str(), ctx);
}

template<typename CharT>
template<class FmtContext>
FmtContext::iterator std::formatter<dynamic::Value, CharT>::format(dynamic::Value const& v, FmtContext& ctx) const
{
    return v.visit([&ctx] (auto const& underlying)
    {
        return std::formatter<std::remove_cvref_t<decltype(underlying)>, CharT>{}.format(underlying, ctx);
    });
}
