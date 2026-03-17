#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <xcb/xcb.h>

/* Load a PNG file into a buffer of ARGB pixels (32-bit, native byte order).
 * The _NET_WM_ICON format expects: width, height, then width*height uint32_t
 * pixels in ARGB order (most-significant byte is A). */
static uint32_t *load_png_argb(const char *path, int *w, int *h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", path);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "PNG read error\n");
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    /* Normalize to 8-bit RGBA regardless of source format. */
    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int rowbytes = png_get_rowbytes(png, info);
    png_bytep *rows = malloc(sizeof(png_bytep) * (*h));
    for (int y = 0; y < *h; y++)
        rows[y] = malloc(rowbytes);

    png_read_image(png, rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    /* Convert from RGBA byte order to ARGB uint32. */
    uint32_t *pixels = malloc((*w) * (*h) * sizeof(uint32_t));
    for (int y = 0; y < *h; y++) {
        png_bytep row = rows[y];
        for (int x = 0; x < *w; x++) {
            uint8_t r = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t b = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];
            pixels[y * (*w) + x] = ((uint32_t)a << 24) |
                                   ((uint32_t)r << 16) |
                                   ((uint32_t)g << 8)  |
                                   (uint32_t)b;
        }
        free(rows[y]);
    }
    free(rows);
    return pixels;
}

int main(int argc, char **argv)
{
    const char *icon_path = (argc > 1) ? argv[1] : "icon.png";

    /* Load icon PNG. */
    int icon_w, icon_h;
    uint32_t *icon_pixels = load_png_argb(icon_path, &icon_w, &icon_h);
    if (!icon_pixels)
        return 1;

    printf("Loaded icon: %dx%d from %s\n", icon_w, icon_h, icon_path);

    /* Connect to X server. */
    int screen_num;
    xcb_connection_t *conn = xcb_connect(NULL, &screen_num);
    if (xcb_connection_has_error(conn)) {
        fprintf(stderr, "Cannot connect to X server\n");
        free(icon_pixels);
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_num; i++)
        xcb_screen_next(&iter);
    xcb_screen_t *screen = iter.data;

    /* Create the window. */
    xcb_window_t win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {
        screen->white_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS
    };

    xcb_create_window(conn,
                      XCB_COPY_FROM_PARENT,
                      win,
                      screen->root,
                      0, 0, 400, 300, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      mask, values);

    /* Set window title. */
    const char *title = "Hello XCB - App Icon Demo";
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        strlen(title), title);

    /* Intern _NET_WM_ICON and CARDINAL atoms. */
    xcb_intern_atom_cookie_t icon_cookie =
        xcb_intern_atom(conn, 0, strlen("_NET_WM_ICON"), "_NET_WM_ICON");
    xcb_intern_atom_cookie_t cardinal_cookie =
        xcb_intern_atom(conn, 0, strlen("CARDINAL"), "CARDINAL");

    xcb_intern_atom_reply_t *icon_reply =
        xcb_intern_atom_reply(conn, icon_cookie, NULL);
    xcb_intern_atom_reply_t *cardinal_reply =
        xcb_intern_atom_reply(conn, cardinal_cookie, NULL);

    if (!icon_reply || !cardinal_reply) {
        fprintf(stderr, "Failed to intern atoms\n");
        free(icon_pixels);
        free(icon_reply);
        free(cardinal_reply);
        xcb_disconnect(conn);
        return 1;
    }

    /* Build _NET_WM_ICON data: [width, height, pixel0, pixel1, ...] */
    size_t icon_data_len = 2 + (size_t)(icon_w * icon_h);
    uint32_t *icon_data = malloc(icon_data_len * sizeof(uint32_t));
    icon_data[0] = icon_w;
    icon_data[1] = icon_h;
    memcpy(&icon_data[2], icon_pixels, icon_w * icon_h * sizeof(uint32_t));

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
                        icon_reply->atom,
                        cardinal_reply->atom,
                        32,
                        icon_data_len,
                        icon_data);

    free(icon_reply);
    free(cardinal_reply);
    free(icon_data);
    free(icon_pixels);

    /* Map (show) the window. */
    xcb_map_window(conn, win);
    xcb_flush(conn);

    printf("Window created. Press any key or close the window to exit.\n");

    /* Event loop. */
    xcb_generic_event_t *event;
    int running = 1;
    while (running && (event = xcb_wait_for_event(conn))) {
        switch (event->response_type & ~0x80) {
        case XCB_EXPOSE:
            /* Nothing to draw — we just show a white window. */
            break;
        case XCB_KEY_PRESS:
            running = 0;
            break;
        case 0:
            /* Error event. */
            running = 0;
            break;
        }
        free(event);
    }

    xcb_disconnect(conn);
    return 0;
}
