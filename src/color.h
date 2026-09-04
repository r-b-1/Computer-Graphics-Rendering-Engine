#ifndef COLOR_H
#define COLOR_H

#include "vec3.h"

using color = vec3; // here we are using the matrix math from the vec3.h file to create a color class to color objects.

void write_color(std::ostream& out, const color& pixel_color) {
    auto r = pixel_color.x();
    auto g = pixel_color.y();
    auto b = pixel_color.z();

    //Translate the [0,1] component values to the color byte ranges [0,255].
    int rbyte = int(255.999 * r);
    int gbyte = int(255.999 * g);
    int bbyte = int(255.999 * b);

    //Write out the pixel color components.
    out << rbyte << ' ' << gbyte << ' ' << bbyte << '\n';
}

#endif
