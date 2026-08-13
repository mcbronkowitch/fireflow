#pragma once

#include <cstddef>

namespace spkyvcv::pad_geometry {

struct Point {
    float x;
    float y;
};

inline Point catmullRom(Point p0, Point p1, Point p2, Point p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {
        0.5f * ((2.f * p1.x)
              + (-p0.x + p2.x) * t
              + (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2
              + (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3),
        0.5f * ((2.f * p1.y)
              + (-p0.y + p2.y) * t
              + (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2
              + (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3),
    };
}

namespace detail {

inline bool pointOnSegment(Point query, Point a, Point b) {
    constexpr float kBoundaryTolerance = 1e-4f;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    float t = 0.f;
    if (lengthSquared > 0.f) {
        t = ((query.x - a.x) * dx + (query.y - a.y) * dy) / lengthSquared;
        if (t < 0.f)
            t = 0.f;
        else if (t > 1.f)
            t = 1.f;
    }
    const float nearestX = a.x + t * dx;
    const float nearestY = a.y + t * dy;
    const float distanceX = query.x - nearestX;
    const float distanceY = query.y - nearestY;
    return distanceX * distanceX + distanceY * distanceY
        <= kBoundaryTolerance * kBoundaryTolerance;
}

} // namespace detail

inline bool pointInClosedCatmullRom(
        const Point* points,
        std::size_t pointCount,
        Point query,
        unsigned subdivisionsPerSegment = 12) {
    if (!points || pointCount < 3 || subdivisionsPerSegment == 0)
        return false;

    bool inside = false;
    Point previous = points[0];
    for (std::size_t i = 0; i < pointCount; ++i) {
        const Point p0 = points[(i + pointCount - 1) % pointCount];
        const Point p1 = points[i];
        const Point p2 = points[(i + 1) % pointCount];
        const Point p3 = points[(i + 2) % pointCount];
        for (unsigned step = 1; step <= subdivisionsPerSegment; ++step) {
            const float t = static_cast<float>(step)
                          / static_cast<float>(subdivisionsPerSegment);
            const Point current = catmullRom(p0, p1, p2, p3, t);
            if (detail::pointOnSegment(query, previous, current))
                return true;

            const bool straddles = (previous.y > query.y) != (current.y > query.y);
            if (straddles) {
                const float crossingX = previous.x
                    + (query.y - previous.y) * (current.x - previous.x)
                    / (current.y - previous.y);
                if (query.x < crossingX)
                    inside = !inside;
            }
            previous = current;
        }
    }
    return inside;
}

} // namespace spkyvcv::pad_geometry
