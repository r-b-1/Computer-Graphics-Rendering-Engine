#include "color.h"
#include "vec3.h"

#include <iostream>

int main() {
    // Image

    int imag_width = 256;
    int image_height = 256;

    // Render

    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    for (int j = 0; j < image_height; j++) {
        std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush; // Here we are calculating the scanlines left, using j to iterate.
        for (int i=0; i < image_width; i++) {
            auto pixel_color = color(float(i)/(image_width-1), double(j)/(image_height - 1), 0);
            write_color(std::cout, pixel_color);
        }
    }

    std::clog << "\rDone.                 \n";
}