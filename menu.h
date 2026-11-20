/**
 * @file menu.h
 * @brief Tiny, allocation-free tree menu engine for embedded C.
 *
 * All pages are statically allocated by the user. No malloc, no recursion.
 */
#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ---- Configuration (override with -D flags or before including) ---- */
#ifndef MENU_MAX_CHILDREN
#define MENU_MAX_CHILDREN 5   /**< Max children per page */
#endif

#ifndef MENU_TEXT_LEN
#define MENU_TEXT_LEN 20      /**< Max title length, including '\0' */
#endif

typedef struct menu_page menu_page_t;

/** Called when a page is activated (entered, or returned to). */
typedef void (*menu_handler_t)(menu_page_t *self);

typedef enum {
    MENU_OK = 0,
    MENU_ERR_NULL,   /**< NULL pointer passed */
    MENU_ERR_FULL    /**< Parent has no room for another child */
} menu_status_t;

/** One page (node) of the menu tree. */
struct menu_page {
    uint8_t         id;                           /**< User-defined ID */
    char            text[MENU_TEXT_LEN];          /**< Title */
    menu_page_t    *parent;                       /**< NULL for root */
    menu_page_t    *children[MENU_MAX_CHILDREN];
    uint8_t         child_count;
    uint8_t         selected;                     /**< Highlighted child index */
    menu_handler_t  handler;                      /**< May be NULL */
};

/** Menu controller: remembers where the user currently is. */
typedef struct {
    menu_page_t *root;
    menu_page_t *current;
} menu_t;

/* ---- Building the tree ---- */
void          menu_page_init(menu_page_t *page, uint8_t id,
                             const char *text, menu_handler_t handler);
menu_status_t menu_add_child(menu_page_t *parent, menu_page_t *child);

/* ---- Controller ---- */
menu_status_t menu_init(menu_t *menu, menu_page_t *root);
void          menu_next(menu_t *menu);   /**< Highlight next child (wraps) */
void          menu_prev(menu_t *menu);   /**< Highlight previous child (wraps) */
void          menu_enter(menu_t *menu);  /**< Activate highlighted child */
void          menu_back(menu_t *menu);   /**< Go to parent page */

/* ---- Helpers ---- */
menu_page_t  *menu_current(const menu_t *menu);
menu_page_t  *menu_selected_child(const menu_page_t *page);

#ifdef __cplusplus
}
#endif
#endif /* MENU_H */
