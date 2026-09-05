#include "color.h"
#include "ray.h"
#include "vec3.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

// -----------------------------------------------------------------------------
// Math helpers
// -----------------------------------------------------------------------------

vec3 reflect(const vec3& v, const vec3& n) {
    return v - 2.0f * dot(v, n) * n;
}

// Cheap procedural hash.
float hash13(vec3 p) {
    p = vec3(
        std::fmod(std::sin(dot(p, vec3(127.1f, 311.7f, 74.7f))) * 43758.5453f, 1.0f),
        std::fmod(std::sin(dot(p, vec3(269.5f, 183.3f, 246.1f))) * 43758.5453f, 1.0f),
        std::fmod(std::sin(dot(p, vec3(113.5f, 271.9f, 124.6f))) * 43758.5453f, 1.0f));
    return std::fmod(p.x() + p.y() + p.z(), 1.0f);
}

float value_noise(vec3 p) {
    vec3 i(std::floor(p.x()), std::floor(p.y()), std::floor(p.z()));
    vec3 f(p.x() - i.x(), p.y() - i.y(), p.z() - i.z());
    vec3 u(f.x() * f.x() * (3 - 2 * f.x()),
           f.y() * f.y() * (3 - 2 * f.y()),
           f.z() * f.z() * (3 - 2 * f.z()));
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0));
    float n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1));
    float n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1));
    float n111 = hash13(i + vec3(1, 1, 1));
    return (
        n000 * (1 - u.x()) * (1 - u.y()) * (1 - u.z()) +
        n100 * u.x()       * (1 - u.y()) * (1 - u.z()) +
        n010 * (1 - u.x()) * u.y()       * (1 - u.z()) +
        n110 * u.x()       * u.y()       * (1 - u.z()) +
        n001 * (1 - u.x()) * (1 - u.y()) * u.z()       +
        n101 * u.x()       * (1 - u.y()) * u.z()       +
        n011 * (1 - u.x()) * u.y()       * u.z()       +
        n111 * u.x()       * u.y()       * u.z()
    );
}

// Smoke density: low-frequency layered noise so the room is hazy near the
// ground and clear above the helmet. Cheap to evaluate (no trilinear noise).
float fog_density(const vec3& p) {
    float low = std::fmax(0.0f, 1.0f - (p.y() + 1.5f) * 0.40f);
    float n = 0.5f + 0.5f * std::sin(0.7f * p.x() + 0.3f * p.y()) *
                       std::sin(0.9f * p.y() - 0.5f * p.z()) *
                       std::sin(0.4f * p.z() + 0.6f * p.x());
    n += 0.10f * (hash13(p * 2.7f) - 0.5f);
    return std::fmax(0.0f, n - 0.10f) * low;
}

// -----------------------------------------------------------------------------
// Scene primitives
// -----------------------------------------------------------------------------

struct hit_record {
    float t;
    vec3 p;
    vec3 normal;
    int material; // 0 = gold, 1 = chrome, 2 = visor, 3 = mouth slit, 4 = ground
};

// Ellipsoid = sphere stretched by scale, then translated. We transform the
// ray into the sphere's local frame, intersect the unit sphere, then map back.
struct ellipsoid {
    vec3 center;
    vec3 scale;   // half-extents in each axis
};

bool hit_ellipsoid(const ellipsoid& e, const ray& r, float t_min, float t_max, hit_record& rec, int material_floor, int material_ceiling) {
    vec3 inv_scale(1.0f / e.scale.x(), 1.0f / e.scale.y(), 1.0f / e.scale.z());
    vec3 local_o = (r.origin() - e.center) * inv_scale;
    vec3 local_d = r.direction() * inv_scale;

    vec3 oc = local_o;
    auto a = local_d.length_squared();
    auto h = dot(local_d, oc);
    auto c = oc.length_squared() - 1.0f;
    auto disc = h * h - a * c;
    if (disc < 0) return false;
    auto sqd = std::sqrt(disc);
    auto root = (-h - sqd) / a;
    if (root < t_min || root > t_max) {
        root = (-h + sqd) / a;
        if (root < t_min || root > t_max) return false;
    }

    rec.t = root;
    rec.p = r.at(root);

    vec3 grad(rec.p.x() - e.center.x(),
              rec.p.y() - e.center.y(),
              rec.p.z() - e.center.z());
    grad = vec3(grad.x() / (e.scale.x() * e.scale.x()),
                grad.y() / (e.scale.y() * e.scale.y()),
                grad.z() / (e.scale.z() * e.scale.z()));
    rec.normal = unit_vector(grad);

    float y = rec.p.y();
    bool front_facing = rec.normal.z() > 0.3f;
    if (front_facing) {
        if (y > 0.00f && y < 0.18f) {
            rec.material = 2; // visor
            return true;
        }
        if (y > -0.32f && y < -0.24f && std::fabs(rec.p.x()) < 0.22f) {
            rec.material = 3; // mouth slit
            return true;
        }
    }
    if (rec.p.x() < 0) {
        rec.material = 0;
    } else if (rec.p.x() > 0) {
        rec.material = 1;
    } else {
        rec.material = 0;
    }
    (void)material_floor; (void)material_ceiling;
    return true;
}

bool hit_plane(const point3& origin, const vec3& normal, float t_min, float t_max, const ray& r, hit_record& rec, int material) {
    auto denom = dot(normal, r.direction());
    if (std::fabs(denom) < 1e-6f) return false;
    auto t = dot(origin - r.origin(), normal) / denom;
    if (t < t_min || t > t_max) return false;
    rec.t = t;
    rec.p = r.at(t);
    rec.normal = normal;
    rec.material = material;
    return true;
}

const ellipsoid kHelmet = { vec3(0, 0.05f, -1.3f), vec3(1.05f, 0.95f, 1.05f) };

bool hit_world(const ray& r, float t_min, float t_max, hit_record& rec) {
    bool hit_anything = false;
    float closest = t_max;
    hit_record tmp;
    if (hit_ellipsoid(kHelmet, r, t_min, closest, tmp, 0, 1)) { hit_anything = true; closest = tmp.t; rec = tmp; }
    if (hit_plane(point3(0, -1.10f, 0), vec3(0, 1, 0), t_min, closest, r, tmp, 4)) { hit_anything = true; closest = tmp.t; rec = tmp; }
    return hit_anything;
}

// -----------------------------------------------------------------------------
// Materials
// -----------------------------------------------------------------------------

color shade_metal(const hit_record& rec, const vec3& tint) {
    // Stage lights in front of the helmet.
    vec3 key_light  = unit_vector(vec3( 0.4f,  0.8f,  0.5f));
    vec3 fill_light = unit_vector(vec3(-0.6f,  0.3f,  0.5f));
    vec3 back_light = unit_vector(vec3( 0.0f, -0.2f,  1.0f));
    float kd_key  = std::fmax(0.0f, dot(rec.normal, key_light));
    float kd_fill = std::fmax(0.0f, dot(rec.normal, fill_light));
    float kd_back = std::fmax(0.0f, dot(rec.normal, back_light));

    color base = tint * (0.30f + 1.20f * kd_key + 0.40f * kd_fill + 0.20f * kd_back);

    // Specular highlights.
    vec3 spec_dirs[3] = {
        unit_vector(vec3( 0.5f,  0.7f,  0.5f)),
        unit_vector(vec3(-0.4f,  0.4f,  0.6f)),
        unit_vector(vec3( 0.0f, -0.3f,  1.0f)),
    };
    color spec_cols[3] = {
        color(1.0f, 0.95f, 0.85f),
        color(0.85f, 0.90f, 1.0f),
        color(1.0f, 0.90f, 0.95f),
    };
    color spec_accum(0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        vec3 r = reflect(-spec_dirs[i], rec.normal);
        float spec = std::pow(std::fmax(0.0f, r.z()), 12.0f);
        spec_accum += tint * spec_cols[i] * spec * 1.6f;
    }
    return base + spec_accum;
}

color shade_gold(const hit_record& rec) {
    return shade_metal(rec, color(1.00f, 0.78f, 0.34f));
}

color shade_chrome(const hit_record& rec) {
    return shade_metal(rec, color(0.88f, 0.90f, 0.95f));
}

color shade_visor(const hit_record& rec, const vec3& view_dir) {
    // Glossy black visor with a thin specular streak.
    vec3 light_dir = unit_vector(vec3(0.4f, 0.6f, 0.7f));
    float spec = std::pow(std::fmax(0.0f, dot(reflect(-light_dir, rec.normal), -view_dir)), 60.0f);
    color base(0.015f, 0.015f, 0.025f);
    return base + spec * color(0.5f, 0.6f, 0.85f);
}

color shade_mouth(const hit_record& rec) {
    // Deep dark slit. Just a slightly lighter black than the visor.
    return color(0.005f, 0.005f, 0.008f);
}

color shade_ground(const hit_record& rec) {
    // Dark dance floor. Soft Lambert with the key light.
    vec3 key = unit_vector(vec3(0.4f, 0.8f, 0.5f));
    float kd = std::fmax(0.0f, dot(rec.normal, key));
    return color(0.05f, 0.05f, 0.06f) * (0.4f + 0.8f * kd);
}

// -----------------------------------------------------------------------------
// Lasers / volumetric
// -----------------------------------------------------------------------------

struct laser_beam {
    point3 origin;
    vec3 direction;
    color color;
    float radius;
    float intensity;
};

vec3 closest_point_on_line(const point3& a, const vec3& adir, const point3& p) {
    return a + std::fmax(0.0f, dot(p - a, adir)) * adir;
}

float distance_to_line(const point3& a, const vec3& adir, const point3& p) {
    return (p - closest_point_on_line(a, adir, p)).length();
}

// March along the ray from t_near to t_far. At each step compute the
// in-scattered light from every beam: fog_density * beam radial falloff.
color volumetric_lasers(const ray& r, float t_near, float t_far,
                        const std::vector<laser_beam>& lasers) {
    if (t_far <= t_near) return color(0, 0, 0);
    const int steps = 80;
    float dt = (t_far - t_near) / float(steps);
    color result(0, 0, 0);
    for (int i = 0; i < steps; ++i) {
        float t = t_near + (i + 0.5f) * dt;
        if (t <= 0.0f) continue;
        vec3 p = r.at(t);
        float fog = fog_density(p);
        if (fog <= 0.001f) continue;
        for (const auto& beam : lasers) {
            float d = distance_to_line(beam.origin, beam.direction, p);
            if (d >= beam.radius) continue;
            float radial = 1.0f - d / beam.radius;
            radial = radial * radial;
            float along = std::fmax(0.0f, dot(p - beam.origin, beam.direction));
            float length_falloff = std::exp(-along * 0.04f);
            result += beam.color * (radial * length_falloff * fog * beam.intensity * dt);
        }
    }
    return result;
}

// Surface light from a laser painting the hit point.
color laser_surface_light(const hit_record& rec, const std::vector<laser_beam>& lasers) {
    color result(0, 0, 0);
    for (const auto& beam : lasers) {
        float d = distance_to_line(beam.origin, beam.direction, rec.p);
        if (d >= beam.radius * 1.2f) continue;
        float radial = 1.0f - d / (beam.radius * 1.2f);
        radial = radial * radial;
        result += beam.color * (radial * beam.intensity * 0.12f);
    }
    return result;
}

// -----------------------------------------------------------------------------
// Main ray color
// -----------------------------------------------------------------------------

color ray_color(const ray& r, const std::vector<laser_beam>& lasers) {
    hit_record rec;
    if (hit_world(r, 0.001f, 1000.0f, rec)) {
        color surface;
        switch (rec.material) {
            case 0: surface = shade_gold(rec);   break;
            case 1: surface = shade_chrome(rec); break;
            case 2: surface = shade_visor(rec, unit_vector(r.direction())); break;
            case 3: surface = shade_mouth(rec);  break;
            case 4: surface = shade_ground(rec); break;
            default: surface = color(1, 0, 1);   break;
        }

        color light(0, 0, 0);
        // The visor and mouth slit are opaque dark surfaces in the helmet
        // design; they should not be lit by the lasers (either on the surface
        // or by smoke passing in front of them).
        if (rec.material != 2 && rec.material != 3) {
            light += laser_surface_light(rec, lasers);
            light += volumetric_lasers(r, 0.0f, rec.t, lasers);
        }

        return surface + light;
    }

    // No surface: just sky + volumetric beams.
    vec3 unit = unit_vector(r.direction());
    float a = 0.5f * (unit.y() + 1.0f);
    color sky = (1.0f - a) * color(0.03f, 0.03f, 0.08f) + a * color(0.08f, 0.09f, 0.18f);
    color volumetric = volumetric_lasers(r, 0.0f, 100.0f, lasers);
    return sky + volumetric;
}

} // namespace

int main() {
    auto aspect_ratio = 16.0 / 9.0;
    int image_width = 600;
    int image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    auto focal_length = 1.7f;
    auto viewport_height = 1.4f;
    auto viewport_width = viewport_height * (float(image_width) / image_height);
    auto camera_center = point3(0, 0.15f, 2.0f);

    auto viewport_u = vec3(viewport_width, 0, 0);
    auto viewport_v = vec3(0, -viewport_height, 0);
    auto pixel_delta_u = viewport_u / image_width;
    auto pixel_delta_v = viewport_v / image_height;
    auto viewport_upper_left = camera_center - vec3(0, 0, focal_length) - viewport_u / 2 - viewport_v / 2;
    auto pixel00_loc = viewport_upper_left + 0.5f * (pixel_delta_u + pixel_delta_v);

    // Lasers: a smoke-show spread. Mix of cross-cutting horizontals and
    // diagonals, all converging roughly on the helmet area.
    std::vector<laser_beam> lasers = {
        { point3(-4.0f,  0.2f, -0.5f), unit_vector(vec3( 1.0f, -0.05f, -0.2f)), color(1.0f, 0.10f, 0.25f), 0.30f, 12.0f },
        { point3( 4.0f,  0.3f, -0.5f), unit_vector(vec3(-1.0f, -0.05f, -0.2f)), color(0.10f, 0.50f, 1.0f), 0.30f, 12.0f },
        { point3(-3.0f,  1.5f, -2.5f), unit_vector(vec3( 0.6f, -0.30f,  0.6f)), color(0.20f, 1.00f, 0.50f), 0.25f, 8.0f },
        { point3( 3.0f,  1.4f, -2.5f), unit_vector(vec3(-0.6f, -0.30f,  0.6f)), color(1.00f, 0.20f, 1.00f), 0.25f, 8.0f },
        { point3( 0.0f,  2.0f, -3.0f), unit_vector(vec3( 0.05f,-0.50f, 1.0f)), color(0.10f, 0.95f, 0.80f), 0.30f, 8.0f },
    };

    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    for (int j = 0; j < image_height; j++) {
        std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
        for (int i = 0; i < image_width; i++) {
            auto pixel_center = pixel00_loc + (i * pixel_delta_u) + (j * pixel_delta_v);
            auto ray_direction = pixel_center - camera_center;
            ray r(camera_center, ray_direction);
            color pixel_color = ray_color(r, lasers);
            // Reinhard tonemap.
            color mapped(
                pixel_color.x() / (1.0f + pixel_color.x()),
                pixel_color.y() / (1.0f + pixel_color.y()),
                pixel_color.z() / (1.0f + pixel_color.z()));
            mapped = mapped * 1.5f;
            write_color(std::cout, mapped);
        }
    }

    std::clog << "\rDone.                 \n";
}
