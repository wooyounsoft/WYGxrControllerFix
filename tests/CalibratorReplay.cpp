// 개발용: VD 공유 메모리 캡처(tools/capture_vd_shm.py CSV)를 VdFrameCalibrator 에 재생해서 맞춘 프레임을 출력한다.
// usage: CalibratorReplay vdshm.csv
//        CalibratorReplay --openxr openxr_capture.csv   (레이어 캡처: 공간마다 따로, 뒤로 외삽 없음)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "QuatMath.h"
#include "VdFrameCalibrator.h"

using namespace controllerfix;

namespace {

constexpr double POSE_LAG_SEC = 0.037;  // VD 외삽 (poseLagMs 기본값)
constexpr double PI = 3.14159265358979323846;

Quat expq(const Vec3& r) {
    double n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (n < 1e-12) return {};
    double s = std::sin(n / 2) / n;
    return {std::cos(n / 2), r.x * s, r.y * s, r.z * s};
}

void printFrame(const char* label, double t, const VdFrameCalibrator& c) {
    Quat b = c.frame();
    double s = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    double ang = 2 * std::atan2(s, std::fabs(b.w));
    double k = s > 0 ? (b.w < 0 ? -1 : 1) * ang / s * 180 / PI : 0;
    std::printf("%-6s t=%6.1fs %s rotvec deg (x %+.1f, y %+.1f, z %+.1f) fitErr %.1f pairs %zu solves %llu\n", label, t,
                c.calibrated() ? "calibrated" : "default   ", b.x * k, b.y * k, b.z * k, c.fitErrorDeg(), c.pairCount(),
                static_cast<unsigned long long>(c.solveCount()));
}


Quat fromMatrix(const double m[3][3]) {
    double tr = m[0][0] + m[1][1] + m[2][2];
    Quat q;
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        q = {0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
        q = {(m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s};
    } else if (m[1][1] > m[2][2]) {
        double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
        q = {(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s};
    } else {
        double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        q = {(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s};
    }
    return q;
}

int replayOpenXr(const char* path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    std::map<int, VdFrameCalibrator> cals;       // 공간(device)마다
    std::map<std::string, int> baseIndex;        // 같은 공간의 기준 공간 구분
    double t = 0;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> c;
        while (std::getline(ss, cell, ',')) c.push_back(cell);
        if (c.size() < 21) continue;
        t = std::atof(c[0].c_str());
        int dev = std::atoi(c[1].c_str());
        double m[3][3] = {{std::atof(c[3].c_str()), std::atof(c[4].c_str()), std::atof(c[5].c_str())},
                          {std::atof(c[7].c_str()), std::atof(c[8].c_str()), std::atof(c[9].c_str())},
                          {std::atof(c[11].c_str()), std::atof(c[12].c_str()), std::atof(c[13].c_str())}};
        if (!std::isfinite(m[0][0]) || !std::isfinite(m[1][1])) continue;
        Quat r = fromMatrix(m);
        Vec3 p{std::atof(c[6].c_str()), std::atof(c[10].c_str()), std::atof(c[14].c_str())};
        Vec3 lin{std::atof(c[15].c_str()), std::atof(c[16].c_str()), std::atof(c[17].c_str())};
        Vec3 ang{std::atof(c[18].c_str()), std::atof(c[19].c_str()), std::atof(c[20].c_str())};
        auto [it, inserted] = cals.try_emplace(dev);
        if (inserted) it->second.setPrior(expq({15 * PI / 180, 0, 0}));
        int b = baseIndex.emplace(c[2], static_cast<int>(baseIndex.size())).first->second;
        it->second.addSample(b, t, r, p, lin, rotate(r, ang));
    }
    for (auto& [dev, cal] : cals) {
        char label[32];
        std::snprintf(label, sizeof(label), "dev %d", dev);
        printFrame(label, t, cal);
        std::printf("        angular %s\n", cal.angularReliable() ? "used (linear+angular)" : "NOT used (linear only)");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    if (std::string(argv[1]) == "--openxr" && argc > 2) return replayOpenXr(argv[2]);
    std::ifstream in(argv[1]);
    std::string line;
    std::getline(in, line);
    std::map<std::string, size_t> col;
    {
        std::stringstream ss(line);
        std::string name;
        for (size_t i = 0; std::getline(ss, name, ','); ++i) col[name] = i;
    }
    VdFrameCalibrator cal;
    cal.setPrior(expq({15 * PI / 180, 0, 0}));
    cal.setUndoLag(POSE_LAG_SEC);
    double nextPrint = 1;
    double t = 0;
    while (std::getline(in, line)) {
        std::vector<double> v;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) v.push_back(cell.empty() || cell.size() > 40 ? 0 : std::atof(cell.c_str()));
        t = v[col["t"]];
        int dev = 0;
        for (const char* hand : {"left", "right"}) {
            auto g = [&](const char* k) { return v[col[std::string(hand) + "_" + k]]; };
            Quat r{g("qw"), g("qx"), g("qy"), g("qz")};
            if (std::fabs(r.w) + std::fabs(r.x) + std::fabs(r.y) + std::fabs(r.z) < 0.5) { ++dev; continue; }
            Vec3 p{g("px"), g("py"), g("pz")}, lin{g("vx"), g("vy"), g("vz")}, ang{g("wx"), g("wy"), g("wz")};
            // DriverPose 와 같게: VD 가 보고 속도로 뒤로 외삽한 포즈
            Vec3 pd{p.x - POSE_LAG_SEC * lin.x, p.y - POSE_LAG_SEC * lin.y, p.z - POSE_LAG_SEC * lin.z};
            Quat rd = multiply(expq({-POSE_LAG_SEC * ang.x, -POSE_LAG_SEC * ang.y, -POSE_LAG_SEC * ang.z}), r);
            cal.addSample(dev++, t, rd, pd, lin, ang);
        }
        if (t >= nextPrint) {
            printFrame("", t, cal);
            nextPrint = t < 5 ? t + 0.5 : t + 10;
        }
    }
    printFrame("final", t, cal);
    return 0;
}
