#define main renderer_main
#include "../src/main.cc"
#undef main

#include <sstream>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    auto finite_nonnegative = [](const color& c) {
        for (int k = 0; k < 3; ++k)
            if (!std::isfinite(c[k]) || c[k] < 0) return false;
        return true;
    };

    for (int z = -3; z <= 3; ++z) {
        for (int y = -3; y <= 3; ++y) {
            for (int x = -3; x <= 3; ++x) {
                for (float offset : {0.0f, 0.37f}) {
                    vec3 p(x + offset, y + offset, z + offset);
                    float h = hash(p), n = noise(p);
                    check(std::isfinite(h) && h >= 0 && h < 1, "hash range");
                    check(std::isfinite(n) && n >= 0 && n <= 1, "noise range");
                    for (float time : {-10.0f, 0.0f, 10.0f}) {
                        float density = fog_density(p, time);
                        check(std::isfinite(density) && density >= 0, "fog density");
                    }
                }
            }
        }
    }

    for (float x : {-0.35f, 0.35f}) {
        for (float y : {0.0f, 0.7f}) {
            Hit hit{};
            bool found = hit_world(ray(point3(x, y, 4.5f), vec3(0, 0, -1)),
                                   0.002f, 14, hit);
            check(found, "front shell ray hits");
            if (found) {
                Material expected = y == 0 ? Glass : (x < 0 ? Gold : Chrome);
                check(hit.material == expected,
                      y == 0 ? "left/right visor material" : "left/right shell material");
            }
        }
    }

    const std::vector<Beam> beams = {{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, 5}};
    for (float direction : {-1.0f, 1.0f}) {
        color parallel = laser_scattering(ray(point3(0, 2, 0), vec3(0, direction, 0)),
                                          4, beams, 0);
        check(finite_nonnegative(parallel), "parallel scattering finite nonnegative");
        check(parallel.x() > 0, "parallel scattering positive");
    }
    ray crossing(point3(0, 2, 2), vec3(0, 0, -1));
    color clipped = laser_scattering(crossing, 1, beams, 0);
    color visible = laser_scattering(crossing, 4, beams, 0);
    check(finite_nonnegative(clipped), "clipped scattering finite nonnegative");
    check(clipped.length() < 1e-6f, "foreground clips beam scattering");
    check(finite_nonnegative(visible), "perpendicular scattering finite nonnegative");
    check(visible.x() > 0, "unclipped beam scattering positive");

    for (float input : {-1.0f, 0.0f, 0.001f, 0.01f, 0.18f, 1.0f, 4.0f, 100.0f, 10000.0f}) {
        float output = display(input);
        check(std::isfinite(output) && output >= 0 && output <= 1, "display range");
    }

    for (float time : {-2.0f, 0.0f, 2.0f}) {
        auto lasers = make_lasers(time);
        for (int y = -4; y <= 4; ++y) {
            for (int x = -6; x <= 6; ++x) {
                color c = render_ray(ray(point3(0, 0.35f, 4.5f),
                                         unit_vector(vec3(x * 0.1f, y * 0.1f, -1))),
                                     lasers, time);
                check(finite_nonnegative(c), "render grid finite nonnegative");
            }
        }
    }

    auto cli = [&](std::vector<std::string> arguments, int expected, bool render = false) {
        // Keep accidental renders small and capture output rather than emitting PPM.
        std::vector<std::string> storage = {"renderer", "--width", "64", "--samples", "1"};
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& argument : storage) argv.push_back(argument.data());
        argv.push_back(nullptr);
        std::ostringstream out, err, log;
        auto* old_out = std::cout.rdbuf(out.rdbuf());
        auto* old_err = std::cerr.rdbuf(err.rdbuf());
        auto* old_log = std::clog.rdbuf(log.rdbuf());
        int result = renderer_main(static_cast<int>(storage.size()), argv.data());
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::clog.rdbuf(old_log);
        check(result == expected, arguments.front().c_str());
        check((out.str().find("P3\n") == 0) == render, "CLI image output");
        check(expected == 0 ? !out.str().empty() && err.str().empty()
                            : out.str().empty() && !err.str().empty(),
              "CLI help/error output");
        return out.str();
    };
    cli({"--width", "0"}, 1);
    cli({"--samples", "17"}, 1);
    cli({"--time", "nan"}, 1);
    cli({"--unknown", "value"}, 1);
    cli({"--width"}, 1);
    cli({"--help"}, 0);
    cli({"--width", "64junk"}, 1);
    cli({"--time", "inf"}, 1);
    std::string image = cli({"--time", "0"}, 0, true);
    check(image == cli({"--time", "0"}, 0, true), "deterministic rendering");
    check(image != cli({"--time", "2"}, 0, true), "time changes scene");
    std::istringstream ppm(image);
    std::string magic;
    int width = 0, height = 0, maximum = 0;
    ppm >> magic >> width >> height >> maximum;
    check(magic == "P3" && width == 64 && height == 36 && maximum == 255, "PPM header");
    int channel = 0, count = 0;
    while (ppm >> channel) {
        check(channel >= 0 && channel <= 255, "PPM channel range");
        ++count;
    }
    check(ppm.eof() && count == 64 * 36 * 3, "PPM pixel count");

    if (failures) std::cerr << failures << " renderer check(s) failed\n";
    return failures ? 1 : 0;
}
