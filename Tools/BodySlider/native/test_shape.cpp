#include "ShapeMath.h"
#include <cassert>
#include <limits>

static float Point(const float p[3], const Shape::Matrix& matrix, int axis) {
    return matrix.m[3][axis] + p[0]*matrix.m[0][axis] + p[1]*matrix.m[1][axis] + p[2]*matrix.m[2][axis];
}
int main() {
    uint64_t guid = 42;
    assert(Shape::Guid("0x0000000000001234", 18, guid) && guid == 0x1234);
    assert(!Shape::Guid("0000000000000000", 16, guid));
    assert(!Shape::Guid("00000000000000zz", 16, guid));
    assert(!Shape::Guid("0000000000001234junk", 20, guid));
    assert(!Shape::ValidValue(std::numeric_limits<double>::quiet_NaN()));
    assert(!Shape::ValidValue(std::numeric_limits<double>::infinity()));
    assert(!Shape::ValidValue(1.001) && Shape::ValidValue(-1));
    Shape::Matrix local{{ {0,1,0,0}, {-1,0,0,0}, {0,0,1,0}, {3,4,5,1} }};
    float pivot[3] = {2,3,4};
    Shape::Transform transform;
    assert(!transform.Active());
    auto neutral = Shape::Apply(local, pivot, transform);
    assert(std::memcmp(&neutral, &local, sizeof(local)) == 0);
    transform.scale[0] = 1.5f;
    transform.scale[1] = .8f;
    transform.scale[2] = 1.2f;
    auto shaped = Shape::Apply(local, pivot, transform);
    for (int a = 0; a < 3; ++a) assert(std::fabs(Point(pivot, shaped, a)-Point(pivot, local, a)) < 1e-5f);
    float point[3] = {pivot[0]+2, pivot[1], pivot[2]};
    assert(std::fabs(Point(point, shaped, 1)-Point(pivot, shaped, 1)-3) < 1e-5f);
    transform.move[2] = .25f;
    auto translated = Shape::Apply(local, pivot, transform);
    assert(std::fabs(Point(pivot, translated, 2)-Point(pivot, local, 2)-.25f) < 1e-5f);
    Shape::Matrix identity{{ {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} }};
    auto multiplied = Shape::Multiply(shaped, identity);
    assert(std::memcmp(&multiplied, &shaped, sizeof(shaped)) == 0);
    return 0;
}
