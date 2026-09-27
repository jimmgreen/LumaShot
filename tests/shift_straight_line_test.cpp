#include "model/document.h"
#include <iostream>
#include <cmath>
#include <cassert>

using namespace lumashot;

static void Expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        std::exit(1);
    }
    std::cout << "PASS: " << msg << std::endl;
}

int main() {
    const Point origin{100.0f, 100.0f};

    // 1. Horizontal snap (dx > dy)
    {
        Point p = ConstrainStraightLine(origin, {350.0f, 105.0f});
        Expect(std::abs(p.y - origin.y) < 1e-3f, "Snaps to horizontal line (exact Y)");
        Expect(p.x > 349.0f, "Preserves distance horizontally");
    }

    // 2. Vertical snap (dy > dx)
    {
        Point p = ConstrainStraightLine(origin, {95.0f, 400.0f});
        Expect(std::abs(p.x - origin.x) < 1e-3f, "Snaps to vertical line (exact X)");
        Expect(p.y > 399.0f, "Preserves distance vertically");
    }

    // 3. 45-degree diagonal snap
    {
        Point p = ConstrainStraightLine(origin, {205.0f, 195.0f});
        const float dx = std::abs(p.x - origin.x);
        const float dy = std::abs(p.y - origin.y);
        Expect(std::abs(dx - dy) < 1e-3f, "Snaps to 45 degree diagonal (|dx| == |dy|)");
    }

    // 4. Negative angles (e.g. up-left diagonal)
    {
        Point p = ConstrainStraightLine(origin, {10.0f, 15.0f});
        const float dx = std::abs(p.x - origin.x);
        const float dy = std::abs(p.y - origin.y);
        Expect(std::abs(dx - dy) < 1e-3f, "Snaps to -135 degree diagonal (|dx| == |dy|)");
        Expect(p.x < origin.x && p.y < origin.y, "Correct quadrant direction");
    }

    // 5. Test Pen and Highlighter straight line points in Mark
    {
        Mark pen;
        pen.tool = Tool::Pen;
        pen.pen_mode = PenMode::Normal;
        pen.a = origin;
        Point end = ConstrainStraightLine(origin, {300.0f, 102.0f});
        pen.b = end;
        pen.points = {origin, end};

        Expect(pen.points.size() == 2, "Straight pen stroke contains 2 endpoints");
        Expect(pen.points[0] == origin, "Origin matches");
        Expect(pen.points[1] == end, "Constrained endpoint matches");

        Mark highlighter;
        highlighter.tool = Tool::Pen;
        highlighter.pen_mode = PenMode::Highlighter;
        highlighter.a = origin;
        Point h_end = ConstrainStraightLine(origin, {101.0f, 500.0f});
        highlighter.b = h_end;
        highlighter.points = {origin, h_end};

        Expect(highlighter.points.size() == 2, "Straight highlighter stroke contains 2 endpoints");
        Expect(highlighter.points[1] == h_end, "Highlighter constrained endpoint matches");
    }

    std::cout << "ALL SHIFT STRAIGHT LINE TESTS PASSED" << std::endl;
    return 0;
}
