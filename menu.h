/**
 * @file menu.h
 * @brief Tiny, allocation-free tree menu engine for embedded C.
 *
 * - No malloc, no recursion, no platform dependencies.
 * - Pages hold only constant data, so a whole tree can be declared `const`
 *   and live in Flash. All runtime state (where the user is, what is
 *   highlighted, what is being edited) lives in one menu_t.
 * - Children are a linked list: no per-page child limit, leaves are cheap.
 * - Built-in value editing (int / bool / enum), hidden and disabled items,
 *   Yes/No confirmation, scrolling view for small displays, inactivity
 *   timeout, jump-to-page and breadcrumbs.
 */
#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Configuration ----
 * MENU_MAX_DEPTH changes the size of menu_t, so menu.c and every file that
 * includes menu.h MUST see the same value. Either pass it as a compiler flag
 * for the whole project (-DMENU_MAX_DEPTH=10), or put it in a menu_config.h
 * on the include path and build with -DMENU_USE_CONFIG_FILE.
 * Do NOT #define it in a single .c file before including this header.
 */
#ifdef MENU_USE_CONFIG_FILE
#include "menu_config.h"
#endif

#ifndef MENU_MAX_DEPTH
#define MENU_MAX_DEPTH 8   /**< Max tree levels, root included */
#endif

#if MENU_MAX_DEPTH < 1 || MENU_MAX_DEPTH > 255
#error "MENU_MAX_DEPTH must be 1..255"
#endif

/** Maximum children per page (indices are uint8_t). */
#define MENU_MAX_CHILDREN 255u

typedef struct menu_page menu_page_t;
typedef struct menu      menu_t;

/* ------------------------------------------------------------------------ */
/* Events                                                                   */
/* ------------------------------------------------------------------------ */

/** Why a handler is being called. */
typedef enum {
    MENU_EVT_ENTER = 0,    /**< Page became current (menu_init, menu_enter, menu_goto, menu_home) */
    MENU_EVT_BACK,         /**< Page became current again via menu_back() */
    MENU_EVT_SELECT,       /**< Highlight on this (current) page moved */
    MENU_EVT_REFRESH,      /**< Current page should redraw (edit/confirm changed, after an action, menu_refresh) */
    MENU_EVT_ACTION,       /**< This leaf was activated (after confirmation, if it asks for one) */
    MENU_EVT_VALUE_CHANGED,/**< This value page's variable was changed by the user */
    MENU_EVT_TIMEOUT       /**< Sent to root after the inactivity timeout brought the menu home */
} menu_event_t;

/**
 * Page handler. ENTER, BACK, SELECT, REFRESH and TIMEOUT go to the current
 * page and all mean "draw yourself". ACTION and VALUE_CHANGED go to the leaf
 * the user activated or edited.
 */
typedef void (*menu_handler_t)(menu_t *menu, const menu_page_t *self,
                               menu_event_t event);

/* ------------------------------------------------------------------------ */
/* Hidden / disabled items                                                  */
/* ------------------------------------------------------------------------ */

typedef enum {
    MENU_ITEM_NORMAL = 0,
    MENU_ITEM_DISABLED,    /**< Drawn, but cannot be highlighted or entered */
    MENU_ITEM_HIDDEN       /**< Not drawn, cannot be highlighted */
} menu_item_state_t;

/** Decides at runtime whether a page is shown / usable. NULL = always normal. */
typedef menu_item_state_t (*menu_state_fn_t)(const menu_page_t *self);

/* ------------------------------------------------------------------------ */
/* Editable values                                                          */
/* ------------------------------------------------------------------------ */

typedef enum {
    MENU_VALUE_INT = 0,    /**< var is int32_t*, uses min / max / step (clamped) */
    MENU_VALUE_BOOL,       /**< var is bool*, toggled directly by menu_enter() */
    MENU_VALUE_ENUM        /**< var is uint8_t* index into options (wraps) */
} menu_value_type_t;

/** Describes the variable a value page edits. Can be const (in Flash). */
typedef struct {
    menu_value_type_t  type;
    void              *var;
    int32_t            min, max, step;  /**< INT only */
    const char *const *options;         /**< ENUM only */
    uint8_t            option_count;    /**< ENUM only */
} menu_value_t;

#define MENU_VALUE_INT_INIT(var_ptr, vmin, vmax, vstep) \
    { MENU_VALUE_INT, (var_ptr), (vmin), (vmax), (vstep), NULL, 0 }
#define MENU_VALUE_BOOL_INIT(var_ptr) \
    { MENU_VALUE_BOOL, (var_ptr), 0, 1, 1, NULL, 0 }
/** opts must be a real array (not a pointer), so its length can be computed. */
#define MENU_VALUE_ENUM_INIT(var_ptr, opts) \
    { MENU_VALUE_ENUM, (var_ptr), 0, 0, 0, (opts), \
      (uint8_t)(sizeof(opts) / sizeof((opts)[0])) }

/* ------------------------------------------------------------------------ */
/* Pages                                                                    */
/* ------------------------------------------------------------------------ */

/** Page flags. */
#define MENU_FLAG_CONFIRM 0x01u  /**< Ask Yes/No before sending ACTION */

/**
 * One page (node) of the menu tree. Contains no runtime state, so it can be
 * `const`. Build it either at runtime with menu_page_init() + menu_add_child(),
 * or statically with MENU_PAGE_DECLARE / MENU_PAGE_DEFINE.
 */
struct menu_page {
    const char         *text;          /**< Title (not copied) */
    menu_handler_t      handler;       /**< May be NULL */
    menu_state_fn_t     state;         /**< May be NULL (always normal) */
    const menu_value_t *value;         /**< Non-NULL makes this a value page */
    void               *user;          /**< Free for the application */
    const menu_page_t  *parent;        /**< Optional; set by menu_add_child() */
    const menu_page_t  *first_child;   /**< NULL for a leaf */
    const menu_page_t  *next_sibling;  /**< NULL for the last child */
    uint8_t             id;            /**< User-defined ID */
    uint8_t             flags;         /**< MENU_FLAG_* */
};

/**
 * Static (Flash) trees. Declare every page first so pages can point at each
 * other, then define them with designated initializers:
 *
 *   MENU_PAGE_DECLARE(main_pg);
 *   MENU_PAGE_DECLARE(about_pg);
 *   MENU_PAGE_DEFINE(main_pg,  .text = "Main", .handler = draw, .first_child = &about_pg);
 *   MENU_PAGE_DEFINE(about_pg, .text = "About", .parent = &main_pg);
 *
 * Pages of a static tree must not be passed to menu_add_child().
 */
#define MENU_PAGE_DECLARE(name)      static const menu_page_t name
#define MENU_PAGE_DEFINE(name, ...)  static const menu_page_t name = { __VA_ARGS__ }

/* ------------------------------------------------------------------------ */
/* Controller                                                               */
/* ------------------------------------------------------------------------ */

typedef enum {
    MENU_MODE_BROWSE = 0,  /**< Moving through the list */
    MENU_MODE_EDIT,        /**< Editing an INT / ENUM value (menu_active() is the page) */
    MENU_MODE_CONFIRM      /**< Asking Yes/No (menu_active() is the page) */
} menu_mode_t;

/** All runtime state. Treat the fields as private; use the functions below. */
struct menu {
    const menu_page_t *root;
    const menu_page_t *path[MENU_MAX_DEPTH];   /**< path[0] = root ... path[depth] = current */
    uint8_t            sel[MENU_MAX_DEPTH];    /**< Highlighted child index per level */
    uint8_t            scroll[MENU_MAX_DEPTH]; /**< First visible row per level */
    uint8_t            depth;
    uint8_t            mode;                   /**< menu_mode_t */
    bool               confirm_yes;
    int32_t            edit_value;
    uint32_t           idle_ms;
    uint32_t           timeout_ms;             /**< 0 = no timeout */
};

typedef enum {
    MENU_OK = 0,
    MENU_ERR_NULL,       /**< NULL pointer passed */
    MENU_ERR_FULL,       /**< Parent already has MENU_MAX_CHILDREN children */
    MENU_ERR_HAS_PARENT, /**< Child is already attached to a parent */
    MENU_ERR_LOOP,       /**< Child is the parent itself or one of its ancestors */
    MENU_ERR_NOT_FOUND,  /**< Page is not in this menu's tree */
    MENU_ERR_DEPTH       /**< Page is deeper than MENU_MAX_DEPTH */
} menu_status_t;

/** Buttons for menu_input(). */
typedef enum {
    MENU_KEY_UP = 0,     /**< Browse: previous item. Edit: increase. */
    MENU_KEY_DOWN,       /**< Browse: next item. Edit: decrease. */
    MENU_KEY_ENTER,
    MENU_KEY_BACK,
    MENU_KEY_HOME
} menu_key_t;

/* ---- Building a tree at runtime ---- */
void          menu_page_init(menu_page_t *page, uint8_t id,
                             const char *text, menu_handler_t handler);
menu_status_t menu_add_child(menu_page_t *parent, menu_page_t *child);

/* ---- Start ---- */
menu_status_t menu_init(menu_t *menu, const menu_page_t *root); /**< Sends ENTER to root */

/* ---- Navigation (each counts as user activity for the timeout) ---- */
void          menu_next(menu_t *menu);    /**< Browse: next item (wraps). Edit: +step. Confirm: toggle */
void          menu_prev(menu_t *menu);    /**< Browse: previous item (wraps). Edit: -step. Confirm: toggle */
void          menu_enter(menu_t *menu);   /**< Open / run / edit / toggle; in edit or confirm: accept */
void          menu_back(menu_t *menu);    /**< Go to parent; in edit or confirm: cancel */
void          menu_home(menu_t *menu);    /**< Jump to root, sends ENTER */
menu_status_t menu_goto(menu_t *menu, const menu_page_t *page);
void          menu_input(menu_t *menu, menu_key_t key);
void          menu_scroll(menu_t *menu, int8_t delta); /**< Rotary encoder: >0 = next, <0 = prev */
void          menu_refresh(menu_t *menu); /**< Send REFRESH to the current page (not user activity) */

/* ---- Inactivity timeout ---- */
void          menu_set_timeout(menu_t *menu, uint32_t timeout_ms); /**< 0 disables */
void          menu_tick(menu_t *menu, uint32_t elapsed_ms);        /**< Call periodically */

/* ---- Where are we ---- */
const menu_page_t *menu_current(const menu_t *menu);
const menu_page_t *menu_selected_child(const menu_t *menu);
uint8_t            menu_selected_index(const menu_t *menu);
uint8_t            menu_depth(const menu_t *menu);                  /**< 0 at root */
const menu_page_t *menu_path(const menu_t *menu, uint8_t level);    /**< 0 = root ... depth = current */
menu_mode_t        menu_mode(const menu_t *menu);
const menu_page_t *menu_active(const menu_t *menu); /**< Page being edited / confirmed, else NULL */
bool               menu_confirm_yes(const menu_t *menu);

/* ---- Drawing helpers ---- */

/** One row to draw, filled by menu_view(). */
typedef struct {
    const menu_page_t *page;
    uint8_t            index;     /**< Child index within the current page */
    bool               selected;
    bool               disabled;
    bool               editing;   /**< This row is the value being edited */
} menu_item_t;

/**
 * Fill up to `rows` items of the current page to draw, scrolled so the
 * highlighted item is always visible. Hidden items are skipped.
 * Returns how many items were written.
 */
uint8_t       menu_view(menu_t *menu, menu_item_t *items, uint8_t rows);

/** Value to show for a value page: the value being edited, or the stored one. */
int32_t       menu_value_get(const menu_t *menu, const menu_page_t *page);
/** For ENUM pages: the option text to show (NULL for other pages). */
const char   *menu_value_label(const menu_t *menu, const menu_page_t *page);

/* ---- Tree helpers ---- */
uint8_t            menu_child_count(const menu_page_t *page);
const menu_page_t *menu_child_at(const menu_page_t *page, uint8_t index); /**< NULL if out of range */
menu_item_state_t  menu_item_state(const menu_page_t *page);

#ifdef __cplusplus
}
#endif
#endif /* MENU_H */
