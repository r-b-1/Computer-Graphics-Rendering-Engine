#include "color.h"
#include "ray.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr float pi = 3.14159265f;
float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
vec3 reflect(const vec3& v, const vec3& n) { return v - 2 * dot(v, n) * n; }
float smooth(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}

float hash(const vec3& p) {
    float n = std::sin(dot(p, vec3(127.1f, 311.7f, 74.7f))) * 43758.5453f;
    return n - std::floor(n);
}

float noise(const vec3& p) {
    vec3 cell(std::floor(p.x()), std::floor(p.y()), std::floor(p.z()));
    vec3 f = p - cell;
    for (int k = 0; k < 3; ++k) f[k] = f[k] * f[k] * (3 - 2 * f[k]);
    float result = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                result += hash(cell + vec3(x, y, z)) *
                    (x ? f.x() : 1 - f.x()) * (y ? f.y() : 1 - f.y()) *
                    (z ? f.z() : 1 - f.z());
    return result;
}

float fog_density(vec3 p, float time) {
    p += vec3(time * 0.18f, 0, time * 0.09f);
    float billow = noise(p * 1.6f + vec3(0, noise(p * 0.7f) * 2, 0));
    float detail = noise(p * 4.2f);
    float ground = std::exp(-std::max(p.y() + 1.25f, 0.0f) * 1.3f);
    float cloud = smooth(0.40f, 0.72f, billow + detail * 0.22f);
    float foreground = std::exp(-std::pow((p.z() + 0.1f) * 0.85f, 2.0f));
    return 0.012f + ground * (0.025f + cloud * (0.28f + 2.4f * foreground));
}

enum Material { Gold, Chrome, Glass, Black, Floor, Led };
struct Hit { float t; point3 p; vec3 normal; Material material; };
struct Ellipsoid { point3 center; vec3 scale; Material material; bool shell = false; };

// Separate ear housings, chin, neck and shoulder armor give the helmet a real silhouette.
const std::vector<Ellipsoid> model = {
    {{0, 0.1f, -1.3f}, {0.87f, 1.06f, 0.76f}, Gold, true},
    {{-0.9f, 0.03f, -1.32f}, {0.19f, 0.39f, 0.42f}, Gold},
    {{0.9f, 0.03f, -1.32f}, {0.19f, 0.39f, 0.42f}, Chrome},
    {{-0.94f, 0.03f, -1.01f}, {0.145f, 0.28f, 0.13f}, Black},
    {{0.94f, 0.03f, -1.01f}, {0.145f, 0.28f, 0.13f}, Chrome},
    {{0, -0.71f, -1.23f}, {0.66f, 0.23f, 0.6f}, Gold, true},
    {{0, -1.02f, -1.38f}, {0.37f, 0.36f, 0.38f}, Black},
    {{0, -1.56f, -1.46f}, {1.22f, 0.53f, 0.57f}, Black}
};

bool hit_world(const ray& r, float min_t, float max_t, Hit& hit, bool floor = true) {
    bool found = false;
    for (const auto& e : model) {
        vec3 inv(1 / e.scale.x(), 1 / e.scale.y(), 1 / e.scale.z());
        vec3 o = (r.origin() - e.center) * inv, d = r.direction() * inv;
        float a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1;
        float disc = b * b - a * c;
        if (disc < 0) continue;
        float t = (-b - std::sqrt(disc)) / a;
        if (t < min_t) t = (-b + std::sqrt(disc)) / a;
        if (t < min_t || t > max_t) continue;
        max_t = t;
        found = true;
        hit = {t, r.at(t), unit_vector((r.at(t) - e.center) * inv * inv), e.material};
        if (e.shell) {
            float x = hit.p.x(), y = hit.p.y();
            hit.material = x < 0 ? Gold : Chrome;
            if (hit.normal.z() > 0.38f) {
                // Guy-Manuel's deep wraparound faceplate meets Thomas's narrow visor.
                float top = x < 0 ? 0.53f - 0.18f * x * x : 0.32f - 0.12f * x;
                float bottom = x < 0 ? -0.49f + 0.30f * x * x : -0.025f - 0.06f * x;
                if (y > bottom && y < top) hit.material = Glass;
                if (std::fabs(x) < 0.012f) hit.material = Black;
                if (x > 0.09f && x < 0.55f && y > -0.55f && y < -0.43f) {
                    hit.material = Black;
                    if (std::fmod(x - 0.09f, 0.075f) < 0.012f) hit.material = Chrome;
                }
                if (x < -0.58f && x > -0.76f && y > -0.35f && y < 0.34f &&
                    std::fmod(y + 0.35f, 0.095f) < 0.025f) hit.material = Led;
            }
        }
    }
    if (floor && std::fabs(r.direction().y()) > 1e-6f) {
        float t = (-1.62f - r.origin().y()) / r.direction().y();
        if (t > min_t && t < max_t) {
            hit = {t, r.at(t), {0, 1, 0}, Floor};
            found = true;
        }
    }
    return found;
}

// A dark studio with large strip lights reflected in the metal, not painted highlights.
color environment(const vec3& d) {
    color c = color(0.018f, 0.025f, 0.05f) * (0.4f + 0.6f * saturate(d.y()));
    float az = std::atan2(d.x(), d.z());
    float strip = std::exp(-std::pow((az + 0.72f) / 0.16f, 8.0f));
    float strip2 = std::exp(-std::pow((az - 0.92f) / 0.10f, 8.0f));
    float height = smooth(-0.45f, -0.2f, d.y()) * (1 - smooth(0.65f, 0.85f, d.y()));
    c += height * (strip * color(4.0f, 3.3f, 2.3f) + strip2 * color(2.5f, 3.6f, 4.5f));
    c += color(1.4f, 1.65f, 2.0f) * smooth(0.58f, 0.72f, d.y());
    c += color(0.04f, 0.28f, 0.48f) * std::pow(saturate(d.x()), 6.0f);
    c += color(0.4f, 0.055f, 0.012f) * std::pow(saturate(-d.x()), 6.0f);
    return c;
}

color shade(const Hit& h, const vec3& view) {
    vec3 reflected = reflect(view, h.normal);
    float facing = saturate(dot(-view, h.normal));
    float fresnel = std::pow(1 - facing, 5.0f);
    color env = environment(reflected);
    float key = saturate(dot(h.normal, unit_vector(vec3(-0.5f, 0.8f, 1))));
    switch (h.material) {
        case Gold: return color(1, 0.55f, 0.12f) * (env * 0.72f + color(0.12f, 0.10f, 0.065f) * key);
        case Chrome: return env * color(0.88f, 0.94f, 1) + color(0.055f, 0.065f, 0.08f) * key;
        case Glass: return color(0.004f, 0.009f, 0.016f) + env * (0.065f + fresnel * 0.65f);
        case Led: return color(0.06f, 1.4f, 1.8f);
        case Black: return color(0.008f, 0.011f, 0.017f) * (0.4f + key) + env * (0.018f + 0.12f * fresnel);
        case Floor: return color(0.008f, 0.012f, 0.02f);
    }
    return {};
}

struct Beam { point3 origin; vec3 direction; color tint; float length; };
std::vector<Beam> make_lasers(float time) {
    std::vector<Beam> beams;
    for (int side : {-1, 1}) {
        for (int i = 0; i < 7; ++i) {
            point3 origin(side * 2.65f, -1.3f, -3.2f);
            float sweep = std::sin(time * 0.6f + i * 0.18f) * 0.14f;
            vec3 direction = unit_vector(vec3(-side * (0.3f + i * 0.24f), 1.1f + sweep, -0.12f));
            Hit block;
            float length = 9;
            if (hit_world(ray(origin, direction), 0.01f, length, block)) length = block.t;
            beams.push_back({origin, direction, side < 0 ? color(1, 0.11f, 0.025f) : color(0.015f, 0.48f, 1), length});
        }
    }
    return beams;
}

// Integrate Gaussian beam profiles analytically along each viewing ray. Unlike a
// coarse room-wide ray march this cannot skip a thin laser between sample points.
color laser_scattering(const ray& r, float far, const std::vector<Beam>& beams, float time) {
    color result;
    for (const auto& b : beams) {
        vec3 w = r.origin() - b.origin;
        float dv = dot(r.direction(), b.direction);
        vec3 perpendicular = r.direction() - dv * b.direction;
        vec3 offset = w - dot(w, b.direction) * b.direction;
        float a = dot(perpendicular, perpendicular);
        float lo = 0, hi = far;
        float along0 = dot(w, b.direction);
        if (std::fabs(dv) > 1e-6f) {
            float t0 = -along0 / dv, t1 = (b.length - along0) / dv;
            lo = std::max(lo, std::min(t0, t1));
            hi = std::min(hi, std::max(t0, t1));
        } else if (along0 < 0 || along0 > b.length) continue;
        if (hi <= lo) continue;
        float t = a > 1e-8f ? -dot(offset, perpendicular) / a : (lo + hi) * 0.5f;
        float d2 = (offset + t * perpendicular).length_squared();
        if (d2 > 0.08f) continue;
        float fog = fog_density(r.at(std::clamp(t, lo, hi)), time);
        for (int layer = 0; layer < 2; ++layer) {
            float radius = layer == 0 ? 0.016f : 0.085f;
            float integral;
            if (a > 1e-8f) {
                float scale = std::sqrt(a) / radius;
                integral = std::exp(-d2 / (radius * radius)) * std::sqrt(pi) / (2 * scale) *
                    (std::erf((hi - t) * scale) - std::erf((lo - t) * scale));
            } else {
                integral = (hi - lo) * std::exp(-d2 / (radius * radius));
            }
            result += b.tint * (integral * fog * (layer == 0 ? 850.0f : 12.0f) *
                std::exp(-0.055f * std::clamp(t, lo, hi)));
        }
    }
    return result;
}

color render_ray(const ray& r, const std::vector<Beam>& beams, float time, bool reflection = false) {
    Hit h;
    float far = 14;
    color surface(0.003f, 0.005f, 0.013f);
    if (hit_world(r, 0.002f, far, h)) {
        far = h.t;
        surface = shade(h, r.direction());
        if (h.material == Floor && !reflection) {
            vec3 d = reflect(r.direction(), h.normal);
            float fresnel = 0.22f + 0.5f * std::pow(1 - saturate(-r.direction().y()), 5.0f);
            surface += render_ray(ray(h.p + vec3(0, 0.003f, 0), d), beams, time, true) * fresnel;
            float contact = std::exp(-(h.p.x() * h.p.x() + std::pow(h.p.z() + 1.4f, 2.0f)) * 1.5f);
            surface *= 1 - 0.75f * contact;
        }
    }
    // Beer-Lambert extinction and ambient scattering apply in front of ALL materials.
    constexpr int steps = 40;
    float dt = far / steps, transmission = 1;
    color smoke;
    for (int i = 0; i < steps; ++i) {
        vec3 p = r.at((i + 0.5f) * dt);
        float density = fog_density(p, time);
        float absorbed = 1 - std::exp(-density * dt);
        float left = 1 / (1 + (p - vec3(-2.8f, -0.6f, -2.5f)).length_squared() * 0.6f);
        float right = 1 / (1 + (p - vec3(2.8f, -0.6f, -2.5f)).length_squared() * 0.6f);
        color lighting = color(0.018f, 0.028f, 0.06f) + left * color(0.48f, 0.055f, 0.015f) + right * color(0.015f, 0.18f, 0.48f);
        smoke += transmission * absorbed * lighting;
        transmission *= 1 - absorbed;
    }
    return surface * transmission + smoke + laser_scattering(r, far, beams, time);
}

float display(float x) {
    x = std::max(0.0f, x);
    float mapped = saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
    return mapped <= 0.0031308f ? 12.92f * mapped : 1.055f * std::pow(mapped, 1 / 2.4f) - 0.055f;
}
} // namespace

int main(int argc, char** argv) {
    int width = 960, samples = 4;
    float time = 0;
    std::string output;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Usage: daftpunk_lasershow [--width 64..3840] [--samples 1..16]\n"
                             "                         [--time seconds] [--output image.ppm]\n"
                             "Defaults: width 960, samples 4, time 0; PPM on stdout.\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            std::string value = argv[++i];
            std::size_t used = 0;
            if (arg == "--width" || arg == "--samples") {
                int n = std::stoi(value, &used);
                if (used != value.size()) throw std::runtime_error("Invalid integer for " + arg);
                if (arg == "--width") width = n; else samples = n;
            } else if (arg == "--time") {
                time = std::stof(value, &used);
                if (used != value.size() || !std::isfinite(time) || std::fabs(time) > 100000)
                    throw std::runtime_error("Time must be finite and within +/-100000 seconds");
            } else if (arg == "--output") output = value;
            else throw std::runtime_error("Unknown option: " + arg);
        }
        if (width < 64 || width > 3840 || samples < 1 || samples > 16)
            throw std::runtime_error("Width must be 64..3840 and samples must be 1..16");
        std::ofstream file;
        if (!output.empty()) {
            file.open(output);
            if (!file) throw std::runtime_error("Cannot open output: " + output);
        }
        std::ostream& out = output.empty() ? std::cout : file;
        int height = width * 9 / 16;
        std::vector<color> pixels(width * height), bloom(width * height), temp(width * height);
        auto beams = make_lasers(time);
        point3 camera(0, 0.35f, 4.5f);
        vec3 forward = unit_vector(vec3(0, -0.25f, -5.8f));
        vec3 right(1, 0, 0), up(0, -forward.z(), forward.y());
        for (int y = 0; y < height; ++y) {
            if (y % 32 == 0) std::clog << "\rRendering " << 100 * y / height << "%" << std::flush;
            for (int x = 0; x < width; ++x) {
                color c;
                for (int s = 0; s < samples; ++s) {
                    // Hammersley pixel samples: deterministic and valid for any sample count.
                    unsigned bits = static_cast<unsigned>(s);
                    float sy = 0, weight = 0.5f;
                    while (bits) { sy += (bits & 1u) * weight; bits >>= 1; weight *= 0.5f; }
                    float sx = (s + 0.5f) / samples;
                    sy += 0.5f / samples;
                    float u = (2 * (x + sx) / width - 1) * (float(width) / height) * 0.32f;
                    float v = (1 - 2 * (y + sy) / height) * 0.32f;
                    c += render_ray(ray(camera, unit_vector(forward + u * right + v * up)), beams, time);
                }
                pixels[y * width + x] = c / float(samples);
                for (int k = 0; k < 3; ++k) bloom[y * width + x][k] = std::max(0.0f, c[k] / samples - 1.0f);
            }
        }
        // Small separable bloom in linear HDR, before tone mapping.
        int radius = std::max(2, width / 180);
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                color sum;
                float weights = 0;
                for (int d = -radius; d <= radius; ++d) {
                    float w = std::exp(-2.0f * d * d / (radius * radius));
                    int xx = std::clamp(x + (pass == 0 ? d : 0), 0, width - 1);
                    int yy = std::clamp(y + (pass == 1 ? d : 0), 0, height - 1);
                    sum += bloom[yy * width + xx] * w;
                    weights += w;
                }
                temp[y * width + x] = sum / weights;
            }
            bloom.swap(temp);
        }
        out << "P3\n" << width << ' ' << height << "\n255\n";
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            color c = pixels[i] + bloom[i] * 0.18f;
            for (int k = 0; k < 3; ++k) out << std::clamp(int(display(c[k]) * 255 + 0.5f), 0, 255) << (k == 2 ? '\n' : ' ');
        }
        out.flush();
        if (!out) throw std::runtime_error("Failed to write image");
        std::clog << "\rRendered " << width << 'x' << height << " at " << samples << " samples/pixel.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
