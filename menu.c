#include "menu.h"
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                         */
/* ------------------------------------------------------------------------ */

static void notify(menu_t *menu, const menu_page_t *page, menu_event_t event)
{
    if (page != NULL && page->handler != NULL) {
        page->handler(menu, page, event);
    }
}

static const menu_page_t *cur_page(const menu_t *menu)
{
    return menu->path[menu->depth];
}

static void activity(menu_t *menu)
{
    menu->idle_ms = 0;
}

static bool selectable(const menu_page_t *page)
{
    return page != NULL && menu_item_state(page) == MENU_ITEM_NORMAL;
}

/* Index of the first selectable child, or 0 if there is none. */
static uint8_t first_selectable(const menu_page_t *page)
{
    const menu_page_t *c;
    uint8_t i = 0;
    for (c = page->first_child; c != NULL; c = c->next_sibling, i++) {
        if (selectable(c)) return i;
        if (i == MENU_MAX_CHILDREN - 1u) break;
    }
    return 0;
}

/* Move the highlight one selectable step in `dir` (+1 / -1), wrapping.
 * Returns true if it moved. */
static bool step_selection(menu_t *menu, int dir)
{
    const menu_page_t *page = cur_page(menu);
    uint8_t count = menu_child_count(page);
    uint8_t idx = menu->sel[menu->depth];
    uint8_t tries;

    if (count == 0) return false;
    if (idx >= count) idx = 0;

    for (tries = 0; tries < count; tries++) {
        if (dir > 0) idx = (uint8_t)((idx + 1u) % count);
        else         idx = (uint8_t)((idx + count - 1u) % count);
        if (selectable(menu_child_at(page, idx))) {
            if (idx == menu->sel[menu->depth]) return false;
            menu->sel[menu->depth] = idx;
            return true;
        }
    }
    return false;
}

/* If the highlighted child became hidden/disabled, move to the next usable one. */
static void fix_selection(menu_t *menu)
{
    const menu_page_t *page = cur_page(menu);
    if (!selectable(menu_child_at(page, menu->sel[menu->depth]))) {
        (void)step_selection(menu, +1);
    }
}

static int32_t value_read(const menu_value_t *v)
{
    switch (v->type) {
    case MENU_VALUE_INT:  return *(const int32_t *)v->var;
    case MENU_VALUE_BOOL: return *(const bool *)v->var ? 1 : 0;
    case MENU_VALUE_ENUM: return *(const uint8_t *)v->var;
    default:              return 0;
    }
}

static void value_write(const menu_value_t *v, int32_t value)
{
    switch (v->type) {
    case MENU_VALUE_INT:  *(int32_t *)v->var = value;           break;
    case MENU_VALUE_BOOL: *(bool *)v->var    = (value != 0);    break;
    case MENU_VALUE_ENUM: *(uint8_t *)v->var = (uint8_t)value;  break;
    default:                                                    break;
    }
}

/* Change the value being edited by `steps` (may be negative). */
static void edit_step(menu_t *menu, int steps)
{
    const menu_page_t *page = menu_active(menu);
    const menu_value_t *v;
    int64_t next;

    if (page == NULL || page->value == NULL) return;
    v = page->value;

    if (v->type == MENU_VALUE_INT) {
        next = (int64_t)menu->edit_value + (int64_t)steps * v->step;
        if (next < v->min) next = v->min;
        if (next > v->max) next = v->max;
        menu->edit_value = (int32_t)next;
    } else if (v->type == MENU_VALUE_ENUM && v->option_count > 0) {
        next = ((int64_t)menu->edit_value + steps) % v->option_count;
        if (next < 0) next += v->option_count;
        menu->edit_value = (int32_t)next;
    }
}

/* One step of next (+1) / prev (-1) without notifying. Returns true if
 * something changed. */
static bool move(menu_t *menu, int dir)
{
    switch ((menu_mode_t)menu->mode) {
    case MENU_MODE_EDIT:
        edit_step(menu, dir);
        return true;
    case MENU_MODE_CONFIRM:
        menu->confirm_yes = !menu->confirm_yes;
        return true;
    case MENU_MODE_BROWSE:
    default:
        return step_selection(menu, dir);
    }
}

static void notify_move(menu_t *menu)
{
    notify(menu, cur_page(menu),
           (menu->mode == MENU_MODE_BROWSE) ? MENU_EVT_SELECT : MENU_EVT_REFRESH);
}

/* ------------------------------------------------------------------------ */
/* Building a tree                                                          */
/* ------------------------------------------------------------------------ */

void menu_page_init(menu_page_t *page, uint8_t id,
                    const char *text, menu_handler_t handler)
{
    if (page == NULL) return;

    memset(page, 0, sizeof(*page));          /* no uninitialized fields */
    page->id = id;
    page->text = (text != NULL) ? text : "";
    page->handler = handler;
}

menu_status_t menu_add_child(menu_page_t *parent, menu_page_t *child)
{
    const menu_page_t *p;

    if (parent == NULL || child == NULL) return MENU_ERR_NULL;
    if (child->parent != NULL) return MENU_ERR_HAS_PARENT;

    /* Reject cycles: child must not be parent or any ancestor of parent. */
    for (p = parent; p != NULL; p = p->parent) {
        if (p == child) return MENU_ERR_LOOP;
    }

    if (menu_child_count(parent) >= MENU_MAX_CHILDREN) return MENU_ERR_FULL;

    /* Append at the end so children keep the order they were added in.
     * The siblings were added through this function, so they are writable. */
    child->next_sibling = NULL;
    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        for (p = parent->first_child; p->next_sibling != NULL; p = p->next_sibling) {
        }
        ((menu_page_t *)(uintptr_t)p)->next_sibling = child;
    }

    child->parent = parent;
    return MENU_OK;
}

/* ------------------------------------------------------------------------ */
/* Controller                                                               */
/* ------------------------------------------------------------------------ */

menu_status_t menu_init(menu_t *menu, const menu_page_t *root)
{
    if (menu == NULL || root == NULL) return MENU_ERR_NULL;

    memset(menu, 0, sizeof(*menu));
    menu->root = root;
    menu->path[0] = root;
    menu->sel[0] = first_selectable(root);
    notify(menu, root, MENU_EVT_ENTER);
    return MENU_OK;
}

void menu_next(menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return;
    activity(menu);
    if (move(menu, +1)) notify_move(menu);
}

void menu_prev(menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return;
    activity(menu);
    if (move(menu, -1)) notify_move(menu);
}

void menu_scroll(menu_t *menu, int8_t delta)
{
    bool changed = false;
    int dir;
    int n;

    if (menu == NULL || menu->root == NULL || delta == 0) return;
    activity(menu);

    dir = (delta > 0) ? +1 : -1;
    n = (delta > 0) ? delta : -delta;

    if (menu->mode == MENU_MODE_EDIT) {
        edit_step(menu, dir * n);      /* one clamp, one redraw */
        changed = true;
    } else {
        while (n-- > 0) {
            if (move(menu, dir)) changed = true;
        }
    }
    if (changed) notify_move(menu);
}

void menu_enter(menu_t *menu)
{
    const menu_page_t *cur;
    const menu_page_t *child;
    const menu_page_t *active;

    if (menu == NULL || menu->root == NULL) return;
    activity(menu);
    cur = cur_page(menu);

    /* Accept an edit or a confirmation. */
    if (menu->mode == MENU_MODE_EDIT) {
        active = menu_active(menu);
        menu->mode = MENU_MODE_BROWSE;
        if (active != NULL && active->value != NULL &&
            value_read(active->value) != menu->edit_value) {
            value_write(active->value, menu->edit_value);
            notify(menu, active, MENU_EVT_VALUE_CHANGED);
        }
        notify(menu, cur, MENU_EVT_REFRESH);
        return;
    }
    if (menu->mode == MENU_MODE_CONFIRM) {
        active = menu_active(menu);
        menu->mode = MENU_MODE_BROWSE;
        if (menu->confirm_yes) notify(menu, active, MENU_EVT_ACTION);
        notify(menu, cur, MENU_EVT_REFRESH);
        return;
    }

    child = menu_selected_child(menu);
    if (!selectable(child)) return;

    if (child->first_child != NULL) {
        /* Page with children: it becomes the current page. */
        if (menu->depth + 1u >= MENU_MAX_DEPTH) return;
        menu->depth++;
        menu->path[menu->depth] = child;
        menu->sel[menu->depth] = first_selectable(child);
        menu->scroll[menu->depth] = 0;
        notify(menu, child, MENU_EVT_ENTER);
    } else if (child->value != NULL) {
        if (child->value->type == MENU_VALUE_BOOL) {
            /* Booleans toggle right away. */
            value_write(child->value, !value_read(child->value));
            notify(menu, child, MENU_EVT_VALUE_CHANGED);
        } else {
            menu->mode = MENU_MODE_EDIT;
            menu->edit_value = value_read(child->value);
        }
        notify(menu, cur, MENU_EVT_REFRESH);
    } else if (child->flags & MENU_FLAG_CONFIRM) {
        menu->mode = MENU_MODE_CONFIRM;
        menu->confirm_yes = false;     /* default to the safe answer */
        notify(menu, cur, MENU_EVT_REFRESH);
    } else {
        /* Plain leaf: run its action, then let the current page redraw. */
        notify(menu, child, MENU_EVT_ACTION);
        notify(menu, cur, MENU_EVT_REFRESH);
    }
}

void menu_back(menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return;
    activity(menu);

    if (menu->mode != MENU_MODE_BROWSE) {
        menu->mode = MENU_MODE_BROWSE;  /* cancel edit / confirm */
        notify(menu, cur_page(menu), MENU_EVT_REFRESH);
        return;
    }
    if (menu->depth == 0) return;       /* already at root */

    menu->depth--;
    notify(menu, cur_page(menu), MENU_EVT_BACK);
}

void menu_home(menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return;
    activity(menu);
    menu->mode = MENU_MODE_BROWSE;
    menu->depth = 0;
    notify(menu, menu->root, MENU_EVT_ENTER);
}

menu_status_t menu_goto(menu_t *menu, const menu_page_t *page)
{
    const menu_page_t *stack[MENU_MAX_DEPTH];
    const menu_page_t *it[MENU_MAX_DEPTH];
    uint8_t cnt[MENU_MAX_DEPTH];
    const menu_page_t *c;
    uint8_t d = 0, k, ci;
    bool too_deep = false;

    if (menu == NULL || page == NULL) return MENU_ERR_NULL;
    if (menu->root == NULL) return MENU_ERR_NULL;
    if (page == menu->root) {
        menu_home(menu);
        return MENU_OK;
    }

    /* Iterative depth-first search from the root (no recursion). */
    stack[0] = menu->root;
    it[0] = menu->root->first_child;
    cnt[0] = 0;

    for (;;) {
        c = it[d];
        if (c == NULL) {
            if (d == 0) return too_deep ? MENU_ERR_DEPTH : MENU_ERR_NOT_FOUND;
            d--;
            continue;
        }
        it[d] = c->next_sibling;
        ci = cnt[d]++;

        if (c == page) break;

        if (c->first_child != NULL) {
            if (d + 1u < MENU_MAX_DEPTH) {
                d++;
                stack[d] = c;
                it[d] = c->first_child;
                cnt[d] = 0;
            } else {
                too_deep = true;
            }
        }
    }

    /* Found: `page` is child number `ci` of stack[d]. */
    if (page->first_child != NULL && d + 1u >= MENU_MAX_DEPTH) return MENU_ERR_DEPTH;

    activity(menu);
    menu->mode = MENU_MODE_BROWSE;
    for (k = 0; k <= d; k++) {
        menu->path[k] = stack[k];
        menu->sel[k] = (k < d) ? (uint8_t)(cnt[k] - 1u) : ci;
        menu->scroll[k] = 0;
    }

    if (page->first_child != NULL) {
        /* A page with children: open it. */
        menu->depth = (uint8_t)(d + 1u);
        menu->path[menu->depth] = page;
        menu->sel[menu->depth] = first_selectable(page);
        menu->scroll[menu->depth] = 0;
        notify(menu, page, MENU_EVT_ENTER);
    } else {
        /* A leaf: show its parent with the leaf highlighted. */
        menu->depth = d;
        notify(menu, stack[d], MENU_EVT_ENTER);
    }
    return MENU_OK;
}

void menu_input(menu_t *menu, menu_key_t key)
{
    if (menu == NULL) return;
    switch (key) {
    case MENU_KEY_UP:
        if (menu->mode == MENU_MODE_EDIT) menu_next(menu); else menu_prev(menu);
        break;
    case MENU_KEY_DOWN:
        if (menu->mode == MENU_MODE_EDIT) menu_prev(menu); else menu_next(menu);
        break;
    case MENU_KEY_ENTER: menu_enter(menu); break;
    case MENU_KEY_BACK:  menu_back(menu);  break;
    case MENU_KEY_HOME:  menu_home(menu);  break;
    default:                               break;
    }
}

void menu_refresh(menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return;
    notify(menu, cur_page(menu), MENU_EVT_REFRESH);
}

/* ------------------------------------------------------------------------ */
/* Timeout                                                                  */
/* ------------------------------------------------------------------------ */

void menu_set_timeout(menu_t *menu, uint32_t timeout_ms)
{
    if (menu == NULL) return;
    menu->timeout_ms = timeout_ms;
    menu->idle_ms = 0;
}

void menu_tick(menu_t *menu, uint32_t elapsed_ms)
{
    if (menu == NULL || menu->root == NULL || menu->timeout_ms == 0) return;
    if (menu->idle_ms >= menu->timeout_ms) return;   /* already fired */

    menu->idle_ms = (elapsed_ms > UINT32_MAX - menu->idle_ms)
                  ? UINT32_MAX : menu->idle_ms + elapsed_ms;

    if (menu->idle_ms >= menu->timeout_ms) {
        menu->mode = MENU_MODE_BROWSE;   /* cancel any edit / confirm */
        menu->depth = 0;
        notify(menu, menu->root, MENU_EVT_TIMEOUT);
    }
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

const menu_page_t *menu_current(const menu_t *menu)
{
    return (menu != NULL && menu->root != NULL) ? cur_page(menu) : NULL;
}

uint8_t menu_selected_index(const menu_t *menu)
{
    return (menu != NULL) ? menu->sel[menu->depth] : 0;
}

const menu_page_t *menu_selected_child(const menu_t *menu)
{
    if (menu == NULL || menu->root == NULL) return NULL;
    return menu_child_at(cur_page(menu), menu->sel[menu->depth]);
}

uint8_t menu_depth(const menu_t *menu)
{
    return (menu != NULL) ? menu->depth : 0;
}

const menu_page_t *menu_path(const menu_t *menu, uint8_t level)
{
    if (menu == NULL || menu->root == NULL || level > menu->depth) return NULL;
    return menu->path[level];
}

menu_mode_t menu_mode(const menu_t *menu)
{
    return (menu != NULL) ? (menu_mode_t)menu->mode : MENU_MODE_BROWSE;
}

const menu_page_t *menu_active(const menu_t *menu)
{
    if (menu == NULL || menu->mode == MENU_MODE_BROWSE) return NULL;
    return menu_selected_child(menu);
}

bool menu_confirm_yes(const menu_t *menu)
{
    return (menu != NULL) && menu->confirm_yes;
}

/* ------------------------------------------------------------------------ */
/* Drawing helpers                                                          */
/* ------------------------------------------------------------------------ */

uint8_t menu_view(menu_t *menu, menu_item_t *items, uint8_t rows)
{
    const menu_page_t *page;
    const menu_page_t *c;
    const menu_page_t *active;
    uint8_t sel, i, shown = 0, sel_row = 0, first, n = 0;
    bool sel_visible = false;

    if (menu == NULL || menu->root == NULL || items == NULL || rows == 0) return 0;

    fix_selection(menu);
    page = cur_page(menu);
    sel = menu->sel[menu->depth];
    active = menu_active(menu);

    /* Count visible rows and find which row the highlight is on. */
    for (c = page->first_child, i = 0; c != NULL; c = c->next_sibling, i++) {
        if (menu_item_state(c) == MENU_ITEM_HIDDEN) continue;
        if (i == sel) { sel_row = shown; sel_visible = true; }
        shown++;
        if (i == MENU_MAX_CHILDREN - 1u) break;
    }

    /* Keep the highlighted row inside the window. */
    first = menu->scroll[menu->depth];
    if (sel_visible) {
        if (sel_row < first) first = sel_row;
        if (sel_row >= first + rows) first = (uint8_t)(sel_row - rows + 1u);
    }
    if (shown <= rows) first = 0;
    else if (first > shown - rows) first = (uint8_t)(shown - rows);
    menu->scroll[menu->depth] = first;

    /* Fill the window. */
    shown = 0;
    for (c = page->first_child, i = 0; c != NULL && n < rows; c = c->next_sibling, i++) {
        menu_item_state_t st = menu_item_state(c);
        if (st == MENU_ITEM_HIDDEN) continue;
        if (shown++ < first) continue;
        items[n].page = c;
        items[n].index = i;
        items[n].selected = sel_visible && (i == sel);
        items[n].disabled = (st == MENU_ITEM_DISABLED);
        items[n].editing = (c == active) && (menu->mode == MENU_MODE_EDIT);
        n++;
        if (i == MENU_MAX_CHILDREN - 1u) break;
    }
    return n;
}

int32_t menu_value_get(const menu_t *menu, const menu_page_t *page)
{
    if (page == NULL || page->value == NULL) return 0;
    if (menu != NULL && menu->mode == MENU_MODE_EDIT && menu_active(menu) == page) {
        return menu->edit_value;
    }
    return value_read(page->value);
}

const char *menu_value_label(const menu_t *menu, const menu_page_t *page)
{
    int32_t v;
    if (page == NULL || page->value == NULL) return NULL;
    if (page->value->type != MENU_VALUE_ENUM || page->value->options == NULL) return NULL;
    v = menu_value_get(menu, page);
    if (v < 0 || v >= page->value->option_count) return NULL;
    return page->value->options[v];
}

/* ------------------------------------------------------------------------ */
/* Tree helpers                                                             */
/* ------------------------------------------------------------------------ */

uint8_t menu_child_count(const menu_page_t *page)
{
    const menu_page_t *c;
    uint8_t n = 0;
    if (page == NULL) return 0;
    for (c = page->first_child; c != NULL && n < MENU_MAX_CHILDREN; c = c->next_sibling) {
        n++;
    }
    return n;
}

const menu_page_t *menu_child_at(const menu_page_t *page, uint8_t index)
{
    const menu_page_t *c;
    if (page == NULL) return NULL;
    for (c = page->first_child; c != NULL && index > 0; c = c->next_sibling) {
        index--;
    }
    return c;
}

menu_item_state_t menu_item_state(const menu_page_t *page)
{
    if (page == NULL) return MENU_ITEM_HIDDEN;
    return (page->state != NULL) ? page->state(page) : MENU_ITEM_NORMAL;
}
