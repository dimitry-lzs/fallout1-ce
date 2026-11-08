#ifndef FALLOUT_PLIB_GNW_SVGA_TYPES_H_
#define FALLOUT_PLIB_GNW_SVGA_TYPES_H_

namespace fallout {

// NOTE: These typedefs always appear in this order in every implementation file
// with extended debug info. However `mouse.c` does not have DirectX types
// implying it does not include `svga.h` which does so to expose primary
// DirectDraw objects.

typedef void(UpdatePaletteFunc)();
typedef void(ResetModeFunc)();
typedef int(SetModeFunc)();
typedef void(ScreenTransBlitFunc)(unsigned char* srcBuf, unsigned int srcW, unsigned int srcH, unsigned int subX, unsigned int subY, unsigned int subW, unsigned int subH, unsigned int dstX, unsigned int dstY, unsigned char trans);
typedef void(ScreenBlitFunc)(unsigned char* srcBuf, unsigned int srcW, unsigned int srcH, unsigned int subX, unsigned int subY, unsigned int subW, unsigned int subH, unsigned int dstX, unsigned int dstY);

// How the game image is fitted into the window/screen.
enum ScreenFit {
    // Uniform scale, black bars where the aspect ratio differs.
    SCREEN_FIT_ASPECT = 0,
    // Fill the whole drawable area, pixels may be non-square.
    SCREEN_FIT_STRETCH = 1,
    // Largest whole-number scale that fits, black bars around.
    SCREEN_FIT_INTEGER = 2,
};

typedef struct VideoOptions {
    // Game resolution in game pixels. 0 for either means "auto": derive it
    // from the window/screen size divided by `scale`.
    int width;
    int height;
    // Requested window (or fullscreen mode) size in points; 0 means
    // `width`/`height` times `scale`.
    int windowWidth;
    int windowHeight;
    bool fullscreen;
    // With `fullscreen`: borderless desktop-sized window instead of an
    // exclusive display mode switch.
    bool fullscreenDesktop;
    // With exclusive fullscreen: stay in the desktop display mode and scale
    // the game image instead of switching to the closest smaller mode (on
    // notched Macs those smaller 16:10 modes leave the notch strip black).
    bool keepDesktopMode;
    int scale;
    int fit;
    // Use linear filtering when scaling to the window (default: nearest).
    bool linearFilter;
} VideoOptions;

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_SVGA_TYPES_H_ */
