#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// A small picture a widget has already decoded and wants drawn in its panel: what a row opens, as
// the shell shows it. Plain pixels, so a widget needs no Direct2D to hold one and a worker thread
// can make one; the painter turns it into a device bitmap the first time it is drawn.
struct PanelImage {
    // Unique for the life of the process and never reused, so the painter can keep the bitmap it
    // made for an image under this number without ever confusing it with a different picture.
    std::uint64_t id = 0;
    int width = 0;
    int height = 0;
    // width * height * 4 bytes, blue-green-red-alpha, premultiplied, top row first - the layout
    // Direct2D wants for D2D1_ALPHA_MODE_PREMULTIPLIED, so nothing is converted on the way in.
    std::vector<std::uint8_t> bgra;

    bool Valid() const {
        return width > 0 && height > 0 &&
               bgra.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }
};

inline std::uint64_t NextImageId() {
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}
