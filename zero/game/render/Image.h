#ifndef ZERO_RENDER_IMAGE_H_
#define ZERO_RENDER_IMAGE_H_

#include <cstddef>

namespace zero {

// Image loading function that typically passes to stb_image.
// It also implements RLE BMP loading if that fails.
unsigned char* ImageLoad(const char* filename, int* width, int* height, bool asset = true);
unsigned char* ImageLoadFromMemory(const unsigned char* data, size_t size, int* width, int* height);
void ImageFree(void* data);

}  // namespace zero

#endif
