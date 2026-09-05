# Hybrid Daft Punk Renderer

## Watch And Explore

The live GPU viewer plays a seamless 24-second smoke and laser show on repeat. Move the camera while it plays, or pause the scene and inspect it from any angle. No video export or frame-by-frame CPU rendering is needed.

On macOS, install GLFW if needed (`brew install glfw`), then run:

```sh
cmake -S . -B build-renderer -DRENDERER_ONLY=ON -DBUILD_LIVE_VIEWER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-renderer --config Release
./build-renderer/daftpunk_live
```

| Control | Action |
| --- | --- |
| Left-button drag or arrow keys | Orbit the camera around the helmet |
| Mouse wheel / two-finger vertical scroll | Zoom in or out |
| `Space` | Pause/resume the show and automatic camera movement; manual camera controls still work |
| `A` | Toggle automatic camera orbit for hands-free viewing |
| `R` | Reset camera and restart the show |
| `Q` | Switch fast/high-quality smoke rendering |
| `Esc` or close window | Quit |

The window is resizable; its title shows playback state, quality, FPS, and controls. Dragging or using arrow keys stops automatic orbit. The shader is embedded in the executable at build time, so the viewer can be launched from any directory.

The live viewer requires a graphical desktop, OpenGL 3.3, and GLFW. Non-macOS builds additionally need GLEW. The CPU renderer remains available without these dependencies: use `-DBUILD_LIVE_VIEWER=OFF` for a CPU-only build. On multi-configuration generators, executables are under `build-renderer/Release/`.

To run the GPU checks or capture a still from the live renderer:

```sh
./build-renderer/daftpunk_live --smoke-test
./build-renderer/daftpunk_live --snapshot live-preview.ppm
```

GPU checks require a graphical session even though their window is hidden. They verify the loop boundary, changing animation/camera views, both quality modes, and framebuffer values. They are separate from CTest so headless CPU builds do not require a display.

## CPU Still Renderer

Render a half-gold, half-chrome Daft Punk-inspired helmet with reflective materials, wraparound and slit visors, ear housings, colored laser fans, low-lying procedural smoke, and a reflective stage. This CPU still renderer uses only the C++20 standard library. The renderer-only build requires CMake 3.22 or newer and a C++20 compiler, but no external libraries, vcpkg, or OpenGL.

## Dependency-Free Build

Run from the repository root:

```sh
cmake -S . -B build-renderer -DRENDERER_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-renderer --config Release
```

`RENDERER_ONLY` defaults to `OFF`. Enabling it builds `daftpunk_lasershow` and its regression checks, skipping the starter project's external dependencies and other targets. Pass `-DBUILD_TESTING=OFF` to omit the checks.

```sh
ctest --test-dir build-renderer -C Release --output-on-failure
```

## Render Still Images

Quick preview:

```sh
./build-renderer/daftpunk_lasershow --width 480 --samples 1 --output preview.ppm
```

Higher quality:

```sh
./build-renderer/daftpunk_lasershow --width 1920 --samples 9 --output daftpunk.ppm
```

For multi-configuration generators such as Visual Studio or Xcode, the executable is under `build-renderer/Release/` instead (with an `.exe` suffix on Windows).

| Option | Description | Default |
| --- | --- | --- |
| `--width integer` | Image width; accepts 64 through 3840. | `960` |
| `--samples integer` | Sampling quality; accepts 1 through 16. | `4` |
| `--time float` | Finite scene time within +/-100000 seconds; varies smoke and laser positions. | `0` |
| `--output path` | Write the PPM image to this path. | PPM to standard output |
| `--help` | Display command-line help. | |

`--time` selects a frame to render in the CPU still renderer. For interactive playback use `daftpunk_live` above. If `--output` is omitted, redirect standard output to a `.ppm` file. The GPU viewer uses a cheaper noise function and periodic motion, so its frames are not pixel-identical to the CPU renderer.

On macOS, convert the PPM image to PNG with `sips`:

```sh
sips -s format png daftpunk.ppm --out daftpunk.png
```

# Original Starter Instructions

The instructions below apply to the original starter build with `RENDERER_ONLY=OFF`.

This project uses CMake and vcpkg for managing C++ dependencies. It serves as a simple example to test your build setup before we get into more complicted code.

## Building Using CMake Presets

We have several CMake Build Presets that are outlined in the CMakePresets.json. Some are for building for Release or Debug mode. Running the default setup is just fine too.

```
cd <path/to/this source>
cmake --preset=default
```

Each preset defines its own build directory and various build variables that are important on that system.

Then, to build this source, you would

```
cd buildVCPkg
cmake --build .
```

Your executables will then be in the build folder. They may be in sub-folders depending on the environment.



## Development Environment Setup

Before you get too deep into this, you will need some tools, depending on your operating system and hardware. To build this, you do need some development tools for C++. The following sections will help with each of your specific environments.  Minimally, you need a terminal, a good C++ editor, a git command line client, and of course a C++ compiler toolchain.

### Linux

The following command in Ubuntu (or related) Linux will get you most of what you might

```
sudo apt update
sudo apt install build-essential cmake git g++ pkg-config libgl-dev libglu-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev 
```

### macOS

On macOS, you'll will need to get Apple's Xcode development environment and IDE installed. It's on the AppStore.

After this, you will want to install Brew:

```
https://brew.sh/
```

Brew is a package installer for mac os that works well.  You can install pretty much any package with Brew that are available on Linux machines.

```
brew install cmake
```

will get you initially started.


### Windows

Building on Windows requires installing a few packages:

* Visual Studio Community Edition 2022 (or newer - such as 2026) - this includes Microsoft's C++ compiler
* CMake https://cmake.org/download/
* Git Bash https://git-scm.com/downloads - You can use the powershell, but this is a nice Unix terminal for Windows and it comes with git
* VS Code https://code.visualstudio.com - VSCode is a nice editor for C++

When you install Microsoft Visual Studio Community Edition, feel free
to add whatever development languages and frameworks you want, but
certainly add C++ and the graphics libraries (DirectX, etc...). Once
the development environments are installed, make sure to setup your
git SSH keys if you want.


# vcpkg - Generalized Build Instructions for Windows, macos and Linux

Once you have some of the items above, it's time to make sure you've got vcpkg ready on your development machine.

We support a more generalized build system using vcpkg [https://learn.microsoft.com/en-us/vcpkg/get_started/overview](https://learn.microsoft.com/en-us/vcpkg/get_started/overview) and CMake build presets. Vcpkg is a C++ package manager used to pull the dependencies needed to build this code. When used in this way, the cmake build system will pull the needed requirements and not rely on installed system dependencies (meaning you shouldn't have to install all sorts of things, ideally). This can result in the initial build being a little slower as the required dependencies are pulled and compiled, but it does mean that you do not have to manually install our dependencies.

## Setting up vcpkg

To setup vcpkg, you will need to clone the vcpkg repository and setup environment variables that CMake can use to locate your vcpkg install.  More information on vcpkg and specific details for setting it up on different systems (Windows vs. Linux-based systems) can be found here: [https://learn.microsoft.com/en-us/vcpkg/get_started/overview](https://learn.microsoft.com/en-us/vcpkg/get_started/overview). 

Determine a location where you want vcpkg installed. It can be in system location for all users or cloned into your own user account. After cloning, be sure to run the bootstrap batch file in the vcpkg folder.

### TLDR; On Windows

Using git-bash, change directories to where you store your development files. Then, clone vcpkg, as shown below:

```
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
```

Next, you will need to create the VCPKG_ROOT environment variable to point to the location of the vcpkg local repository on your system. You should also add the vcpkg root to your PATH variable. The following focuses on Windows, but the same ideas are needed on Unix systems and your shell's environment variables. On Windows, the ideal way to do this so that it is permanent is to set the variables using the Windows System Environment Variables panel from Settings. You can get to this by searching for Environment in the Windows menu. You will need something like the following:

```
VCPKG_ROOT = "C:\path\to\vcpkg"
```
Then, make sure the VCPKG_ROOT is also in your user Path variable.  You should be able to just add another folder path to the VCPKG_ROOT location.

You will need to quit your Powershell (or git-bash) after this and bring up a new window so the PATH variable information gets propoated.


### TLDR; On Linux and macOS

Using a terminal, change directories to where you store your development files. Then, clone vcpkg, as shown below:

```
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
```

Next, you will need to create the VCPKG_ROOT environment variable to point to the location of the vcpkg local repository on your system. You should also add the vcpkg root to your PATH variable. On Linux, you will need to determine which shell environment you use.  For bash, you would edit the ~/.bashrc file, and modify the PATH variable a bit, as shown below:

```
export VCPKG_ROOT=/home/willemsn/dev/vcpkg
export PATH="$PATH":"$VCPKG_ROOT"
```

The instructions are similar for macOS. Determine which shell you use (typically zsh), and then add similar lines to the ~/.zprofile file.

Then, when you restart your terminals, you should be able to run the vcpkg program:
```
vcpkg
```
