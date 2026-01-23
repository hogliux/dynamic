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

        state().points.addListener(this, [] (std::shared_ptr<Application> &&, Object::Operation, Array<Point> const& array, Point const& newValue, std::size_t idx)
        {
            std::cout << "Fundamental { .x = " << newValue.x << ", .y = " << newValue.y << " } will be added to array " << array << " at index " << idx << std::endl;
        });

        state.addChildListener(this, [] (std::shared_ptr<Application> &&, ID const& id, Object::Operation op,  Object const& parent, Value const & value)
        {
            switch (op)
            {
            case Object::Operation::add:
            {
                std::cout << "Fundamental \"" << value << "\" will be added to array \"" << parent << "\" at ID \"" << id.toString() << "\"" << std::endl;
                break;
            }
            case Object::Operation::remove:
            {
                std::cout << "Fundamental \"" << value << "\" will be removed from array \"" << parent << "\" at ID \"" << id.toString() << "\"" << std::endl;
                break;
            }
            case Object::Operation::modify:
            {
                auto const& oldValue = parent(id.back());
                std::cout << "Fundamental at \"" << id.toString() << "\" changed from \"" << oldValue << "\" to \"" << value << "\"" << std::endl;
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

int main()
{
    auto app = std::make_shared<Application>();
    app->run();

    return 0;
}
