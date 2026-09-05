#version 330 core

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCamera;
uniform vec3 uTarget;
uniform int uQuality;

out vec4 fragColor;

const float PI = 3.141592653589793;
const float FAR = 14.0;
const int GOLD = 0, CHROME = 1, GLASS = 2, BLACK = 3, FLOOR = 4, LED = 5;

struct Ellipsoid {
    vec3 center;
    vec3 scale;
    int material;
    bool shell;
};

// Branch selection avoids a constant-struct-array compiler stall on Apple's GL driver.
Ellipsoid model(int i) {
    if (i == 0) return Ellipsoid(vec3(0.0, 0.1, -1.3), vec3(0.87, 1.06, 0.76), GOLD, true);
    if (i == 1) return Ellipsoid(vec3(-0.9, 0.03, -1.32), vec3(0.19, 0.39, 0.42), GOLD, false);
    if (i == 2) return Ellipsoid(vec3(0.9, 0.03, -1.32), vec3(0.19, 0.39, 0.42), CHROME, false);
    if (i == 3) return Ellipsoid(vec3(-0.94, 0.03, -1.01), vec3(0.145, 0.28, 0.13), BLACK, false);
    if (i == 4) return Ellipsoid(vec3(0.94, 0.03, -1.01), vec3(0.145, 0.28, 0.13), CHROME, false);
    if (i == 5) return Ellipsoid(vec3(0.0, -0.71, -1.23), vec3(0.66, 0.23, 0.6), GOLD, true);
    if (i == 6) return Ellipsoid(vec3(0.0, -1.02, -1.38), vec3(0.37, 0.36, 0.38), BLACK, false);
    return Ellipsoid(vec3(0.0, -1.56, -1.46), vec3(1.22, 0.53, 0.57), BLACK, false);
}

struct Hit {
    float t;
    vec3 p;
    vec3 normal;
    int material;
};

vec3 fogOffset;
vec3 beamDirections[14];

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float d2 = dot(v, v);
    return d2 > 1e-12 ? v * inversesqrt(max(d2, 1e-12)) : fallback;
}

float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash(cell), hash(cell + vec3(1, 0, 0)), f.x),
            mix(hash(cell + vec3(0, 1, 0)), hash(cell + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash(cell + vec3(0, 0, 1)), hash(cell + vec3(1, 0, 1)), f.x),
            mix(hash(cell + vec3(0, 1, 1)), hash(cell + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fogDensity(vec3 p) {
    p += fogOffset;
    float billow = valueNoise(p * 1.6);
    float detail = valueNoise(p * 4.2);
    float ground = exp(-max(p.y + 1.25, 0.0) * 1.3);
    float cloud = smoothstep(0.40, 0.72, billow + detail * 0.22);
    float z = (p.z + 0.1) * 0.85;
    float foreground = exp(-z * z);
    return 0.012 + ground * (0.025 + cloud * (0.28 + 2.4 * foreground));
}

Hit hitWorld(vec3 origin, vec3 direction, bool includeFloor) {
    Hit hit = Hit(FAR, origin + direction * FAR, vec3(0, 1, 0), -1);
    int primitive = -1;
    for (int i = 0; i < 8; ++i) {
        Ellipsoid e = model(i);
        vec3 invScale = 1.0 / e.scale;
        vec3 o = (origin - e.center) * invScale;
        vec3 d = direction * invScale;
        float a = dot(d, d);
        float b = dot(o, d);
        float c = dot(o, o) - 1.0;
        float discriminant = b * b - a * c;
        if (discriminant < 0.0) continue;
        float root = sqrt(max(discriminant, 0.0));
        float t = (-b - root) / a;
        if (t < 0.002) t = (-b + root) / a;
        if (t < 0.002 || t >= hit.t) continue;
        hit.t = t;
        primitive = i;
    }
    if (primitive >= 0) {
        Ellipsoid e = model(primitive);
        hit.p = origin + hit.t * direction;
        hit.normal = safeNormalize((hit.p - e.center) / (e.scale * e.scale), vec3(0, 1, 0));
        hit.material = e.material;
        if (e.shell) {
            float x = hit.p.x, y = hit.p.y;
            hit.material = x < 0.0 ? GOLD : CHROME;
            if (hit.normal.z > 0.38) {
                float top = x < 0.0 ? 0.53 - 0.18 * x * x : 0.32 - 0.12 * x;
                float bottom = x < 0.0 ? -0.49 + 0.30 * x * x : -0.025 - 0.06 * x;
                if (y > bottom && y < top) hit.material = GLASS;
                if (abs(x) < 0.012) hit.material = BLACK;
                if (x > 0.09 && x < 0.55 && y > -0.55 && y < -0.43) {
                    hit.material = mod(x - 0.09, 0.075) < 0.012 ? CHROME : BLACK;
                }
                if (x < -0.58 && x > -0.76 && y > -0.35 && y < 0.34 &&
                    mod(y + 0.35, 0.095) < 0.025) hit.material = LED;
            }
        }
    }
    if (includeFloor && abs(direction.y) > 1e-6) {
        float t = (-1.62 - origin.y) / direction.y;
        if (t > 0.002 && t < hit.t) {
            hit = Hit(t, origin + t * direction, vec3(0, 1, 0), FLOOR);
        }
    }
    return hit;
}

float stripLight(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return exp(-x4 * x4);
}

vec3 environment(vec3 d) {
    vec3 c = vec3(0.018, 0.025, 0.05) * (0.4 + 0.6 * clamp(d.y, 0.0, 1.0));
    float azimuth = dot(d.xz, d.xz) > 1e-12 ? atan(d.x, d.z) : 0.0;
    float strip = stripLight((azimuth + 0.72) / 0.16);
    float strip2 = stripLight((azimuth - 0.92) / 0.10);
    float height = smoothstep(-0.45, -0.2, d.y) * (1.0 - smoothstep(0.65, 0.85, d.y));
    c += height * (strip * vec3(4.0, 3.3, 2.3) + strip2 * vec3(2.5, 3.6, 4.5));
    c += vec3(1.4, 1.65, 2.0) * smoothstep(0.58, 0.72, d.y);
    c += vec3(0.04, 0.28, 0.48) * pow(clamp(d.x, 0.0, 1.0), 6.0);
    c += vec3(0.4, 0.055, 0.012) * pow(clamp(-d.x, 0.0, 1.0), 6.0);
    return c;
}

vec3 shade(Hit hit, vec3 view) {
    if (hit.material < 0) return vec3(0.003, 0.005, 0.013);
    if (hit.material == FLOOR) return vec3(0.008, 0.012, 0.02);
    vec3 env = environment(reflect(view, hit.normal));
    float facing = clamp(dot(-view, hit.normal), 0.0, 1.0);
    float fresnel = pow(1.0 - facing, 5.0);
    float key = max(dot(hit.normal, normalize(vec3(-0.5, 0.8, 1.0))), 0.0);
    if (hit.material == GOLD)
        return vec3(1.0, 0.55, 0.12) * (env * 0.72 + vec3(0.12, 0.10, 0.065) * key);
    if (hit.material == CHROME)
        return env * vec3(0.88, 0.94, 1.0) + vec3(0.055, 0.065, 0.08) * key;
    if (hit.material == GLASS)
        return vec3(0.004, 0.009, 0.016) + env * (0.065 + fresnel * 0.65);
    if (hit.material == LED) return vec3(0.06, 1.4, 1.8);
    return vec3(0.008, 0.011, 0.017) * (0.4 + key) + env * (0.018 + 0.12 * fresnel);
}

// Abramowitz-Stegun erf approximation in complementary form preserves the tails.
float erfcPositive(float x) {
    float t = 1.0 / (1.0 + 0.3275911 * x);
    return t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 +
        t * (-1.453152027 + t * 1.061405429)))) * exp(-x * x);
}

float erfInterval(float lo, float hi) {
    if (lo >= 0.0) return max(erfcPositive(lo) - erfcPositive(hi), 0.0);
    if (hi <= 0.0) return max(erfcPositive(-hi) - erfcPositive(-lo), 0.0);
    return max(2.0 - erfcPositive(-lo) - erfcPositive(hi), 0.0);
}

vec3 laserScattering(vec3 origin, vec3 direction, float rayLength) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < 14; ++i) {
        float side = i < 7 ? -1.0 : 1.0;
        vec3 beamOrigin = vec3(side * 2.65, -1.3, -3.2);
        vec3 beamDirection = beamDirections[i];
        vec3 tint = i < 7 ? vec3(1.0, 0.11, 0.025) : vec3(0.015, 0.48, 1.0);
        vec3 w = origin - beamOrigin;
        float dv = dot(direction, beamDirection);
        float along = dot(w, beamDirection);
        vec3 perpendicular = direction - dv * beamDirection;
        vec3 offset = w - along * beamDirection;
        float a = dot(perpendicular, perpendicular);
        float lo = 0.0, hi = rayLength;
        // All beams point up and behind the model, so their nine-unit paths are unblocked.
        if (abs(dv) > 1e-6) {
            float t0 = -along / dv, t1 = (9.0 - along) / dv;
            lo = max(lo, min(t0, t1));
            hi = min(hi, max(t0, t1));
        } else if (along < 0.0 || along > 9.0) {
            continue;
        }
        if (hi <= lo) continue;
        bool parallel = a <= 1e-10;
        float t = parallel ? 0.5 * (lo + hi) : -dot(offset, perpendicular) / max(a, 1e-10);
        float sampleT = clamp(t, lo, hi);
        vec3 closest = offset + sampleT * perpendicular;
        if (dot(closest, closest) > 0.08) continue;
        vec3 lineClosest = offset + t * perpendicular;
        float d2 = max(dot(lineClosest, lineClosest), 0.0);
        float fog = fogDensity(origin + sampleT * direction);
        float attenuation = exp(-0.055 * sampleT);
        // Closed-form core and halo integrals cannot step over a thin laser.
        for (int layer = 0; layer < 2; ++layer) {
            float radius = layer == 0 ? 0.016 : 0.085;
            float strength = layer == 0 ? 850.0 : 12.0;
            float integral;
            if (parallel) {
                integral = (hi - lo) * exp(-d2 / (radius * radius));
            } else {
                float scale = sqrt(max(a, 1e-10)) / radius;
                integral = exp(-d2 / (radius * radius)) * (sqrt(PI) / (2.0 * scale)) *
                    erfInterval((lo - t) * scale, (hi - t) * scale);
            }
            result += tint * integral * fog * strength * attenuation;
        }
    }
    return result;
}

vec3 applyVolume(vec3 surface, vec3 origin, vec3 direction, float rayLength, int steps) {
    float dt = rayLength / float(steps);
    float transmission = 1.0;
    vec3 smoke = vec3(0.0);
    for (int i = 0; i < 32; ++i) {
        if (i >= steps) break;
        vec3 p = origin + (float(i) + 0.5) * dt * direction;
        float absorbed = 1.0 - exp(-fogDensity(p) * dt);
        vec3 leftDelta = p - vec3(-2.8, -0.6, -2.5);
        vec3 rightDelta = p - vec3(2.8, -0.6, -2.5);
        float left = 1.0 / (1.0 + dot(leftDelta, leftDelta) * 0.6);
        float right = 1.0 / (1.0 + dot(rightDelta, rightDelta) * 0.6);
        vec3 lighting = vec3(0.018, 0.028, 0.06) + left * vec3(0.48, 0.055, 0.015) +
            right * vec3(0.015, 0.18, 0.48);
        smoke += transmission * absorbed * lighting;
        transmission *= 1.0 - absorbed;
    }
    return surface * transmission + smoke + laserScattering(origin, direction, rayLength);
}

vec3 displayColor(vec3 color) {
    color = clamp(color, vec3(0.0), vec3(1e4));
    vec3 mapped = clamp((color * (2.51 * color + 0.03)) /
        (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
    vec3 srgb = mix(1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
        12.92 * mapped, lessThanEqual(mapped, vec3(0.0031308)));
    return clamp(srgb, 0.0, 1.0);
}

void main() {
    float phase = mod(uTime, 24.0) * (2.0 * PI / 24.0);
    fogOffset = vec3(0.72 * sin(phase), 0.0, 0.36 * (1.0 - cos(phase)));
    for (int i = 0; i < 14; ++i) {
        float side = i < 7 ? -1.0 : 1.0;
        float fan = float(i % 7);
        float sweep = sin(2.0 * phase + fan * 0.18) * 0.14;
        beamDirections[i] = safeNormalize(vec3(-side * (0.3 + fan * 0.24),
            1.1 + sweep, -0.12), vec3(0, 1, 0));
    }

    vec2 resolution = max(uResolution, vec2(1.0));
    vec2 screen = (2.0 * gl_FragCoord.xy - resolution) / resolution.y;
    vec3 forward = safeNormalize(uTarget - uCamera, vec3(0, 0, -1));
    vec3 worldUp = abs(forward.y) > 0.999 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 right = safeNormalize(cross(forward, worldUp), vec3(1, 0, 0));
    vec3 up = safeNormalize(cross(right, forward), vec3(0, 1, 0));
    vec3 direction = safeNormalize(forward + 0.32 * (screen.x * right + screen.y * up), forward);
    int steps = uQuality > 0 ? 32 : 16;

    Hit hit = hitWorld(uCamera, direction, true);
    vec3 surface = shade(hit, direction);
    if (hit.material == FLOOR) {
        // One explicit secondary ray; its hit cannot spawn another reflection.
        vec3 reflectedOrigin = hit.p + vec3(0, 0.003, 0);
        vec3 reflectedDirection = reflect(direction, hit.normal);
        Hit reflectedHit = hitWorld(reflectedOrigin, reflectedDirection, false);
        vec3 reflected = applyVolume(shade(reflectedHit, reflectedDirection),
            reflectedOrigin, reflectedDirection, reflectedHit.t, steps / 2);
        float fresnel = 0.22 + 0.5 * pow(1.0 - clamp(-direction.y, 0.0, 1.0), 5.0);
        surface += reflected * fresnel;
        vec2 contactOffset = hit.p.xz - vec2(0.0, -1.4);
        surface *= 1.0 - 0.75 * exp(-dot(contactOffset, contactOffset) * 1.5);
    }
    vec3 color = applyVolume(surface, uCamera, direction, hit.t, steps);
    fragColor = vec4(displayColor(color), 1.0);
}
