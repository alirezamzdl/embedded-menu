#include "menu.h"
#include <string.h>

void menu_page_init(menu_page_t *page, uint8_t id,
                    const char *text, menu_handler_t handler)
{
    if (page == NULL) return;

    memset(page, 0, sizeof(*page));          /* no uninitialized fields */
    page->id = id;
    page->handler = handler;

    if (text != NULL) {
        strncpy(page->text, text, MENU_TEXT_LEN - 1);
        page->text[MENU_TEXT_LEN - 1] = '\0';
    }
}

menu_status_t menu_add_child(menu_page_t *parent, menu_page_t *child)
{
    if (parent == NULL || child == NULL) return MENU_ERR_NULL;
    if (parent->child_count >= MENU_MAX_CHILDREN) return MENU_ERR_FULL;

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return MENU_OK;
}

menu_status_t menu_init(menu_t *menu, menu_page_t *root)
{
    if (menu == NULL || root == NULL) return MENU_ERR_NULL;
    menu->root = root;
    menu->current = root;
    return MENU_OK;
}

void menu_next(menu_t *menu)
{
    menu_page_t *p;
    if (menu == NULL || menu->current == NULL) return;
    p = menu->current;
    if (p->child_count == 0) return;
    p->selected = (uint8_t)((p->selected + 1) % p->child_count);
}

void menu_prev(menu_t *menu)
{
    menu_page_t *p;
    if (menu == NULL || menu->current == NULL) return;
    p = menu->current;
    if (p->child_count == 0) return;
    p->selected = (uint8_t)((p->selected + p->child_count - 1) % p->child_count);
}

void menu_enter(menu_t *menu)
{
    menu_page_t *child;
    if (menu == NULL || menu->current == NULL) return;

    child = menu_selected_child(menu->current);
    if (child == NULL) return;

    /* Pages with children become the current page; leaves just run. */
    if (child->child_count > 0) {
        menu->current = child;
    }
    if (child->handler != NULL) {
        child->handler(child);
    }
}

void menu_back(menu_t *menu)
{
    if (menu == NULL || menu->current == NULL) return;
    if (menu->current->parent == NULL) return;   /* already at root */

    menu->current = menu->current->parent;
    if (menu->current->handler != NULL) {
        menu->current->handler(menu->current);   /* let it redraw */
    }
}

menu_page_t *menu_current(const menu_t *menu)
{
    return (menu != NULL) ? menu->current : NULL;
}

menu_page_t *menu_selected_child(const menu_page_t *page)
{
    if (page == NULL || page->child_count == 0) return NULL;
    return page->children[page->selected];
}
