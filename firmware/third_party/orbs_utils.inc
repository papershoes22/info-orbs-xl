/* Copied unchanged by tools/extract_utils.py from the orbs' original firmware (Utils.cpp). Do not edit. */

struct GrayscaleToTargetColorCache {
    uint8_t targetR8, targetG8, targetB8;
    float brightness;
    bool swapBytes;
    uint16_t lut[256];
    bool valid = false; // Indicates if the cache is valid
};

GrayscaleToTargetColorCache grayscaleToTargetColorCache;

uint16_t Utils::rgb888ToRgb565(uint32_t rgb888, bool swapBytes) {
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >> 8) & 0xFF;
    uint8_t b = rgb888 & 0xFF;

    uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    if (swapBytes) {
        // Swap bytes
        pixel = (pixel >> 8) | (pixel << 8);
    }
    return pixel;
}

uint16_t Utils::rgb565dim(uint16_t rgb565, uint8_t brightness, bool swapBytes) {
    if (rgb565 == TFT_BLACK or brightness == 0) {
        return 0;
    }
    if (swapBytes) {
        // swap bytes
        rgb565 = (rgb565 >> 8) | (rgb565 << 8);
    }

    // Extract 5-bit Red, 6-bit Green, and 5-bit Blue components
    uint8_t r5 = (rgb565 >> 11) & 0x1F;
    uint8_t g6 = (rgb565 >> 5) & 0x3F;
    uint8_t b5 = rgb565 & 0x1F;

    // Scale brightness (0-255) to (0-32) and (0-64) ranges
    uint16_t r5_dim = (r5 * brightness + 127) / 255; // Round by adding 127
    uint16_t g6_dim = (g6 * brightness + 127) / 255;
    uint16_t b5_dim = (b5 * brightness + 127) / 255;

    // Recombine into RGB565 format
    uint16_t result = (r5_dim << 11) | (g6_dim << 5) | b5_dim;

    if (swapBytes) {
        // swap bytes
        result = (result >> 8) | (result << 8);
    }
    return result;
}

void Utils::rgb565dimBitmap(uint16_t *pixel565, size_t length, uint8_t brightness, bool swapBytes) {
    for (int i = 0; i < length; i++) {
        pixel565[i] = rgb565dim(pixel565[i], brightness, swapBytes);
    }
}

uint32_t Utils::rgb565ToRgb888(uint16_t rgb565, bool swapBytes) {
    uint8_t r, g, b;

    if (swapBytes) {
        // Swap bytes
        rgb565 = (rgb565 >> 8) | (rgb565 << 8);
    }
    r = (rgb565 >> 11) & 0x1F; // Extract red (5 bits)
    g = (rgb565 >> 5) & 0x3F; // Extract green (6 bits)
    b = rgb565 & 0x1F; // Extract blue (5 bits)

    // Scale to 8 bits
    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;

    // Pack R, G, B into a single 32-bit color
    return (r << 16) | (g << 8) | b;
}

uint16_t Utils::grayscaleToTargetColor(uint8_t grayscale, uint8_t targetR8, uint8_t targetG8, uint8_t targetB8, float brightness, bool swapBytes) {
    // Check if the cache matches the current parameters
    if (!grayscaleToTargetColorCache.valid ||
        grayscaleToTargetColorCache.targetR8 != targetR8 ||
        grayscaleToTargetColorCache.targetG8 != targetG8 ||
        grayscaleToTargetColorCache.targetB8 != targetB8 ||
        grayscaleToTargetColorCache.brightness != brightness ||
        grayscaleToTargetColorCache.swapBytes != swapBytes) {

        // Cache miss: Recompute the LUT
        grayscaleToTargetColorCache.targetR8 = targetR8;
        grayscaleToTargetColorCache.targetG8 = targetG8;
        grayscaleToTargetColorCache.targetB8 = targetB8;
        grayscaleToTargetColorCache.brightness = brightness;
        grayscaleToTargetColorCache.swapBytes = swapBytes;

        for (uint16_t i = 0; i < 256; i++) {
            // Apply brightness enhancement
            int scaledGrayscale = i * brightness;
            if (scaledGrayscale > 255)
                scaledGrayscale = 255; // Clamp to 255

            // Map grayscale to target color
            uint8_t r = (targetR8 * scaledGrayscale) / 255;
            uint8_t g = (targetG8 * scaledGrayscale) / 255;
            uint8_t b = (targetB8 * scaledGrayscale) / 255;

            // Store in LUT
            grayscaleToTargetColorCache.lut[i] = rgb888ToRgb565((r << 16) | (g << 8) | b, swapBytes);
        }

        grayscaleToTargetColorCache.valid = true; // Mark cache as valid
    }

    // Use the cached value
    return grayscaleToTargetColorCache.lut[grayscale];
}

void Utils::colorizeImageData(uint16_t *pixels565, size_t length, uint32_t targetColor565, float brightness, bool swapBytes) {
    uint32_t targetColor888 = rgb565ToRgb888(targetColor565, false);

    // Extract target R, G, B
    uint8_t targetR8 = (targetColor888 >> 16) & 0xFF;
    uint8_t targetG8 = (targetColor888 >> 8) & 0xFF;
    uint8_t targetB8 = targetColor888 & 0xFF;

    for (size_t i = 0; i < length; i++) {
        if (pixels565[i] == TFT_BLACK) {
            // Skip black pixels
            continue;
        } else if (pixels565[i] == TFT_WHITE) {
            // Set target color directly
            if (swapBytes) {
                // Swap bytes
                pixels565[i] = (targetColor565 >> 8) | (targetColor565 << 8);
            } else {
                pixels565[i] = targetColor565;
            }
            continue;
        }
        // Convert RGB565 to RGB888
        uint32_t color888 = rgb565ToRgb888(pixels565[i], true);

        // Extract grayscale (approximate)
        uint8_t r = (color888 >> 16) & 0xFF;
        uint8_t g = (color888 >> 8) & 0xFF;
        uint8_t b = color888 & 0xFF;
        uint8_t grayscale = (r * 30 + g * 59 + b * 11) / 100;

        // Map grayscale to the target color
        pixels565[i] = grayscaleToTargetColor(grayscale, targetR8, targetG8, targetB8, brightness, swapBytes);
    }
}
