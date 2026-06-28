#pragma once

// Mock of image_processor.h for builds without AI features.
// Only the abstract interface and POD alias are provided; the concrete
// onnxruntime/opencv-backed CLIPImageProcessor is omitted (it is referenced
// only from AI-only translation units which are not compiled in this build).

#include <vector>
#include <string>
#include "option.h"

// processed_image_t is a flattened vector of floats
using processed_image_t = std::vector<float>;

class ImageProcessor {
    public:
        virtual ~ImageProcessor() = default;
        virtual Option<processed_image_t> process_image(const std::string& image) = 0;
};
