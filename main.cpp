#include <iostream>
#include <memory>
#include "dynamic.hpp"

// Example usage
using namespace dynamic;

struct Point
{
    Field<float, "x">      x;
    Field<float, "y">      y;

    friend bool operator==(Point const& a, Point const& b) noexcept
    {
        return a.x() == b.x() && a.y() == b.y();
    }
};



struct Line
{
    Field<Point, "start">       start;
    Field<Point, "finish">      finish;

    friend bool operator==(Line const& a, Line const& b) noexcept
    {
        return a.start() == b.start() && a.finish() == b.finish();
    }
};

struct State
{
    Field<Line,         "line"  > line;
    Field<Array<Point>, "points"> points;

    Field<Map<Line>, "paths"> paths;

    friend bool operator==(State const& a, State const& b) noexcept
    {
        return a.line() == b.line() && a.points == b.points && a.paths == b.paths;
    }
};

struct Application : std::enable_shared_from_this<Application>
{
    Application() {}

    void run()
    {
        Point new_pt;

        new_pt.x = 1.1f;
        new_pt.y = 2.2f;

        Record<State> state;

        state("points"_fld).addListener(this, [] (Object::Operation, Array<Point> const& array, Point const& newValue, std::size_t idx)
        {
            std::cout << "Fundamental { .x = " << newValue.x << ", .y = " << newValue.y << " } was added to array " << array << " at index " << idx << std::endl;
        });

        state.addChildListener(this, [] (ID const& id, Object::Operation op,  Object const& parent, Value const & value)
        {
            switch (op)
            {
            case Object::Operation::add:
            {
                std::cout << "Fundamental \"" << value << "\" was added to \"" << parent << "\" at ID \"" << id.toString() << "\"" << std::endl;
                break;
            }
            case Object::Operation::remove:
            {
                std::cout << "Fundamental \"" << value << "\" was removed from \"" << parent << "\" at ID \"" << id.toString() << "\"" << std::endl;
                break;
            }
            case Object::Operation::modify:
            {
                std::cout << "Fundamental at \"" << id.toString() << "\" changed to \"" << value << "\"" << std::endl;
                break;
            }
            default: break;
            }

        });

        Record<Line> newLine;

        state("paths"_fld).addElement("fabian", newLine);
        state("paths"_fld).addElement("chris", newLine);
        state("paths"_fld).assignChild("ces", newLine);

        state("points"_fld).addElement(new_pt);
        state("points"_fld).addElement(new_pt);
        static_cast<Object&>(state("points"_fld)("0"))("x").visit([] (float& f)
        {
            f = 3.3f;
        });

        static_cast<Object&>(state("points"_fld)("0")).assignChild("y", Fundamental<float>(7.5f));

        for (auto const& key : Record<Point>::kFieldNames)
            std::cout << key << std::endl;

        auto& line = state("line"_fld);
        auto const& qine = line;

        line("start"_fld)("x"_fld) = 3.4f;
        line("start"_fld) = new_pt;

        std::cout << line("start"_fld)("x"_fld)() << std::endl;
        std::cout << qine("start"_fld)("x"_fld)() << std::endl;

        Value & xpoint = line("start"_fld)("x"_fld);

        xpoint.visit([] (float& f)
        {
           f = 0.4f;
        });

        std::cout << "after:" << std::endl;

        std::cout << std::format("{}", xpoint) << std::endl;

        //line.get().start.get().x = 3.4f;
    }
};

void metaTypeExamples()
{
    std::cout << "\n=== MetaType Examples ===\n\n";

    // 1. Inspect a Record type without creating an instance
    std::cout << "--- Record<Point> fields (no instance needed) ---\n";
    auto const& pointMeta = Record<Point>::meta();
    for (auto const& field : pointMeta.fields())
        std::cout << "  " << field.fieldname << " (opaque: " << field.metaType().isOpaque() << ")\n";

    // 2. Inspect nested Record types recursively
    std::cout << "\n--- Record<Line> fields (nested introspection) ---\n";
    auto const& lineMeta = Record<Line>::meta();
    for (auto const& field : lineMeta.fields())
    {
        auto const& fieldMeta = field.metaType();
        std::cout << "  " << field.fieldname;

        if (fieldMeta.isRecord())
        {
            std::cout << " (record with fields:";
            for (auto const& nested : fieldMeta.fields())
                std::cout << " " << nested.fieldname;
            std::cout << ")\n";
        }
        else
        {
            std::cout << " (opaque)\n";
        }
    }

    // 3. Full State introspection including containers
    std::cout << "\n--- Record<State> fields ---\n";
    auto const& stateMeta = Record<State>::meta();
    for (auto const& field : stateMeta.fields())
    {
        auto const& fieldMeta = field.metaType();
        std::cout << "  " << field.fieldname;

        if (fieldMeta.isRecord())
            std::cout << " [record, " << fieldMeta.fields().size() << " fields]";
        else if (fieldMeta.isArray())
            std::cout << " [array, element is " << (fieldMeta.elementMetaType()->isRecord() ? "record" : "opaque") << "]";
        else if (fieldMeta.isMap())
            std::cout << " [map, element is " << (fieldMeta.elementMetaType()->isRecord() ? "record" : "opaque") << "]";
        else
            std::cout << " [opaque]";

        std::cout << "\n";
    }

    // 4. Construct instances via MetaType factory
    std::cout << "\n--- Construct via MetaType ---\n";
    auto pointInstance = pointMeta.construct();
    std::cout << "  Constructed Record<Point>: " << static_cast<Object&>(*pointInstance) << "\n";

    auto arrayInstance = Array<Point>::meta().construct();
    std::cout << "  Constructed Array<Point>, isStruct: " << arrayInstance->isStruct() << "\n";

    // 5. metaType() on an existing Value reference (runtime introspection)
    std::cout << "\n--- Runtime metaType() from Value& ---\n";
    Record<State> state;
    Value& lineField = state("line"_fld);
    auto const& runtimeMeta = lineField.metaType();
    std::cout << "  state.line has " << runtimeMeta.fields().size() << " fields:";
    for (auto const& field : runtimeMeta.fields())
        std::cout << " " << field.fieldname;
    std::cout << "\n";

    // 6. Use metaTypeOf<T>() free function
    std::cout << "\n--- metaTypeOf<T>() ---\n";
    std::cout << "  metaTypeOf<float>().isOpaque() = " << metaTypeOf<float>().isOpaque() << "\n";
    std::cout << "  metaTypeOf<Point>().isRecord() = " << metaTypeOf<Point>().isRecord() << "\n";
    std::cout << "  metaTypeOf<Point>().fields().size() = " << metaTypeOf<Point>().fields().size() << "\n";
}

int main()
{
    auto app = std::make_shared<Application>();
    app->run();

    metaTypeExamples();

    return 0;
}
