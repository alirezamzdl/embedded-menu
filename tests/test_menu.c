/**
 * @file test_menu.c
 * @brief Host-side unit tests for embedded-menu. Run with `make test`.
 */
#include <stdio.h>
#include "menu.h"

static int failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                              \
        }                                                            \
    } while (0)

/* ---- Event recorder ---- */
#define LOG_MAX 16
static struct { const menu_page_t *page; menu_event_t event; } log_buf[LOG_MAX];
static int log_len = 0;

static void record(menu_t *menu, const menu_page_t *self, menu_event_t event)
{
    (void)menu;
    if (log_len < LOG_MAX) {
        log_buf[log_len].page = self;
        log_buf[log_len].event = event;
        log_len++;
    }
}

static int logged(int i, const menu_page_t *page, menu_event_t event)
{
    return i < log_len && log_buf[i].page == page && log_buf[i].event == event;
}

/* ------------------------------------------------------------------------ */

static void test_add_child(void)
{
    menu_page_t a, b, c, d;
    menu_page_init(&a, 0, "a", NULL);
    menu_page_init(&b, 1, "b", NULL);
    menu_page_init(&c, 2, "c", NULL);
    menu_page_init(&d, 3, NULL, NULL);

    CHECK(d.text != NULL && d.text[0] == '\0');
    CHECK(menu_add_child(&a, &b) == MENU_OK);
    CHECK(menu_add_child(&a, &d) == MENU_OK);
    CHECK(menu_add_child(&b, &c) == MENU_OK);
    CHECK(menu_add_child(&a, &c) == MENU_ERR_HAS_PARENT);
    CHECK(menu_add_child(&a, &a) == MENU_ERR_LOOP);
    CHECK(menu_add_child(&c, &a) == MENU_ERR_LOOP);
    CHECK(menu_add_child(NULL, &a) == MENU_ERR_NULL);
    CHECK(menu_add_child(&a, NULL) == MENU_ERR_NULL);

    CHECK(menu_child_count(&a) == 2);
    CHECK(menu_child_at(&a, 0) == &b);
    CHECK(menu_child_at(&a, 1) == &d);
    CHECK(menu_child_at(&a, 2) == NULL);
    CHECK(c.parent == &b);
}

static void test_full(void)
{
    static menu_page_t parent, kids[MENU_MAX_CHILDREN + 1];
    unsigned i;
    menu_page_init(&parent, 0, "p", NULL);
    for (i = 0; i < MENU_MAX_CHILDREN; i++) {
        menu_page_init(&kids[i], 0, "k", NULL);
        CHECK(menu_add_child(&parent, &kids[i]) == MENU_OK);
    }
    menu_page_init(&kids[i], 0, "k", NULL);
    CHECK(menu_add_child(&parent, &kids[i]) == MENU_ERR_FULL);
    CHECK(menu_child_count(&parent) == MENU_MAX_CHILDREN);
}

static void test_navigation_and_events(void)
{
    menu_t m;
    menu_page_t root, sub, leaf1, leaf2, subleaf;

    menu_page_init(&root,    0, "root",    record);
    menu_page_init(&sub,     1, "sub",     record);
    menu_page_init(&leaf1,   2, "leaf1",   record);
    menu_page_init(&leaf2,   3, "leaf2",   NULL);
    menu_page_init(&subleaf, 4, "subleaf", record);
    menu_add_child(&root, &sub);
    menu_add_child(&root, &leaf1);
    menu_add_child(&root, &leaf2);
    menu_add_child(&sub, &subleaf);

    log_len = 0;
    CHECK(menu_init(&m, &root) == MENU_OK);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_ENTER));

    /* Wrap-around both ways. */
    log_len = 0;
    menu_prev(&m);
    CHECK(menu_selected_index(&m) == 2);
    CHECK(logged(0, &root, MENU_EVT_SELECT));
    menu_next(&m);
    CHECK(menu_selected_index(&m) == 0);

    /* Leaf action: ACTION to leaf, then REFRESH to current page. */
    menu_next(&m);
    log_len = 0;
    menu_enter(&m);
    CHECK(menu_current(&m) == &root);
    CHECK(log_len == 2);
    CHECK(logged(0, &leaf1, MENU_EVT_ACTION));
    CHECK(logged(1, &root, MENU_EVT_REFRESH));

    /* Leaf without handler: still refreshes, no crash. */
    menu_next(&m);
    log_len = 0;
    menu_enter(&m);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_REFRESH));

    /* Enter a page with children, then back. */
    menu_next(&m);
    CHECK(menu_selected_child(&m) == &sub);
    log_len = 0;
    menu_enter(&m);
    CHECK(menu_current(&m) == &sub);
    CHECK(menu_depth(&m) == 1);
    CHECK(menu_path(&m, 0) == &root && menu_path(&m, 1) == &sub);
    CHECK(menu_path(&m, 2) == NULL);
    CHECK(log_len == 1 && logged(0, &sub, MENU_EVT_ENTER));

    log_len = 0;
    menu_back(&m);
    CHECK(menu_current(&m) == &root);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_BACK));
    CHECK(menu_selected_index(&m) == 0);   /* highlight is remembered */

    /* Back at root does nothing. */
    log_len = 0;
    menu_back(&m);
    CHECK(log_len == 0 && menu_current(&m) == &root);

    log_len = 0;
    menu_refresh(&m);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_REFRESH));
}

static void test_values(void)
{
    static int32_t num = 5;
    static bool    flag = false;
    static uint8_t opt = 0;
    static const char *const opts[] = { "a", "b", "c" };
    static const menu_value_t v_num  = MENU_VALUE_INT_INIT(&num, 0, 10, 3);
    static const menu_value_t v_flag = MENU_VALUE_BOOL_INIT(&flag);
    static const menu_value_t v_opt  = MENU_VALUE_ENUM_INIT(&opt, opts);
    menu_t m;
    menu_page_t root, p_num, p_flag, p_opt;

    menu_page_init(&root,   0, "root", record);
    menu_page_init(&p_num,  1, "num",  record);
    menu_page_init(&p_flag, 2, "flag", record);
    menu_page_init(&p_opt,  3, "opt",  record);
    p_num.value = &v_num;
    p_flag.value = &v_flag;
    p_opt.value = &v_opt;
    menu_add_child(&root, &p_num);
    menu_add_child(&root, &p_flag);
    menu_add_child(&root, &p_opt);
    menu_init(&m, &root);

    CHECK(v_opt.option_count == 3);

    /* INT: edit, clamp at max, commit. */
    log_len = 0;
    menu_enter(&m);
    CHECK(menu_mode(&m) == MENU_MODE_EDIT);
    CHECK(menu_active(&m) == &p_num);
    CHECK(logged(0, &root, MENU_EVT_REFRESH));
    menu_next(&m);                    /* 8 */
    CHECK(menu_value_get(&m, &p_num) == 8);
    CHECK(num == 5);                  /* not written until accepted */
    menu_next(&m);                    /* 11 -> clamped to 10 */
    CHECK(menu_value_get(&m, &p_num) == 10);
    log_len = 0;
    menu_enter(&m);
    CHECK(num == 10);
    CHECK(menu_mode(&m) == MENU_MODE_BROWSE);
    CHECK(logged(0, &p_num, MENU_EVT_VALUE_CHANGED));
    CHECK(logged(1, &root, MENU_EVT_REFRESH));

    /* INT: cancel with back keeps the old value. */
    menu_enter(&m);
    menu_scroll(&m, -5);              /* 10 - 15 -> clamped to 0 */
    CHECK(menu_value_get(&m, &p_num) == 0);
    menu_back(&m);
    CHECK(num == 10 && menu_mode(&m) == MENU_MODE_BROWSE);
    CHECK(menu_current(&m) == &root);  /* back cancelled, didn't leave the page */

    /* Accepting an unchanged value sends no VALUE_CHANGED. */
    menu_enter(&m);
    log_len = 0;
    menu_enter(&m);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_REFRESH));

    /* KEY_UP increases while editing. */
    menu_enter(&m);
    menu_input(&m, MENU_KEY_DOWN);
    CHECK(menu_value_get(&m, &p_num) == 7);
    menu_input(&m, MENU_KEY_UP);
    CHECK(menu_value_get(&m, &p_num) == 10);
    menu_back(&m);

    /* BOOL toggles directly. */
    menu_next(&m);
    log_len = 0;
    menu_enter(&m);
    CHECK(flag == true && menu_mode(&m) == MENU_MODE_BROWSE);
    CHECK(logged(0, &p_flag, MENU_EVT_VALUE_CHANGED));
    CHECK(menu_value_get(&m, &p_flag) == 1);

    /* ENUM wraps both ways. */
    menu_next(&m);
    menu_enter(&m);
    menu_prev(&m);
    CHECK(menu_value_get(&m, &p_opt) == 2);
    CHECK(menu_value_label(&m, &p_opt)[0] == 'c');
    menu_next(&m);
    menu_next(&m);
    CHECK(menu_value_get(&m, &p_opt) == 1);
    menu_enter(&m);
    CHECK(opt == 1);
    CHECK(menu_value_label(&m, &p_opt)[0] == 'b');
    CHECK(menu_value_label(&m, &p_num) == NULL);
}

static void test_confirm(void)
{
    menu_t m;
    menu_page_t root, danger;

    menu_page_init(&root,   0, "root",   record);
    menu_page_init(&danger, 1, "danger", record);
    danger.flags = MENU_FLAG_CONFIRM;
    menu_add_child(&root, &danger);
    menu_init(&m, &root);

    /* Default answer is No. */
    menu_enter(&m);
    CHECK(menu_mode(&m) == MENU_MODE_CONFIRM && !menu_confirm_yes(&m));
    log_len = 0;
    menu_enter(&m);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_REFRESH));

    /* Back cancels. */
    menu_enter(&m);
    menu_next(&m);
    CHECK(menu_confirm_yes(&m));
    log_len = 0;
    menu_back(&m);
    CHECK(menu_mode(&m) == MENU_MODE_BROWSE);
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_REFRESH));

    /* Yes runs the action. */
    menu_enter(&m);
    menu_prev(&m);
    log_len = 0;
    menu_enter(&m);
    CHECK(log_len == 2);
    CHECK(logged(0, &danger, MENU_EVT_ACTION));
    CHECK(logged(1, &root, MENU_EVT_REFRESH));
}

static bool show_b = false;
static menu_item_state_t hide_unless_b(const menu_page_t *p) { (void)p; return show_b ? MENU_ITEM_NORMAL : MENU_ITEM_HIDDEN; }
static menu_item_state_t always_disabled(const menu_page_t *p) { (void)p; return MENU_ITEM_DISABLED; }

static void test_hidden_disabled_and_view(void)
{
    menu_t m;
    menu_page_t root, kids[6];
    menu_item_t items[3];
    uint8_t n, i;

    /* root: k0(disabled) k1 k2(hidden unless show_b) k3 k4 k5 */
    menu_page_init(&root, 0, "root", NULL);
    for (i = 0; i < 6; i++) {
        menu_page_init(&kids[i], i, "k", record);
        menu_add_child(&root, &kids[i]);
    }
    kids[0].state = always_disabled;
    kids[2].state = hide_unless_b;
    show_b = false;
    menu_init(&m, &root);

    /* First selectable is k1. */
    CHECK(menu_selected_index(&m) == 1);
    menu_next(&m);
    CHECK(menu_selected_index(&m) == 3);   /* k2 hidden: skipped */
    menu_prev(&m);
    menu_prev(&m);
    CHECK(menu_selected_index(&m) == 5);   /* k0 disabled: wraps past it */

    /* Entering a disabled item does nothing. */
    CHECK(menu_item_state(&kids[0]) == MENU_ITEM_DISABLED);

    /* View: 5 visible rows (k2 hidden), window of 3, highlight on k5. */
    n = menu_view(&m, items, 3);
    CHECK(n == 3);
    CHECK(items[0].index == 3 && items[1].index == 4 && items[2].index == 5);
    CHECK(items[2].selected && !items[0].selected);

    /* Wrap to top: window scrolls just enough to show the highlight. */
    menu_next(&m);
    CHECK(menu_selected_index(&m) == 1);
    n = menu_view(&m, items, 3);
    CHECK(n == 3);
    CHECK(items[0].index == 1 && items[0].selected);
    CHECK(items[1].index == 3 && items[2].index == 4);

    /* Scrolling up to the top shows the disabled row. */
    menu_goto(&m, &kids[1]);              /* resets the scroll */
    n = menu_view(&m, items, 3);
    CHECK(items[0].index == 0 && items[0].disabled && !items[0].selected);
    CHECK(items[1].index == 1 && items[1].selected);
    CHECK(items[2].index == 3);

    /* Showing k2 at runtime. */
    show_b = true;
    n = menu_view(&m, items, 3);
    CHECK(items[2].index == 2);

    /* Highlighted item becomes hidden: selection moves on. */
    menu_next(&m);
    CHECK(menu_selected_index(&m) == 2);
    show_b = false;
    n = menu_view(&m, items, 3);
    CHECK(menu_selected_index(&m) == 3);

    /* Bigger window than rows: everything, no scroll. */
    {
        menu_item_t all[8];
        n = menu_view(&m, all, 8);
        CHECK(n == 5 && all[0].index == 0);
    }
    CHECK(menu_view(&m, items, 0) == 0);
}

/* Static const tree, as it would sit in Flash. */
MENU_PAGE_DECLARE(s_root);
MENU_PAGE_DECLARE(s_a);
MENU_PAGE_DECLARE(s_a1);
MENU_PAGE_DECLARE(s_a1x);
MENU_PAGE_DECLARE(s_b);
MENU_PAGE_DEFINE(s_root, .text = "root", .handler = record, .first_child = &s_a);
MENU_PAGE_DEFINE(s_a,    .text = "a",    .handler = record, .first_child = &s_a1, .next_sibling = &s_b);
MENU_PAGE_DEFINE(s_a1,   .text = "a1",   .handler = record, .first_child = &s_a1x);
MENU_PAGE_DEFINE(s_a1x,  .text = "a1x",  .handler = record);
MENU_PAGE_DEFINE(s_b,    .text = "b",    .handler = record);

static void test_static_tree_and_goto(void)
{
    menu_t m;
    menu_page_t lone;

    menu_init(&m, &s_root);
    CHECK(menu_child_count(&s_root) == 2);

    /* goto a leaf: show its parent with it highlighted. */
    log_len = 0;
    CHECK(menu_goto(&m, &s_a1x) == MENU_OK);
    CHECK(menu_current(&m) == &s_a1);
    CHECK(menu_depth(&m) == 2);
    CHECK(menu_selected_child(&m) == &s_a1x);
    CHECK(log_len == 1 && logged(0, &s_a1, MENU_EVT_ENTER));

    /* back walks the real path. */
    menu_back(&m);
    CHECK(menu_current(&m) == &s_a && menu_selected_child(&m) == &s_a1);
    menu_back(&m);
    CHECK(menu_current(&m) == &s_root && menu_selected_child(&m) == &s_a);

    /* goto a page with children: open it. */
    CHECK(menu_goto(&m, &s_a) == MENU_OK);
    CHECK(menu_current(&m) == &s_a && menu_depth(&m) == 1);

    CHECK(menu_goto(&m, &s_b) == MENU_OK);
    CHECK(menu_current(&m) == &s_root && menu_selected_child(&m) == &s_b);

    /* goto root == home. */
    CHECK(menu_goto(&m, &s_root) == MENU_OK);
    CHECK(menu_depth(&m) == 0);

    menu_page_init(&lone, 9, "lone", NULL);
    CHECK(menu_goto(&m, &lone) == MENU_ERR_NOT_FOUND);
    CHECK(menu_goto(&m, NULL) == MENU_ERR_NULL);

    /* home from deep. */
    menu_goto(&m, &s_a1x);
    log_len = 0;
    menu_input(&m, MENU_KEY_HOME);
    CHECK(menu_depth(&m) == 0 && logged(0, &s_root, MENU_EVT_ENTER));
}

static void test_depth_limit(void)
{
    /* A chain deeper than MENU_MAX_DEPTH. */
    static menu_page_t chain[MENU_MAX_DEPTH + 2];
    menu_t m;
    unsigned i;

    for (i = 0; i < MENU_MAX_DEPTH + 2; i++) {
        menu_page_init(&chain[i], 0, "c", NULL);
        if (i > 0) menu_add_child(&chain[i - 1], &chain[i]);
    }
    menu_init(&m, &chain[0]);
    for (i = 0; i < MENU_MAX_DEPTH + 2; i++) menu_enter(&m);
    CHECK(menu_depth(&m) == MENU_MAX_DEPTH - 1);
    CHECK(menu_goto(&m, &chain[MENU_MAX_DEPTH + 1]) == MENU_ERR_DEPTH);
}

static void test_timeout(void)
{
    menu_t m;
    menu_page_t root, sub, leaf;
    static int32_t num = 0;
    static const menu_value_t v = MENU_VALUE_INT_INIT(&num, 0, 9, 1);

    menu_page_init(&root, 0, "root", record);
    menu_page_init(&sub,  1, "sub",  record);
    menu_page_init(&leaf, 2, "leaf", record);
    leaf.value = &v;
    menu_add_child(&root, &sub);
    menu_add_child(&sub, &leaf);
    menu_init(&m, &root);

    /* Disabled by default. */
    menu_tick(&m, 1000000u);
    menu_enter(&m);
    CHECK(menu_current(&m) == &sub);

    menu_set_timeout(&m, 1000);
    menu_enter(&m);                       /* start editing */
    menu_tick(&m, 600);
    menu_next(&m);                        /* activity resets idle */
    menu_tick(&m, 600);
    CHECK(menu_mode(&m) == MENU_MODE_EDIT);
    log_len = 0;
    menu_tick(&m, 600);
    CHECK(menu_depth(&m) == 0 && menu_mode(&m) == MENU_MODE_BROWSE);
    CHECK(num == 0);                      /* edit was cancelled */
    CHECK(log_len == 1 && logged(0, &root, MENU_EVT_TIMEOUT));

    /* Fires only once until there is activity again. */
    log_len = 0;
    menu_tick(&m, 5000);
    CHECK(log_len == 0);
    menu_next(&m);
    menu_tick(&m, UINT32_MAX);            /* saturates, no overflow */
    CHECK(logged(log_len - 1, &root, MENU_EVT_TIMEOUT));
}

static void test_null_safety(void)
{
    menu_t m;
    menu_item_t it[1];
    CHECK(menu_init(NULL, NULL) == MENU_ERR_NULL);
    CHECK(menu_init(&m, NULL) == MENU_ERR_NULL);
    menu_page_init(NULL, 0, "x", NULL);
    menu_next(NULL);
    menu_prev(NULL);
    menu_enter(NULL);
    menu_back(NULL);
    menu_home(NULL);
    menu_refresh(NULL);
    menu_scroll(NULL, 3);
    menu_input(NULL, MENU_KEY_ENTER);
    menu_tick(NULL, 1);
    menu_set_timeout(NULL, 1);
    CHECK(menu_goto(NULL, NULL) == MENU_ERR_NULL);
    CHECK(menu_current(NULL) == NULL);
    CHECK(menu_selected_child(NULL) == NULL);
    CHECK(menu_child_at(NULL, 0) == NULL);
    CHECK(menu_child_count(NULL) == 0);
    CHECK(menu_path(NULL, 0) == NULL);
    CHECK(menu_active(NULL) == NULL);
    CHECK(menu_view(NULL, it, 1) == 0);
    CHECK(menu_value_get(NULL, NULL) == 0);
    CHECK(menu_value_label(NULL, NULL) == NULL);
}

int main(void)
{
    test_add_child();
    test_full();
    test_navigation_and_events();
    test_values();
    test_confirm();
    test_hidden_disabled_and_view();
    test_static_tree_and_goto();
    test_depth_limit();
    test_timeout();
    test_null_safety();

    if (failures == 0) {
        printf("All tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
