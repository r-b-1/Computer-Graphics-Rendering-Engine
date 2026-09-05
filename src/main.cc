#include "color.h"
#include "ray.h"
#include "vec3.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace {

// -----------------------------------------------------------------------------
// Math helpers
// -----------------------------------------------------------------------------

vec3 reflect(const vec3& v, const vec3& n) {
    return v - 2.0f * dot(v, n) * n;
}

vec3 refract(const vec3& uv, const vec3& n, float etai_over_etat) {
    auto cos_theta = std::fmin(dot(-uv, n), 1.0f);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -std::sqrt(std::fmax(0.0f, 1.0f - r_out_perp.length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

// Schlick approximation for Fresnel reflectance.
float schlick(float cosine, float ref_idx) {
    auto r0 = (1.0f - ref_idx) / (1.0f + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * std::pow((1.0f - cosine), 5.0f);
}

// Cheap procedural noise (hash-based) for fog. Not pretty, but enough
// to make the smoke show feel cloudy.
float hash13(vec3 p) {
    p = vec3(
        std::fmod(std::sin(dot(p, vec3(127.1f, 311.7f, 74.7f))) * 43758.5453f, 1.0f),
        std::fmod(std::sin(dot(p, vec3(269.5f, 183.3f, 246.1f))) * 43758.5453f, 1.0f),
        std::fmod(std::sin(dot(p, vec3(113.5f, 271.9f, 124.6f))) * 43758.5453f, 1.0f));
    return std::fmod(p.x() + p.y() + p.z(), 1.0f);
}

float fog_density(const vec3& p) {
    // Layered low-frequency "smoke". A few sin fields plus a hash jitter
    // give the volumetric beams something to scatter through.
    float n = 0.0f;
    n += 0.50f * std::sin(0.9f * p.x() + 0.4f * p.y());
    n += 0.30f * std::sin(1.3f * p.y() - 0.7f * p.z());
    n += 0.20f * std::sin(0.6f * p.z() + 1.1f * p.x());
    n += 0.15f * (hash13(p * 3.7f) - 0.5f);
    n = (n + 1.0f) * 0.5f; // [0,1]
    return std::fmax(0.0f, n - 0.05f); // keep the room filled with haze
}

// -----------------------------------------------------------------------------
// Scene primitives
// -----------------------------------------------------------------------------

struct hit_record {
    float t;
    vec3 p;
    vec3 normal;
    int material; // 0 = left helmet (gold), 1 = right helmet (chrome), 2 = visor, 3 = ground
};

bool hit_sphere(const point3& center, float radius, const ray& r, float t_min, float t_max, hit_record& rec, int material) {
    vec3 oc = r.origin() - center;
    auto a = r.direction().length_squared();
    auto h = dot(r.direction(), oc);
    auto c = oc.length_squared() - radius * radius;
    auto discriminant = h * h - a * c;
    if (discriminant < 0) return false;
    auto sqd = std::sqrt(discriminant);
    auto root = (-h - sqd) / a;
    if (root < t_min || root > t_max) {
        root = (-h + sqd) / a;
        if (root < t_min || root > t_max) return false;
    }
    rec.t = root;
    rec.p = r.at(root);
    rec.normal = (rec.p - center) / radius;
    rec.material = material;
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

// Intersect a slightly squashed sphere (ellipsoid) for the helmet.
// We solve the quadratic in the ray's own parameter and pick the nearest root.
bool hit_helmet(const ray& r, float t_min, float t_max, hit_record& rec) {
    point3 center(0, 0, -1.2f);
    // Squash the ray's Y component (taller than wide is wrong; we want wider, so
    // we scale the ray's y component to 0.95 of a perfect sphere).
    ray rs(r.origin() * vec3(1, 1.0f/0.95f, 1), r.direction() * vec3(1, 1.0f/0.95f, 1));
    float radius = 0.85f;
    vec3 oc = rs.origin() - center;
    auto a = rs.direction().length_squared();
    auto h = dot(rs.direction(), oc);
    auto c = oc.length_squared() - radius * radius;
    auto discriminant = h * h - a * c;
    if (discriminant < 0) return false;
    auto sqd = std::sqrt(discriminant);
    auto root = (-h - sqd) / a;
    if (root < t_min || root > t_max) {
        root = (-h + sqd) / a;
        if (root < t_min || root > t_max) return false;
    }

    rec.t = root;
    rec.p = r.at(root);
    // Ellipsoid normal: transform the sphere normal back through the same
    // anisotropic scale, then normalize.
    vec3 sphere_normal = (rec.p - center) * vec3(1, 0.95f, 1);
    rec.normal = unit_vector(sphere_normal);

    if (rec.p.x() < 0) {
        rec.material = 0; // gold (Guy-Manuel)
    } else if (rec.p.x() > 0) {
        rec.material = 1; // chrome (Thomas)
    } else {
        rec.material = 0; // seam -> gold
    }
    return true;
}

// Visor: a flat horizontal band on the front of the helmet. Implemented as
// a thin Y-slab intersected with a circular front face.
bool hit_visor(const ray& r, float t_min, float t_max, hit_record& rec) {
    // Slight downward offset so the band sits where the eyes would be.
    float visor_y = -0.05f;
    float visor_z = -0.40f;
    float visor_r = 0.22f;
    float half_thick = 0.025f;

    auto dy = r.direction().y();
    if (std::fabs(dy) < 1e-6f) return false;
    auto t_top = ((visor_y + half_thick) - r.origin().y()) / dy;
    auto t_bot = ((visor_y - half_thick) - r.origin().y()) / dy;
    auto t_enter = std::fmin(t_top, t_bot);
    auto t_exit  = std::fmax(t_top, t_bot);
    if (t_exit < t_min || t_enter > t_max) return false;
    auto t = (t_enter < t_min) ? t_min : t_enter;
    if (t < t_min || t > t_max) return false;

    auto p = r.at(t);
    auto dxz = vec3(p.x(), 0, p.z() - visor_z);
    if (dxz.length_squared() > visor_r * visor_r) return false;

    // Only keep the front-facing half (closer to camera than visor center).
    if (p.z() < visor_z) return false;

    rec.t = t;
    rec.p = p;
    rec.normal = vec3(0, 0, 1);
    rec.material = 2;
    return true;
}

bool hit_ground(const ray& r, float t_min, float t_max, hit_record& rec) {
    return hit_plane(point3(0, -1.05f, 0), vec3(0, 1, 0), t_min, t_max, r, rec, 3);
}

bool hit_world(const ray& r, float t_min, float t_max, hit_record& rec) {
    bool hit_anything = false;
    float closest = t_max;
    hit_record tmp;
    if (hit_helmet(r, t_min, closest, tmp))           { hit_anything = true; closest = tmp.t; rec = tmp; }
    if (hit_visor(r,  t_min, closest, tmp))           { hit_anything = true; closest = tmp.t; rec = tmp; }
    if (hit_ground(r, t_min, closest, tmp))           { hit_anything = true; closest = tmp.t; rec = tmp; }
    return hit_anything;
}

// -----------------------------------------------------------------------------
// Materials
// -----------------------------------------------------------------------------

// Diffuse + tinted ambient. Stable random-in-unit-sphere for soft shadows
// would be nice, but the show works without full path tracing.
color shade_lambert(const hit_record& rec, const vec3& base, const vec3& light_dir) {
    float ndotl = std::fmax(0.0f, dot(rec.normal, light_dir));
    vec3 ambient = 0.35f * base;
    vec3 diffuse = ndotl * base * 1.4f;
    return ambient + diffuse;
}

// Cheap "metallic" look: diffuse base plus a few specular highlights.
// The base color carries the gold vs chrome identity; the highlights
// just give the helmet shape.
color shade_metal(const hit_record& rec, const vec3& tint) {
    // Diffuse-ish term: a soft top light so the helmet has obvious shading.
    vec3 key_light = unit_vector(vec3(0.4f, 0.8f, 0.5f));
    vec3 fill_light = unit_vector(vec3(-0.6f, 0.3f, 0.5f));
    float kd_key  = std::fmax(0.0f, dot(rec.normal, key_light));
    float kd_fill = std::fmax(0.0f, dot(rec.normal, fill_light));

    // A couple of bright specular highlights for the "polished metal" look.
    vec3 spec_lights[3] = {
        unit_vector(vec3( 0.5f,  0.7f,  0.5f)),
        unit_vector(vec3(-0.4f,  0.4f,  0.6f)),
        unit_vector(vec3( 0.0f, -0.2f,  1.0f)),
    };
    color spec_cols[3] = {
        color(1.0f, 0.95f, 0.85f),
        color(0.85f, 0.90f, 1.0f),
        color(1.0f, 0.90f, 0.95f),
    };
    color spec_accum(0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        vec3 r = reflect(-spec_lights[i], rec.normal);
        float spec = std::pow(std::fmax(0.0f, r.z()), 16.0f);
        spec_accum += tint * spec_cols[i] * spec * 1.4f;
    }

    color base = tint * (0.30f + 1.10f * kd_key + 0.45f * kd_fill);
    return base + spec_accum;
}

color shade_gold(const hit_record& rec) {
    return shade_metal(rec, color(1.00f, 0.78f, 0.34f));
}

color shade_chrome(const hit_record& rec) {
    return shade_metal(rec, color(0.85f, 0.88f, 0.95f));
}

color shade_visor(const hit_record& rec, const vec3& view_dir) {
    // Glossy black visor with a thin specular streak along the top.
    vec3 light_dir = unit_vector(vec3(0.4f, 0.6f, 0.7f));
    float spec = std::pow(std::fmax(0.0f, dot(reflect(-light_dir, rec.normal), -view_dir)), 60.0f);
    color base(0.02f, 0.02f, 0.03f);
    return base + spec * color(0.6f, 0.7f, 0.9f);
}

color shade_ground(const hit_record& rec) {
    // Dark dance floor that catches a little light.
    vec3 light_dir = unit_vector(vec3(0.4f, 0.6f, -0.7f));
    return shade_lambert(rec, color(0.08f, 0.08f, 0.10f), light_dir);
}

// -----------------------------------------------------------------------------
// Lasers
// -----------------------------------------------------------------------------

struct laser_beam {
    point3 origin;
    vec3 direction;
    color color;
    float radius;   // beam radius in world units
    float intensity;
};

// A small fixed light direction used everywhere for cheap shading.
const vec3 kKeyLight = unit_vector(vec3(0.4f, 0.6f, -0.7f));

// Distance from point p to the infinite line defined by (origin, direction).
float distance_to_line(const point3& origin, const vec3& direction, const point3& p) {
    return cross(p - origin, unit_vector(direction)).length();
}

// For a given beam, compute the closest approach between the camera ray
// and the beam's line. Returns the param t on the camera ray, the distance
// from the camera ray to the beam line at that t, and the param s on the
// beam line.
void closest_approach_ray_line(const ray& r, const point3& b0, const vec3& bdir,
                               float& t_ray, float& d_min, float& s_beam) {
    // Minimize ||r.origin + t*r.dir - (b0 + s*bdir)||^2 over t, s.
    vec3 w0 = r.origin() - b0;
    float a = dot(r.direction(), r.direction());
    float b = dot(r.direction(), bdir);
    float c = dot(bdir, bdir);
    float d = dot(r.direction(), w0);
    float e = dot(bdir, w0);
    float denom = a * c - b * b;
    if (std::fabs(denom) < 1e-6f) {
        t_ray = 0; s_beam = 0; d_min = w0.length();
        return;
    }
    t_ray = (b * e - c * d) / denom;
    s_beam = (a * e - b * d) / denom;
    vec3 closest = w0 + t_ray * r.direction() - s_beam * bdir;
    d_min = closest.length();
}

// Add volumetric laser light for a single beam. The closest approach
// to the camera ray determines how much the beam contributes; the beam
// fades with the distance to that closest point.
color sample_beam(const ray& r, float t_near, float t_far, const laser_beam& beam) {
    float t_ray, d_min, s_beam;
    closest_approach_ray_line(r, beam.origin, beam.direction, t_ray, d_min, s_beam);
    if (d_min >= beam.radius) return color(0, 0, 0);
    if (t_ray < t_near || t_ray > t_far) return color(0, 0, 0);

    // Radial falloff across the beam.
    float radial = 1.0f - d_min / beam.radius;
    radial = radial * radial;

    // Distance from the beam origin to the closest point: how much the beam
    // has already "burned down" by the time the ray crosses it.
    vec3 closest_pt = beam.origin + s_beam * beam.direction;
    float along = std::fmax(0.0f, s_beam);
    float length_falloff = std::exp(-along * 0.05f);

    // Length of camera-ray segment within the beam's radius: makes wide beams
    // look brighter than skinny ones.
    float half_chord = std::sqrt(std::fmax(0.0f, beam.radius * beam.radius - d_min * d_min));
    float chord = 2.0f * half_chord;

    return beam.color * (radial * length_falloff * beam.intensity * chord);
}

color accumulate_lasers(const ray& r, float t_near, float t_far,
                        const std::vector<laser_beam>& lasers) {
    color result(0, 0, 0);
    for (const auto& beam : lasers) {
        result += sample_beam(r, t_near, t_far, beam);
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
            case 3: surface = shade_ground(rec); break;
            default: surface = color(1, 0, 1);   break; // shouldn't happen
        }
        // Lasers also light the surface directly where the beam crosses it.
        // Bias the lit color toward the surface color so the beams look like
        // they are painting the helmet, not punching holes through it.
        color laser_light(0, 0, 0);
        for (const auto& beam : lasers) {
            float d = distance_to_line(beam.origin, beam.direction, rec.p);
            if (d < beam.radius * 2.0f) {
                float radial = std::fmax(0.0f, 1.0f - d / (beam.radius * 2.0f));
                radial = radial * radial;
                color painted = 0.5f * surface + 0.5f * beam.color;
                laser_light += painted * (radial * beam.intensity * 0.35f);
            }
        }
        // Volumetric beams between camera and surface.
        color volumetric = accumulate_lasers(r, 0.0f, rec.t, lasers);
        return surface + laser_light + volumetric;
    }

    // Sky / no-hit path. The lasers still cut through the air here.
    vec3 unit = unit_vector(r.direction());
    float a = 0.5f * (unit.y() + 1.0f);
    color sky = (1.0f - a) * color(0.04f, 0.04f, 0.10f) + a * color(0.10f, 0.12f, 0.22f);
    color volumetric = accumulate_lasers(r, 0.0f, 100.0f, lasers);
    return sky + volumetric;
}

} // namespace

int main() {
    // Image
    auto aspect_ratio = 16.0 / 9.0;
    int image_width = 800;
    int image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    // Camera
    auto focal_length = 1.8f;
    auto viewport_height = 2.0f;
    auto viewport_width = viewport_height * (float(image_width) / image_height);
    auto camera_center = point3(0, 0.05f, 2.6f);

    auto viewport_u = vec3(viewport_width, 0, 0);
    auto viewport_v = vec3(0, -viewport_height, 0);
    auto pixel_delta_u = viewport_u / image_width;
    auto pixel_delta_v = viewport_v / image_height;
    auto viewport_upper_left = camera_center - vec3(0, 0, focal_length) - viewport_u / 2 - viewport_v / 2;
    auto pixel00_loc = viewport_upper_left + 0.5f * (pixel_delta_u + pixel_delta_v);

    // Lasers: a few cross-cutting beams for the smoke-show effect.
    std::vector<laser_beam> lasers = {
        { point3(-3.5f,  0.0f, -2.0f), unit_vector(vec3( 1.0f, -0.05f,  0.4f)), color(1.0f, 0.10f, 0.20f), 0.20f, 2.5f },
        { point3( 3.5f,  0.2f, -2.0f), unit_vector(vec3(-1.0f, -0.10f,  0.4f)), color(0.10f, 0.45f, 1.0f), 0.20f, 2.5f },
        { point3( 0.0f,  1.8f, -3.5f), unit_vector(vec3( 0.05f, -0.60f, 1.0f)), color(0.10f, 0.95f, 0.70f), 0.18f, 2.0f },
    };

    // Render
    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    for (int j = 0; j < image_height; j++) {
        std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
        for (int i = 0; i < image_width; i++) {
            auto pixel_center = pixel00_loc + (i * pixel_delta_u) + (j * pixel_delta_v);
            auto ray_direction = pixel_center - camera_center;
            ray r(camera_center, ray_direction);
            color pixel_color = ray_color(r, lasers);
            // Simple Reinhard tonemap so highlights don't clip to white.
            color mapped(
                pixel_color.x() / (1.0f + pixel_color.x()),
                pixel_color.y() / (1.0f + pixel_color.y()),
                pixel_color.z() / (1.0f + pixel_color.z()));
            // Mild gamma and exposure bump.
            mapped = mapped * 1.6f;
            write_color(std::cout, mapped);
        }
    }

    std::clog << "\rDone.                 \n";
}
