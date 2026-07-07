#pragma once
#include <string>

struct ImageDimensions {
    int width = 0;
    int height = 0;
};

/**
 * Reads image dimensions from PNG or JPEG files by parsing
 * file headers directly — no external library required.
 *
 * @param filepath Path to the image file
 * @return ImageDimensions with width and height (both 0 on failure)
 */
ImageDimensions get_image_dimensions(const std::string& filepath);
