#include "plib/gnw/svga.h"

#include <math.h>

#include <algorithm>

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();
static void updateMouseScale(int width, int height);

// screen rect
Rect scr_size;

// 0x6ACA18
ScreenBlitFunc* scr_blit = GNW95_ShowRect;

SDL_Window* gSdlWindow = NULL;
SDL_Surface* gSdlSurface = NULL;
SDL_Renderer* gSdlRenderer = NULL;
SDL_Texture* gSdlTexture = NULL;
SDL_Surface* gSdlTextureSurface = NULL;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

static int gScreenFit = SCREEN_FIT_ASPECT;
static bool gLinearFilter = false;

// Game pixels per window point of mouse motion, see `svgaGetMouseScale`.
static float gMouseScaleX = 1.0f;
static float gMouseScaleY = 1.0f;

// 0x4CB310
void GNW95_SetPaletteEntries(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB568
void GNW95_SetPalette(unsigned char* palette)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB850
void GNW95_ShowRect(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int srcWidth, unsigned int srcHeight, unsigned int destX, unsigned int destY)
{
    buf_to_buf(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
}

bool svga_init(VideoOptions* video_options)
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    gScreenFit = video_options->fit;
    gLinearFilter = video_options->linearFilter;

    int scale = std::max(video_options->scale, 1);
    bool autoSize = video_options->width <= 0 || video_options->height <= 0;

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

    if (video_options->fullscreen) {
        windowFlags |= video_options->fullscreenDesktop
            ? SDL_WINDOW_FULLSCREEN_DESKTOP
            : SDL_WINDOW_FULLSCREEN;
    }

    // The requested window size wins over `width` and `height` which may have
    // been raised to the 640x480 minimum, `fit` scales the game into it.
    // Desktop fullscreen ignores the size.
    int windowWidth = autoSize ? 640 * scale : video_options->width * scale;
    int windowHeight = autoSize ? 480 * scale : video_options->height * scale;
    if (!autoSize && video_options->windowWidth > 0 && video_options->windowHeight > 0) {
        windowWidth = video_options->windowWidth;
        windowHeight = video_options->windowHeight;
    }

    // Exclusive fullscreen at the desktop mode avoids a display mode switch.
    if (video_options->fullscreen
        && !video_options->fullscreenDesktop
        && (autoSize || video_options->keepDesktopMode)) {
        SDL_DisplayMode desktopMode;
        if (SDL_GetDesktopDisplayMode(0, &desktopMode) == 0) {
            windowWidth = desktopMode.w;
            windowHeight = desktopMode.h;
        }
    }

    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        windowWidth,
        windowHeight,
        windowFlags);
    if (gSdlWindow == NULL) {
        return false;
    }

    if (autoSize) {
        // Size the game to the window it actually got (desktop fullscreen
        // on a notched Mac, for example, is shorter than the display),
        // keeping the window's aspect ratio so no bars are needed.
        SDL_GetWindowSize(gSdlWindow, &windowWidth, &windowHeight);
        video_options->height = std::max(windowHeight / scale, 480);
        video_options->width = std::max(windowWidth * video_options->height / windowHeight, 640);
    }

    if (!createRenderer(video_options->width, video_options->height)) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;

        return false;
    }

    gSdlSurface = SDL_CreateRGBSurface(0,
        video_options->width,
        video_options->height,
        8,
        0,
        0,
        0,
        0);
    if (gSdlSurface == NULL) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    scr_size.ulx = 0;
    scr_size.uly = 0;
    scr_size.lrx = video_options->width - 1;
    scr_size.lry = video_options->height - 1;

    mouse_blit_trans = NULL;
    scr_blit = GNW95_ShowRect;
    mouse_blit = GNW95_ShowRect;

    return true;
}

void svga_exit()
{
    destroyRenderer();

    if (gSdlWindow != NULL) {
        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&scr_size);
}

static bool createRenderer(int width, int height)
{
    // Read by SDL when the texture is created.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, gLinearFilter ? "linear" : "nearest");

    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == NULL) {
        return false;
    }

    // Without a logical size `renderPresent` copies the frame over the whole
    // output, which is exactly the stretch mode.
    if (gScreenFit != SCREEN_FIT_STRETCH) {
        if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
            return false;
        }

        if (gScreenFit == SCREEN_FIT_INTEGER) {
            SDL_RenderSetIntegerScale(gSdlRenderer, SDL_TRUE);
        }
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == NULL) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, NULL, NULL, NULL) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == NULL) {
        return false;
    }

    updateMouseScale(width, height);

    return true;
}

// The game moves its own cursor by relative mouse deltas, which SDL reports
// in window points. Convert them to game pixels so the cursor crosses the
// screen at the same speed as the desktop cursor, whatever the scaling.
static void updateMouseScale(int width, int height)
{
    int windowWidth;
    int windowHeight;
    SDL_GetWindowSize(gSdlWindow, &windowWidth, &windowHeight);

    int outputWidth;
    int outputHeight;
    if (SDL_GetRendererOutputSize(gSdlRenderer, &outputWidth, &outputHeight) != 0
        || windowWidth <= 0 || windowHeight <= 0 || outputWidth <= 0 || outputHeight <= 0) {
        gMouseScaleX = 1.0f;
        gMouseScaleY = 1.0f;
        return;
    }

    // Size of the game image in output pixels.
    float imageWidth = static_cast<float>(outputWidth);
    float imageHeight = static_cast<float>(outputHeight);
    if (gScreenFit != SCREEN_FIT_STRETCH) {
        float s = std::min(imageWidth / width, imageHeight / height);
        if (gScreenFit == SCREEN_FIT_INTEGER && s >= 1.0f) {
            s = floorf(s);
        }
        imageWidth = width * s;
        imageHeight = height * s;
    }

    // Convert to window points.
    imageWidth = imageWidth * windowWidth / outputWidth;
    imageHeight = imageHeight * windowHeight / outputHeight;

    gMouseScaleX = width / imageWidth;
    gMouseScaleY = height / imageHeight;
}

void svgaGetMouseScale(float* x, float* y)
{
    *x = gMouseScaleX;
    *y = gMouseScaleY;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != NULL) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = NULL;
    }

    if (gSdlTexture != NULL) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = NULL;
    }

    if (gSdlRenderer != NULL) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = NULL;
    }
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());
}

void renderPresent()
{
    SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
