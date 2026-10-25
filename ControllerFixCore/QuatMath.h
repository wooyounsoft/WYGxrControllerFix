#pragma once

#include "VelocityEstimator.h"

namespace controllerfix {

inline Quat conjugate(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }

inline Quat multiply(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline Vec3 rotate(const Quat& q, const Vec3& v) {
    Quat p{0, v.x, v.y, v.z};
    Quat r = multiply(multiply(q, p), conjugate(q));
    return {r.x, r.y, r.z};
}

}  // namespace controllerfix
