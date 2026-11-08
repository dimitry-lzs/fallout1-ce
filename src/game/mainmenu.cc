#include "game/mainmenu.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "game/art.h"
#include "game/game.h"
#include "game/gsound.h"
#include "game/options.h"
#include "game/palette.h"
#include "game/version.h"
#include "plib/color/color.h"
#include "plib/gnw/button.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

#define MAIN_MENU_WINDOW_WIDTH 640
#define MAIN_MENU_WINDOW_HEIGHT 480

typedef enum MainMenuButton {
    MAIN_MENU_BUTTON_INTRO,
    MAIN_MENU_BUTTON_NEW_GAME,
    MAIN_MENU_BUTTON_LOAD_GAME,
    MAIN_MENU_BUTTON_CREDITS,
    MAIN_MENU_BUTTON_EXIT,
    MAIN_MENU_BUTTON_COUNT,
} MainMenuButton;

static int main_menu_fatal_error();
static void main_menu_play_sound(const char* fileName);

// 0x505A84
static int main_window = -1;

// 0x505A88
static unsigned char* main_window_buf = NULL;

// 0x505A8C
static unsigned char* background_data = NULL;

// 0x505A90
static unsigned char* button_up_data = NULL;

// 0x505A94
static unsigned char* button_down_data = NULL;

// 0x505A98
bool in_main_menu = false;

// 0x505A9C
static bool main_menu_created = false;

// 0x505AA0
static unsigned int main_menu_timeout = 120000;

// 0x505AA4
static int button_values[MAIN_MENU_BUTTON_COUNT] = {
    KEY_LOWERCASE_I,
    KEY_LOWERCASE_N,
    KEY_LOWERCASE_L,
    KEY_LOWERCASE_C,
    KEY_LOWERCASE_E,
};

// 0x505AB8
static int return_values[MAIN_MENU_BUTTON_COUNT] = {
    MAIN_MENU_INTRO,
    MAIN_MENU_NEW_GAME,
    MAIN_MENU_LOAD_GAME,
    MAIN_MENU_CREDITS,
    MAIN_MENU_EXIT,
};

// 0x612DC8
static int buttons[MAIN_MENU_BUTTON_COUNT];

// 0x612DC0
static bool main_menu_is_hidden;

// 0x612DC4
static CacheEntry* button_up_key;

// 0x612DDC
static CacheEntry* button_down_key;

// 0x612DE0
static CacheEntry* background_key;

// CE: How the main menu is fitted to the screen (`[MAINMENU] MAIN_MENU_SIZE`).
// 0 - original 640x480 in the centre, 1 - hi-res background covering the
// screen with the button panel scaled to the screen height, 2 - original art
// scaled to fit (bars), 3 - original art stretched to fill.
static int main_menu_size_mode = 0;

// CE: Scaled copies of the button art (the up image also serves as the
// button mask, so it must outlive the buttons).
static std::vector<unsigned char> scaled_button_up;
static std::vector<unsigned char> scaled_button_down;

void main_menu_set_size_mode(int mode)
{
    main_menu_size_mode = mode >= 0 && mode <= 3 ? mode : 0;
}

// 0x472F80
int main_menu_create()
{
    int fid;
    MessageListItem msg;
    int len;

    if (main_menu_created) {
        return 0;
    }

    loadColorTable("color.pal");

    int screenWidth = screenGetWidth();
    int screenHeight = screenGetHeight();

    // The hi-res mode needs the Hi-Res Patch art (shipped with GOG).
    Art* hiresBackground = NULL;
    Art* hiresPanel = NULL;
    int mode = main_menu_size_mode;
    if (mode == 1) {
        hiresBackground = load_frame("art\\intrface\\HR_MAINMENU.frm");
        hiresPanel = load_frame("art\\intrface\\HR_MENU_BG.FRM");
        if (hiresBackground == NULL || hiresPanel == NULL) {
            mode = 2;
        }
    }

    // Menu content is laid out on a "layer" in original pixel units, then
    // scaled by (scaleX, scaleY) to the screen. Hi-res mode stretches the
    // layer to the screen's shape so the panel can sit at the right edge.
    int layerWidth = MAIN_MENU_WINDOW_WIDTH;
    int layerHeight = MAIN_MENU_WINDOW_HEIGHT;
    double scaleX = 1.0;
    double scaleY = 1.0;
    if (mode == 1) {
        scaleX = scaleY = std::min(screenWidth / 640.0, screenHeight / 480.0);
        layerWidth = std::max(static_cast<int>(screenWidth / scaleX), MAIN_MENU_WINDOW_WIDTH);
        layerHeight = std::max(static_cast<int>(screenHeight / scaleY), MAIN_MENU_WINDOW_HEIGHT);
    } else if (mode == 2) {
        scaleX = scaleY = std::min(screenWidth / 640.0, screenHeight / 480.0);
    } else if (mode == 3) {
        scaleX = screenWidth / 640.0;
        scaleY = screenHeight / 480.0;
    }

    int windowWidth = mode == 0 ? MAIN_MENU_WINDOW_WIDTH : screenWidth;
    int windowHeight = mode == 0 ? MAIN_MENU_WINDOW_HEIGHT : screenHeight;
    int layerX = (windowWidth - static_cast<int>(layerWidth * scaleX)) / 2;
    int layerY = (windowHeight - static_cast<int>(layerHeight * scaleY)) / 2;

    // Offset of the original right-side content (panel, buttons, labels,
    // version) in the layer.
    int dx = layerWidth - MAIN_MENU_WINDOW_WIDTH;
    int dy = (layerHeight - MAIN_MENU_WINDOW_HEIGHT) / 2;

    int mainMenuWindowX = (screenWidth - windowWidth) / 2;
    int mainMenuWindowY = (screenHeight - windowHeight) / 2;
    main_window = win_add(mainMenuWindowX,
        mainMenuWindowY,
        windowWidth,
        windowHeight,
        0,
        WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP);
    if (main_window == -1) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    main_window_buf = win_get_buf(main_window);

    // Scaled modes draw into a temporary off-screen window first.
    int layerWindow = main_window;
    if (mode != 0) {
        layerWindow = win_add(0, 0, layerWidth, layerHeight, 0, WINDOW_HIDDEN);
        if (layerWindow == -1) {
            return main_menu_fatal_error();
        }
    }
    unsigned char* layerBuf = win_get_buf(layerWindow);

    if (mode == 1) {
        int panelWidth = art_frame_width(hiresPanel, 0, 0);
        int panelHeight = art_frame_length(hiresPanel, 0, 0);
        // Hi-Res Patch places the panel 24px up-left of the first button.
        int panelX = dx + 425 - 24;
        int panelY = dy + 45 - 24;
        trans_buf_to_buf(art_frame_data(hiresPanel, 0, 0), panelWidth, panelHeight, panelWidth,
            layerBuf + layerWidth * panelY + panelX, layerWidth);
    } else {
        // mainmenu.frm
        int backgroundFid = art_id(OBJ_TYPE_INTERFACE, 140, 0, 0, 0);
        background_data = art_ptr_lock_data(backgroundFid, 0, 0, &background_key);
        if (background_data == NULL) {
            // NOTE: Uninline.
            return main_menu_fatal_error();
        }

        buf_to_buf(background_data, MAIN_MENU_WINDOW_WIDTH,
            MAIN_MENU_WINDOW_HEIGHT,
            MAIN_MENU_WINDOW_WIDTH,
            layerBuf,
            layerWidth);
        art_ptr_unlock(background_key);
    }

    int oldFont = text_curr();
    text_font(100);

    // Copyright.
    msg.num = 14;
    if (message_search(&misc_message_file, &msg)) {
        win_print(layerWindow, msg.text, 0, 15, layerHeight - 20, colorTable[21204] | 0x4000000 | 0x2000000);
    }

    // Version.
    char version[VERSION_MAX];
    getverstr(version, sizeof(version));
    len = text_width(version);
    win_print(layerWindow, version, 0, dx + 615 - len, layerHeight - 20, colorTable[21204] | 0x4000000 | 0x2000000);

    // menuup.frm
    fid = art_id(OBJ_TYPE_INTERFACE, 299, 0, 0, 0);
    button_up_data = art_ptr_lock_data(fid, 0, 0, &button_up_key);
    if (button_up_data == NULL) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    // menudown.frm
    fid = art_id(OBJ_TYPE_INTERFACE, 300, 0, 0, 0);
    button_down_data = art_ptr_lock_data(fid, 0, 0, &button_down_key);
    if (button_down_data == NULL) {
        // NOTE: Uninline.
        return main_menu_fatal_error();
    }

    text_font(104);

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        msg.num = 9 + index;
        if (message_search(&misc_message_file, &msg)) {
            len = text_width(msg.text);
            int labelX = dx + 520 - (len / 2);
            text_to_buf(layerBuf + layerWidth * (dy + 42 * index - index + 46) + labelX,
                msg.text,
                layerWidth - labelX - 1,
                layerWidth,
                colorTable[21091]);
        }
    }

    text_font(oldFont);

    // Compose the scaled menu.
    unsigned char* buttonUp = button_up_data;
    unsigned char* buttonDown = button_down_data;
    int buttonWidth = 26;
    int buttonHeight = 26;
    if (mode != 0) {
        int layerScaledWidth = static_cast<int>(layerWidth * scaleX);
        int layerScaledHeight = static_cast<int>(layerHeight * scaleY);

        if (mode == 1) {
            // Background covers the whole window, cropped evenly.
            int bgWidth = art_frame_width(hiresBackground, 0, 0);
            int bgHeight = art_frame_length(hiresBackground, 0, 0);
            double bgScale = std::max(windowWidth / static_cast<double>(bgWidth), windowHeight / static_cast<double>(bgHeight));
            int cropWidth = std::min(static_cast<int>(windowWidth / bgScale), bgWidth);
            int cropHeight = std::min(static_cast<int>(windowHeight / bgScale), bgHeight);
            unsigned char* bgData = art_frame_data(hiresBackground, 0, 0);
            scale_buf_to_buf(bgData + bgWidth * ((bgHeight - cropHeight) / 2) + (bgWidth - cropWidth) / 2,
                cropWidth,
                cropHeight,
                bgWidth,
                main_window_buf,
                windowWidth,
                windowHeight,
                windowWidth);
            trans_scale_buf_to_buf(layerBuf, layerWidth, layerHeight, layerWidth,
                main_window_buf + windowWidth * layerY + layerX,
                layerScaledWidth,
                layerScaledHeight,
                windowWidth);
        } else {
            scale_buf_to_buf(layerBuf, layerWidth, layerHeight, layerWidth,
                main_window_buf + windowWidth * layerY + layerX,
                layerScaledWidth,
                layerScaledHeight,
                windowWidth);
        }

        win_delete(layerWindow);

        buttonWidth = std::max(static_cast<int>(26 * scaleX), 1);
        buttonHeight = std::max(static_cast<int>(26 * scaleY), 1);
        scaled_button_up.assign(buttonWidth * buttonHeight, 0);
        scaled_button_down.assign(buttonWidth * buttonHeight, 0);
        scale_buf_to_buf(button_up_data, 26, 26, 26, scaled_button_up.data(), buttonWidth, buttonHeight, buttonWidth);
        scale_buf_to_buf(button_down_data, 26, 26, 26, scaled_button_down.data(), buttonWidth, buttonHeight, buttonWidth);
        buttonUp = scaled_button_up.data();
        buttonDown = scaled_button_down.data();
    }

    if (hiresBackground != NULL) {
        mem_free(hiresBackground);
    }
    if (hiresPanel != NULL) {
        mem_free(hiresPanel);
    }

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        buttons[index] = -1;
    }

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        buttons[index] = win_register_button(main_window,
            layerX + static_cast<int>((dx + 425) * scaleX),
            layerY + static_cast<int>((dy + index * 42 - index + 45) * scaleY),
            buttonWidth,
            buttonHeight,
            -1,
            -1,
            1111,
            button_values[index],
            buttonUp,
            buttonDown,
            NULL,
            BUTTON_FLAG_TRANSPARENT);
        if (buttons[index] == -1) {
            // NOTE: Uninline.
            return main_menu_fatal_error();
        }

        win_register_button_mask(buttons[index], buttonUp);
    }

    main_menu_created = true;
    main_menu_is_hidden = true;

    return 0;
}

// 0x473298
void main_menu_destroy()
{
    if (!main_menu_created) {
        return;
    }

    for (int index = 0; index < MAIN_MENU_BUTTON_COUNT; index++) {
        // FIXME: Why it tries to free only invalid buttons?
        if (buttons[index] == -1) {
            win_delete_button(buttons[index]);
        }
    }

    if (button_down_data) {
        art_ptr_unlock(button_down_key);
        button_down_key = NULL;
        button_down_data = NULL;
    }

    if (button_up_data) {
        art_ptr_unlock(button_up_key);
        button_up_key = NULL;
        button_up_data = NULL;
    }

    if (main_window != -1) {
        win_delete(main_window);
    }

    main_menu_created = false;
}

// 0x473330
void main_menu_hide(bool animate)
{
    if (!main_menu_created) {
        return;
    }

    if (main_menu_is_hidden) {
        return;
    }

    soundUpdate();

    if (animate) {
        palette_fade_to(black_palette);
        soundUpdate();
    }

    win_hide(main_window);

    main_menu_is_hidden = true;
}

// 0x473378
void main_menu_show(bool animate)
{
    if (!main_menu_created) {
        return;
    }

    if (!main_menu_is_hidden) {
        return;
    }

    win_show(main_window);

    if (animate) {
        loadColorTable("color.pal");
        palette_fade_to(cmap);
    }

    main_menu_is_hidden = false;
}

// 0x4733BC
int main_menu_is_shown()
{
    return main_menu_created ? main_menu_is_hidden == 0 : 0;
}

// 0x4733D8
int main_menu_is_enabled()
{
    return 1;
}

// 0x4733E0
void main_menu_set_timeout(unsigned int timeout)
{
    main_menu_timeout = 60000 * timeout;
}

// 0x473400
unsigned int main_menu_get_timeout()
{
    return main_menu_timeout / 1000 / 60;
}

// 0x47341C
int main_menu_loop()
{
    in_main_menu = true;

    bool oldCursorIsHidden = mouse_hidden();
    if (oldCursorIsHidden) {
        mouse_show();
    }

    unsigned int tick = get_time();

    int rc = -1;
    while (rc == -1) {
        sharedFpsLimiter.mark();

        int keyCode = get_input();

        for (int buttonIndex = 0; buttonIndex < MAIN_MENU_BUTTON_COUNT; buttonIndex++) {
            if (keyCode == button_values[buttonIndex] || keyCode == toupper(button_values[buttonIndex])) {
                // NOTE: Uninline.
                main_menu_play_sound("nmselec1");

                rc = return_values[buttonIndex];

                if (buttonIndex == MAIN_MENU_BUTTON_CREDITS && (keys[SDL_SCANCODE_RSHIFT] != KEY_STATE_UP || keys[SDL_SCANCODE_LSHIFT] != KEY_STATE_UP)) {
                    rc = MAIN_MENU_QUOTES;
                }

                break;
            }
        }

        if (rc == -1) {
            if (keyCode == KEY_CTRL_R) {
                rc = MAIN_MENU_SELFRUN;
                continue;
            } else if (keyCode == KEY_PLUS || keyCode == KEY_EQUAL) {
                IncGamma();
            } else if (keyCode == KEY_MINUS || keyCode == KEY_UNDERSCORE) {
                DecGamma();
            } else if (keyCode == KEY_UPPERCASE_D || keyCode == KEY_LOWERCASE_D) {
                rc = MAIN_MENU_SCREENSAVER;
                continue;
            } else if (keyCode == 1111) {
                if (!(mouse_get_buttons() & MOUSE_EVENT_LEFT_BUTTON_REPEAT)) {
                    // NOTE: Uninline.
                    main_menu_play_sound("nmselec0");
                }
                continue;
            }
        }

        if (keyCode == KEY_ESCAPE || game_user_wants_to_quit == 3) {
            rc = MAIN_MENU_EXIT;

            // NOTE: Uninline.
            main_menu_play_sound("nmselec1");
            break;
        } else if (game_user_wants_to_quit == 2) {
            game_user_wants_to_quit = 0;
        } else {
            if (elapsed_time(tick) >= main_menu_timeout) {
                rc = MAIN_MENU_TIMEOUT;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (oldCursorIsHidden) {
        mouse_hide();
    }

    in_main_menu = false;

    return rc;
}

// 0x4735B8
static int main_menu_fatal_error()
{
    main_menu_destroy();

    return -1;
}

// 0x4735C4
static void main_menu_play_sound(const char* fileName)
{
    gsound_play_sfx_file(fileName);
}

} // namespace fallout
