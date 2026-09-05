#define GLFW_INCLUDE_NONE
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

#include "live_shader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr const char* controls =
    "Drag / arrows: orbit | Scroll: zoom | Space: pause | A: auto-orbit | R: reset | Q: quality | Esc: quit";

struct Viewer {
    float yaw = 0, pitch = 0.0430768f, distance = 5.80539f;
    double time = 0, mouse_x = 0, mouse_y = 0;
    bool paused = false, orbit = false, dragging = false;
    int quality = 0;

    void clamp_camera() {
        pitch = std::clamp(pitch, -0.12f, 1.35f);
        distance = std::clamp(distance, 2.3f, 10.0f);
        yaw = std::remainder(yaw, 6.2831853f);
    }

    std::array<float, 3> camera() const {
        return {distance * std::cos(pitch) * std::sin(yaw),
                0.1f + distance * std::sin(pitch),
                -1.3f + distance * std::cos(pitch) * std::cos(yaw)};
    }
};

Viewer& state(GLFWwindow* window) {
    return *static_cast<Viewer*>(glfwGetWindowUserPointer(window));
}

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(std::max(size, 1), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed: " + log);
    }
    return shader;
}

std::vector<float> capture(int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL render/readback error");
    for (float value : pixels)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Invalid framebuffer value");
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        if (pixels[i] != 1) throw std::runtime_error("Incomplete framebuffer coverage");
    return pixels;
}
} // namespace

int main(int argc, char** argv) {
    GLFWwindow* window = nullptr;
    try {
        bool smoke_test = false;
        std::string snapshot;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Daft Punk live viewer: a seamless 24-second show.\n" << controls
                          << "\nOptions: --smoke-test, --snapshot image.ppm, --help\n";
                return 0;
            }
            if (arg == "--smoke-test") smoke_test = true;
            else if (arg == "--snapshot" && i + 1 < argc) snapshot = argv[++i];
            else throw std::runtime_error("Unknown option or missing value: " + arg);
        }
        glfwSetErrorCallback([](int code, const char* message) {
            std::cerr << "GLFW " << code << ": " << message << '\n';
        });
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; a graphical desktop is required");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, smoke_test || !snapshot.empty() ? GLFW_FALSE : GLFW_TRUE);
        window = glfwCreateWindow(smoke_test ? 320 : 960, smoke_test ? 180 : 540,
                                  "Daft Punk Live - starting GPU renderer", nullptr, nullptr);
        if (!window) throw std::runtime_error("Cannot create an OpenGL 3.3 window");
        glfwMakeContextCurrent(window);
#ifndef __APPLE__
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) throw std::runtime_error("Cannot initialize OpenGL entry points");
        while (glGetError() != GL_NO_ERROR) {}
#endif
        glfwSwapInterval(smoke_test || !snapshot.empty() ? 0 : 1);
        glDisable(GL_FRAMEBUFFER_SRGB);
        const char* vertex_source = R"GLSL(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";
        GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
        GLuint fragment = compile(GL_FRAGMENT_SHADER, scene_shader);
        GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint size = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
            std::string log(std::max(size, 1), '\0');
            glGetProgramInfoLog(program, size, nullptr, log.data());
            throw std::runtime_error("Shader link failed: " + log);
        }
        glUseProgram(program);
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        GLint resolution = glGetUniformLocation(program, "uResolution");
        GLint time = glGetUniformLocation(program, "uTime");
        GLint camera = glGetUniformLocation(program, "uCamera");
        GLint target = glGetUniformLocation(program, "uTarget");
        GLint quality = glGetUniformLocation(program, "uQuality");
        if (resolution < 0 || time < 0 || camera < 0 || target < 0 || quality < 0)
            throw std::runtime_error("Missing scene shader uniforms");
        Viewer viewer;
        glfwSetWindowUserPointer(window, &viewer);
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
            if (button != GLFW_MOUSE_BUTTON_LEFT) return;
            auto& v = state(w);
            v.dragging = action == GLFW_PRESS;
            if (v.dragging) v.orbit = false;
            glfwGetCursorPos(w, &v.mouse_x, &v.mouse_y);
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
            auto& v = state(w);
            if (v.dragging) {
                v.yaw -= static_cast<float>(x - v.mouse_x) * 0.006f;
                v.pitch += static_cast<float>(y - v.mouse_y) * 0.006f;
                v.clamp_camera();
            }
            v.mouse_x = x;
            v.mouse_y = y;
        });
        glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) {
            if (!focused) state(w).dragging = false;
        });
        glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
            auto& v = state(w);
            v.distance *= std::exp(-static_cast<float>(std::clamp(y, -10.0, 10.0)) * 0.09f);
            v.clamp_camera();
        });
        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
            if (action != GLFW_PRESS) return;
            auto& v = state(w);
            if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
            if (key == GLFW_KEY_SPACE) v.paused = !v.paused;
            if (key == GLFW_KEY_A) v.orbit = !v.orbit;
            if (key == GLFW_KEY_Q) v.quality = 1 - v.quality;
            if (key == GLFW_KEY_R) v = Viewer{};
        });
        auto draw = [&](int width, int height) {
            glViewport(0, 0, width, height);
            glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
            glUniform1f(time, static_cast<float>(viewer.time));
            auto eye = viewer.camera();
            glUniform3fv(camera, 1, eye.data());
            glUniform3f(target, 0, 0.1f, -1.3f);
            glUniform1i(quality, viewer.quality);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };
        std::cout << controls << '\n';
        if (smoke_test) {
            auto key = glfwSetKeyCallback(window, nullptr);
            glfwSetKeyCallback(window, key);
            key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
            if (!viewer.paused) throw std::runtime_error("Pause control failed");
            key(window, GLFW_KEY_A, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_Q, 0, GLFW_PRESS, 0);
            if (!viewer.orbit || viewer.quality != 1) throw std::runtime_error("Orbit/quality controls failed");
            auto cursor = glfwSetCursorPosCallback(window, nullptr);
            glfwSetCursorPosCallback(window, cursor);
            viewer.dragging = true;
            cursor(window, 80, 40);
            if (viewer.yaw == 0 || viewer.pitch == Viewer{}.pitch)
                throw std::runtime_error("Drag control failed");
            auto scroll = glfwSetScrollCallback(window, nullptr);
            glfwSetScrollCallback(window, scroll);
            scroll(window, 0, 1000);
            if (viewer.distance < 2.3f || viewer.distance >= Viewer{}.distance)
                throw std::runtime_error("Zoom control failed");
            key(window, GLFW_KEY_R, 0, GLFW_PRESS, 0);
            if (viewer.paused || viewer.orbit || viewer.yaw != 0 || viewer.quality != 0)
                throw std::runtime_error("Reset control failed");
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            for (int q : {0, 1}) {
                viewer = Viewer{};
                viewer.quality = q;
                draw(width, height);
                auto first = capture(width, height);
                viewer.time = 24;
                draw(width, height);
                if (first != capture(width, height)) throw std::runtime_error("Animation loop is not seamless");
                viewer.time = 6;
                draw(width, height);
                if (first == capture(width, height)) throw std::runtime_error("Animation did not change the image");
                viewer.time = 0;
                viewer.yaw = 0.6f;
                draw(width, height);
                if (first == capture(width, height)) throw std::runtime_error("Camera did not change the image");
                for (float angle : {-3.0f, 0.0f, 3.0f}) {
                    viewer.yaw = angle;
                    viewer.pitch = 1.35f;
                    viewer.distance = 2.3f;
                    draw(width, height);
                    capture(width, height);
                }
            }
            draw(213, 127);
            capture(213, 127);
            std::cout << "GPU checks passed: controls, both qualities, 24-second loop, animation, camera, resize, pixel ranges.\n";
        } else if (!snapshot.empty()) {
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            viewer.quality = 1;
            draw(width, height);
            auto pixels = capture(width, height);
            std::ofstream out(snapshot);
            if (!out) throw std::runtime_error("Cannot open snapshot: " + snapshot);
            out << "P3\n" << width << ' ' << height << "\n255\n";
            for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x)
                for (int c = 0; c < 3; ++c)
                    out << int(pixels[(y * width + x) * 4 + c] * 255 + 0.5f) << (c == 2 ? '\n' : ' ');
            out.flush();
            if (!out) throw std::runtime_error("Cannot write snapshot");
        } else {
            double previous = glfwGetTime(), title_time = previous;
            int frames = 0;
            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();
                double now = glfwGetTime();
                float dt = static_cast<float>(std::clamp(now - previous, 0.0, 0.1));
                previous = now;
                int width = 0, height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                    glfwWaitEventsTimeout(0.1);
                    continue;
                }
                if (!viewer.paused) {
                    viewer.time = std::fmod(viewer.time + dt, 24.0);
                    if (viewer.orbit) viewer.yaw += dt * 0.18f;
                }
                float horizontal = float(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) -
                                   float(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
                float vertical = float(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) -
                                 float(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
                if (horizontal != 0 || vertical != 0) viewer.orbit = false;
                viewer.yaw += horizontal * dt;
                viewer.pitch += vertical * dt;
                viewer.clamp_camera();
                draw(width, height);
                glfwSwapBuffers(window);
                ++frames;
                if (now - title_time >= 0.5) {
                    std::string title = "Daft Punk Live | " + std::to_string(int(frames / (now - title_time))) +
                        " fps | " + (viewer.paused ? "PAUSED" : "PLAYING") +
                        (viewer.quality ? " HQ" : " FAST") + (viewer.orbit ? " AUTO" : "") +
                        " | Drag: orbit / Scroll: zoom / Space: pause / A: auto / R: reset / Q: quality";
                    glfwSetWindowTitle(window, title.c_str());
                    frames = 0;
                    title_time = now;
                }
            }
        }
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        glfwDestroyWindow(window);
        glfwTerminate();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    return 0;
}
