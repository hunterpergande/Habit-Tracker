#include <gtk/gtk.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#endif

#define MAX_DAY_COUNT 80
#define DEFAULT_DAY_COUNT 60
#define ITEM_COUNT 10
#define NAME_LEN 64

static const char *default_item_names[ITEM_COUNT] = {
    "Habit 1",
    "Habit 2",
    "Habit 3",
    "Habit 4",
    "Habit 5",
    "Habit 6",
    "Habit 7",
    "Habit 8",
    "Habit 9",
    "Habit 10",
};

static char item_names[ITEM_COUNT][NAME_LEN];
static gboolean day_states[ITEM_COUNT][MAX_DAY_COUNT];
static int current_day_count = DEFAULT_DAY_COUNT;

static GtkWidget *main_window;
static GtkWidget *title_label;
static GtkWidget *complete_label;
static GtkWidget *grid;
static GtkWidget *day_count_combo;
static GtkWidget **check_buttons;
static GtkWidget *day_header_labels[MAX_DAY_COUNT];
static GtkWidget *habit_name_labels[ITEM_COUNT];
static GtkWidget *stats_summary_label;
static GtkWidget *weekly_label;
static GtkWidget *progress_graph_area;
static GtkWidget *rename_combo;
static GtkWidget *rename_entry;
static GtkWidget *day_action_display;
static int day_action_value = 1;
static int hover_day_index = -1;

static void on_export_stats(GtkButton *button, gpointer user_data);
static void on_reset(GtkButton *button, gpointer user_data);
static void on_toggle(GtkToggleButton *toggle, gpointer user_data);
static void refresh_all_ui(void);

static gboolean on_day_action_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    int value = day_action_value;

    if (event->direction == GDK_SCROLL_UP || event->delta_y < 0)
        value++;
    else if (event->direction == GDK_SCROLL_DOWN || event->delta_y > 0)
        value--;
    else
        return FALSE;

    if (value < 1)
        value = 1;
    if (value > current_day_count)
        value = current_day_count;

    day_action_value = value;
    if (day_action_display) {
        gchar *text = g_strdup_printf("%d", day_action_value);
        gtk_entry_set_text(GTK_ENTRY(day_action_display), text);
        g_free(text);
    }
    return TRUE;
}

static void step_day_action(int delta)
{
    if (!day_action_display)
        return;

    int value = day_action_value;
    value += delta;
    if (value < 1)
        value = 1;
    if (value > current_day_count)
        value = current_day_count;
    day_action_value = value;

    gchar *text = g_strdup_printf("%d", day_action_value);
    gtk_entry_set_text(GTK_ENTRY(day_action_display), text);
    g_free(text);
}

static void on_day_action_minus(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    step_day_action(-1);
}

static void on_day_action_plus(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    step_day_action(1);
}

static int normalize_day_count(int day_count)
{
    if (day_count == 7 || day_count == 30 || day_count == 60 || day_count == 80)
        return day_count;
    return DEFAULT_DAY_COUNT;
}

static void make_label_interactive(GtkWidget *label)
{
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_can_focus(label, FALSE);
}

static void init_default_names(void)
{
    for (int i = 0; i < ITEM_COUNT; i++)
        g_strlcpy(item_names[i], default_item_names[i], NAME_LEN);
}

static gboolean write_atomic_binary(const char *file_path, const void *data, size_t item_size, size_t item_count)
{
    gchar *tmp_path = g_strdup_printf("%s.tmp", file_path);
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        g_warning("could not open %s for writing: %s", tmp_path, g_strerror(errno));
        g_free(tmp_path);
        return FALSE;
    }

    size_t written = fwrite(data, item_size, item_count, f);
    gboolean ok = (written == item_count);

    if (ok && fflush(f) != 0)
        ok = FALSE;
    int file_descriptor;
#ifdef _WIN32
    file_descriptor = _fileno(f);
    if (ok && _commit(file_descriptor) != 0)
        ok = FALSE;
#else
    file_descriptor = fileno(f);
    if (ok && fsync(file_descriptor) != 0)
        ok = FALSE;
#endif
    if (fclose(f) != 0)
        ok = FALSE;

    if (!ok) {
        g_warning("failed writing %s safely", file_path);
        remove(tmp_path);
        g_free(tmp_path);
        return FALSE;
    }

    if (rename(tmp_path, file_path) != 0) {
        g_warning("failed replacing %s atomically: %s", file_path, g_strerror(errno));
        remove(tmp_path);
        g_free(tmp_path);
        return FALSE;
    }

    g_free(tmp_path);
    return TRUE;
}

static void apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css_data =
        "window {"
        "  background-image: linear-gradient(to bottom, #0f1520, #0b111a);"
        "}"
        "label { color: #e3e8f0; }"
        "#app-root { background-color: transparent; }"
        "#header-card, #tracker-card, #graph-card {"
        "  background-color: #151d27;"
        "  border: 1px solid #2a3444;"
        "  border-radius: 12px;"
        "}"
        "#title { font-size: 31pt; font-weight: 700; color: #ffffff; }"
        "#subtitle { font-size: 10pt; color: #8f9eb4; }"
        "#picker-label { font-size: 9pt; color: #91a1b8; }"
        "#stat-chip {"
        "  background-color: #1a2431;"
        "  border: 1px solid #33445b;"
        "  border-radius: 9px;"
        "  padding: 8px 12px;"
        "}"
        "#stat-value { font-size: 15pt; font-weight: 700; color: #ffffff; }"
        "#stat-caption { font-size: 9pt; color: #91a1b8; }"
        "#graph-title { font-size: 12pt; font-weight: 700; color: #d4deea; }"
        "#section-title { font-size: 10pt; font-weight: 700; color: #b8c5d9; }"
        "#graph-body { color: #a8b6c9; font-size: 10pt; }"
        "button { padding: 8px 16px; border-radius: 8px; font-weight: 600; }"
        "#reset-btn, #action-btn {"
        "  background-color: #1a2431;"
        "  border: 1px solid #3a4d67;"
        "  color: #e4ecf9;"
        "}"
        "#reset-btn:hover, #action-btn:hover { background-color: #223044; }"
        "entry {"
        "  background-color: #111823;"
        "  color: #dbe4f0;"
        "  border: 1px solid #2a3444;"
        "  border-radius: 7px;"
        "  padding: 5px 8px;"
        "}"
        "combobox#cycle-combo button, combobox#habit-combo button {"
        "  background-color: #111823;"
        "  color: #dbe4f0;"
        "  border: 1px solid #3a4d67;"
        "  border-radius: 8px;"
        "  padding: 5px 10px;"
        "}"
        "combobox#cycle-combo button:hover, combobox#habit-combo button:hover {"
        "  background-color: #182334;"
        "  border-color: #4f6787;"
        "}"
        "combobox#cycle-combo button:focus, combobox#habit-combo button:focus {"
        "  border-color: #6c8fbe;"
        "}"
        "combobox#cycle-combo arrow, combobox#habit-combo arrow { color: #9fb1c9; }"
        "combobox#cycle-combo *, combobox#habit-combo * { color: #dbe4f0; }"
        "menu {"
        "  background-color: #111823;"
        "  border: 1px solid #2a3444;"
        "}"
        "menu menuitem {"
        "  color: #dbe4f0;"
        "  padding: 6px 10px;"
        "}"
        "menu menuitem:hover {"
        "  background-color: #223044;"
        "  color: #f0f6ff;"
        "}"
        "#grid-label { color: #96a3b7; font-size: 9pt; }"
        "#habit-name { color: #e3ebf8; }"
        "#day-header { color: #7f90a7; font-size: 8pt; }"
        "separator { color: #2a3444; }"
        "checkbutton.habit-cell check {"
        "  background-color: #1b2431;"
        "  border: 1px solid #3c4a61;"
        "  border-radius: 5px;"
        "}"
        "checkbutton.habit-cell check:hover { border-color: #5e7495; }"
        "checkbutton.habit-cell check:checked {"
        "  background-color: #4ea85f;"
        "  border-color: #4ea85f;"
        "}";

    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    gtk_style_context_add_provider_for_screen(screen,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void save_states(void)
{
    write_atomic_binary("states.dat", day_states, sizeof(day_states[0][0]), ITEM_COUNT * MAX_DAY_COUNT);
}

static void load_states(void)
{
    FILE *f = fopen("states.dat", "rb");
    if (!f)
        return;

    size_t expected = ITEM_COUNT * MAX_DAY_COUNT;
    size_t read_count = fread(day_states, sizeof(day_states[0][0]), expected, f);
    fclose(f);

    if (read_count < expected) {
        memset(((gboolean *)day_states) + read_count, 0, (expected - read_count) * sizeof(gboolean));
    }
}

static void save_habit_names(void)
{
    write_atomic_binary("habits.dat", item_names, sizeof(item_names[0][0]), ITEM_COUNT * NAME_LEN);
}

static void load_habit_names(void)
{
    FILE *f = fopen("habits.dat", "rb");
    if (!f) {
        init_default_names();
        return;
    }

    size_t read_count = fread(item_names, sizeof(item_names[0][0]), ITEM_COUNT * NAME_LEN, f);
    fclose(f);

    if (read_count != (size_t)(ITEM_COUNT * NAME_LEN))
        init_default_names();

    for (int i = 0; i < ITEM_COUNT; i++) {
        item_names[i][NAME_LEN - 1] = '\0';
        if (item_names[i][0] == '\0')
            g_strlcpy(item_names[i], default_item_names[i], NAME_LEN);
    }
}

static void save_settings(void)
{
    write_atomic_binary("settings.dat", &current_day_count, sizeof(current_day_count), 1);
}

static void load_settings(void)
{
    FILE *f = fopen("settings.dat", "rb");
    if (!f)
        return;

    int loaded = DEFAULT_DAY_COUNT;
    if (fread(&loaded, sizeof(loaded), 1, f) == 1)
        current_day_count = normalize_day_count(loaded);
    fclose(f);
}

static int count_checked(void)
{
    int count = 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        for (int d = 0; d < current_day_count; d++) {
            if (day_states[i][d])
                count++;
        }
    }
    return count;
}

static int count_checked_for_habit(int item)
{
    int count = 0;
    for (int d = 0; d < current_day_count; d++) {
        if (day_states[item][d])
            count++;
    }
    return count;
}

static int count_checked_in_week(int week_index)
{
    int start_day = week_index * 7;
    int end_day = start_day + 6;
    if (end_day >= current_day_count)
        end_day = current_day_count - 1;

    int count = 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        for (int d = start_day; d <= end_day; d++) {
            if (day_states[i][d])
                count++;
        }
    }
    return count;
}

static double get_day_completion_percent(int day_index)
{
    if (day_index < 0 || day_index >= current_day_count)
        return 0.0;

    int day_checked = 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (day_states[i][day_index])
            day_checked++;
    }

    return (100.0 * day_checked) / ITEM_COUNT;
}

static double get_running_average_percent(int day_index)
{
    if (day_index < 0 || day_index >= current_day_count)
        return 0.0;

    double sum = 0.0;
    for (int d = 0; d <= day_index; d++)
        sum += get_day_completion_percent(d);

    return sum / (day_index + 1);
}

static gboolean on_progress_graph_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    (void)user_data;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    const double left = 44.0;
    const double right = 18.0;
    const double top = 14.0;
    const double bottom = 28.0;
    const double plot_w = width - left - right;
    const double plot_h = height - top - bottom;

    int new_hover_day = -1;

    if (plot_w > 0 && plot_h > 0 && current_day_count > 0) {
        if (event->x >= left && event->x <= (left + plot_w) &&
            event->y >= top && event->y <= (top + plot_h)) {
            double ratio = (event->x - left) / plot_w;
            if (ratio < 0.0)
                ratio = 0.0;
            if (ratio > 1.0)
                ratio = 1.0;
            new_hover_day = (int)(ratio * (current_day_count - 1) + 0.5);
        }
    }

    if (new_hover_day != hover_day_index) {
        hover_day_index = new_hover_day;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean on_progress_graph_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
    (void)event;
    (void)user_data;

    if (hover_day_index != -1) {
        hover_day_index = -1;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)widget;
    (void)user_data;

    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;

    if (ctrl && (event->keyval == GDK_KEY_e || event->keyval == GDK_KEY_E)) {
        on_export_stats(NULL, NULL);
        return TRUE;
    }

    if (ctrl && shift && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R)) {
        on_reset(NULL, NULL);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_draw_progress_graph(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void)user_data;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    if (width <= 0 || height <= 0)
        return FALSE;

    const double left = 44.0;
    const double right = 18.0;
    const double top = 14.0;
    const double bottom = 28.0;
    const double plot_w = width - left - right;
    const double plot_h = height - top - bottom;
    if (plot_w <= 0 || plot_h <= 0)
        return FALSE;

    cairo_set_source_rgb(cr, 0.08, 0.11, 0.16);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.16, 0.21, 0.28);
    cairo_rectangle(cr, left, top, plot_w, plot_h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);

    for (int pct = 0; pct <= 100; pct += 25) {
        double y = top + (100.0 - pct) * (plot_h / 100.0);

        cairo_set_source_rgba(cr, 0.43, 0.51, 0.63, 0.25);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, left + plot_w, y);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.65, 0.72, 0.82);
        cairo_move_to(cr, 8.0, y + 4.0);
        char label[8];
        snprintf(label, sizeof(label), "%d%%", pct);
        cairo_show_text(cr, label);
    }

    cairo_set_source_rgb(cr, 0.39, 0.48, 0.62);
    cairo_set_line_width(cr, 1.4);
    for (int d = 0; d < current_day_count; d++) {
        double x = (current_day_count > 1)
            ? left + ((double)d / (current_day_count - 1)) * plot_w
            : left + (plot_w * 0.5);
        double p = get_day_completion_percent(d);
        double y = top + (100.0 - p) * (plot_h / 100.0);
        if (d == 0)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.39, 0.75, 0.51);
    cairo_set_line_width(cr, 2.6);
    for (int d = 0; d < current_day_count; d++) {
        double x = (current_day_count > 1)
            ? left + ((double)d / (current_day_count - 1)) * plot_w
            : left + (plot_w * 0.5);
        double p = get_running_average_percent(d);
        double y = top + (100.0 - p) * (plot_h / 100.0);

        if (d == 0)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    if (current_day_count > 0) {
        int today_day = current_day_count - 1;
        double x_today = (current_day_count > 1)
            ? left + ((double)today_day / (current_day_count - 1)) * plot_w
            : left + (plot_w * 0.5);
        double p_today = get_running_average_percent(today_day);
        double y_today = top + (100.0 - p_today) * (plot_h / 100.0);

        cairo_set_source_rgba(cr, 0.93, 0.78, 0.37, 0.45);
        cairo_set_line_width(cr, 1.1);
        cairo_move_to(cr, x_today, top);
        cairo_line_to(cr, x_today, top + plot_h);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.93, 0.78, 0.37);
        cairo_arc(cr, x_today, y_today, 4.5, 0, 2 * G_PI);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.93, 0.78, 0.37);
        cairo_move_to(cr, x_today + 6.0, top + 12.0);
        cairo_show_text(cr, "Today");
    }

    int marker_count = (current_day_count <= 14) ? current_day_count : 8;
    for (int m = 0; m < marker_count; m++) {
        int day = (marker_count == 1) ? 0 : (m * (current_day_count - 1)) / (marker_count - 1);
        double x = (current_day_count > 1)
            ? left + ((double)day / (current_day_count - 1)) * plot_w
            : left + (plot_w * 0.5);

        cairo_set_source_rgba(cr, 0.63, 0.71, 0.82, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x, top + plot_h);
        cairo_line_to(cr, x, top + plot_h + 4.0);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.65, 0.72, 0.82);
        cairo_move_to(cr, x - 8.0, top + plot_h + 16.0);
        char day_text[12];
        snprintf(day_text, sizeof(day_text), "D%d", day + 1);
        cairo_show_text(cr, day_text);
    }

    if (plot_w >= 220.0) {
        double legend_x = left + plot_w - 118.0;
        double legend_y = top + 8.0;

        cairo_set_source_rgba(cr, 0.09, 0.13, 0.19, 0.88);
        cairo_rectangle(cr, legend_x, legend_y, 108.0, 42.0);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.39, 0.48, 0.62, 1.0);
        cairo_set_line_width(cr, 1.8);
        cairo_move_to(cr, legend_x + 8.0, legend_y + 14.0);
        cairo_line_to(cr, legend_x + 30.0, legend_y + 14.0);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.82, 0.88, 0.95);
        cairo_move_to(cr, legend_x + 36.0, legend_y + 17.0);
        cairo_show_text(cr, "Daily");

        cairo_set_source_rgba(cr, 0.39, 0.75, 0.51, 1.0);
        cairo_set_line_width(cr, 2.6);
        cairo_move_to(cr, legend_x + 8.0, legend_y + 31.0);
        cairo_line_to(cr, legend_x + 30.0, legend_y + 31.0);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.82, 0.88, 0.95);
        cairo_move_to(cr, legend_x + 36.0, legend_y + 34.0);
        cairo_show_text(cr, "Avg");
    }

    if (hover_day_index >= 0 && hover_day_index < current_day_count) {
        double x = (current_day_count > 1)
            ? left + ((double)hover_day_index / (current_day_count - 1)) * plot_w
            : left + (plot_w * 0.5);
        double daily = get_day_completion_percent(hover_day_index);
        double avg = get_running_average_percent(hover_day_index);
        int day_checked = 0;
        int total_checked_so_far = 0;
        for (int i = 0; i < ITEM_COUNT; i++) {
            if (day_states[i][hover_day_index])
                day_checked++;
            for (int d = 0; d <= hover_day_index; d++) {
                if (day_states[i][d])
                    total_checked_so_far++;
            }
        }
        int total_possible_so_far = ITEM_COUNT * (hover_day_index + 1);
        double y_daily = top + (100.0 - daily) * (plot_h / 100.0);
        double y_avg = top + (100.0 - avg) * (plot_h / 100.0);

        cairo_set_source_rgba(cr, 0.82, 0.88, 0.95, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x, top);
        cairo_line_to(cr, x, top + plot_h);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 0.39, 0.48, 0.62);
        cairo_arc(cr, x, y_daily, 3.0, 0, 2 * G_PI);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.39, 0.75, 0.51);
        cairo_arc(cr, x, y_avg, 4.0, 0, 2 * G_PI);
        cairo_fill(cr);

        double box_w = 220.0;
        double box_h = 64.0;
        double box_x = x + 10.0;
        double box_y = y_avg - 58.0;

        if (box_x + box_w > left + plot_w)
            box_x = x - box_w - 10.0;
        if (box_y < top + 4.0)
            box_y = top + 4.0;

        cairo_set_source_rgba(cr, 0.08, 0.12, 0.18, 0.92);
        cairo_rectangle(cr, box_x, box_y, box_w, box_h);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.86, 0.91, 0.98);
        cairo_move_to(cr, box_x + 8.0, box_y + 14.0);
        char hover_title[24];
        snprintf(hover_title, sizeof(hover_title), "Day %d", hover_day_index + 1);
        cairo_show_text(cr, hover_title);

        cairo_move_to(cr, box_x + 8.0, box_y + 30.0);
        char hover_daily[96];
        snprintf(hover_daily, sizeof(hover_daily), "Daily: %.2f%% (%d/%d)", daily, day_checked, ITEM_COUNT);
        cairo_show_text(cr, hover_daily);

        cairo_move_to(cr, box_x + 8.0, box_y + 47.0);
        char hover_avg[112];
        snprintf(hover_avg, sizeof(hover_avg), "Avg: %.2f%% (%d/%d)", avg, total_checked_so_far, total_possible_so_far);
        cairo_show_text(cr, hover_avg);
    }

    return FALSE;
}

static void update_tracker_title(void)
{
    gchar *title_text = g_strdup_printf("%d Day Tracker", current_day_count);
    gtk_label_set_text(GTK_LABEL(title_label), title_text);
    gtk_window_set_title(GTK_WINDOW(main_window), title_text);
    g_free(title_text);
}

static void update_day_column_visibility(void)
{
    for (int d = 0; d < MAX_DAY_COUNT; d++) {
        gboolean visible = (d < current_day_count);
        gtk_widget_set_visible(day_header_labels[d], visible);

        for (int i = 0; i < ITEM_COUNT; i++) {
            int idx = i * MAX_DAY_COUNT + d;
            gtk_widget_set_visible(check_buttons[idx], visible);
        }
    }
}

static void update_percentage(void)
{
    int total = ITEM_COUNT * current_day_count;
    int checked = count_checked();
    int percent = (total > 0) ? (checked * 100) / total : 0;
    gchar *text = g_strdup_printf("%d%%", percent);
    gtk_label_set_text(GTK_LABEL(complete_label), text);
    g_free(text);
}

static void update_habit_row_labels(void)
{
    for (int i = 0; i < ITEM_COUNT; i++) {
        int checked = count_checked_for_habit(i);
        int percent = (current_day_count > 0) ? (checked * 100) / current_day_count : 0;
        gchar *text = g_strdup_printf("%s (%d%%)", item_names[i], percent);
        gtk_label_set_text(GTK_LABEL(habit_name_labels[i]), text);
        g_free(text);
    }
}

static void rebuild_rename_combo(int selected_index)
{
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(rename_combo));
    for (int i = 0; i < ITEM_COUNT; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rename_combo), item_names[i]);

    if (selected_index < 0 || selected_index >= ITEM_COUNT)
        selected_index = 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(rename_combo), selected_index);
}

static void update_day_action_range(void)
{
    if (!day_action_display)
        return;

    if (day_action_value < 1)
        day_action_value = 1;
    if (day_action_value > current_day_count)
        day_action_value = current_day_count;

    gchar *text = g_strdup_printf("%d", day_action_value);
    gtk_entry_set_text(GTK_ENTRY(day_action_display), text);
    g_free(text);
}

static void apply_selected_day_to_all(gboolean value)
{
    if (!day_action_display)
        return;

    int day_index = day_action_value - 1;
    if (day_index < 0 || day_index >= current_day_count)
        return;

    for (int item = 0; item < ITEM_COUNT; item++) {
        int idx = item * MAX_DAY_COUNT + day_index;
        day_states[item][day_index] = value;
        g_signal_handlers_block_by_func(check_buttons[idx], on_toggle, NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_buttons[idx]), value);
        gtk_widget_queue_draw(check_buttons[idx]);
        g_signal_handlers_unblock_by_func(check_buttons[idx], on_toggle, NULL);
    }

    save_states();
    refresh_all_ui();
}

static void on_fill_day(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    apply_selected_day_to_all(TRUE);
}

static void on_clear_day(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    apply_selected_day_to_all(FALSE);
}

static void update_statistics_panel(void)
{
    int total = ITEM_COUNT * current_day_count;
    int checked = count_checked();
    int percent = (total > 0) ? (checked * 100) / total : 0;

    int best_idx = 0;
    int worst_idx = 0;
    int best_percent = -1;
    int worst_percent = 101;

    for (int i = 0; i < ITEM_COUNT; i++) {
        int habit_percent = (current_day_count > 0)
            ? (count_checked_for_habit(i) * 100) / current_day_count
            : 0;
        if (habit_percent > best_percent) {
            best_percent = habit_percent;
            best_idx = i;
        }
        if (habit_percent < worst_percent) {
            worst_percent = habit_percent;
            worst_idx = i;
        }
    }

    double average_per_habit = (double)checked / ITEM_COUNT;

    gchar *summary = g_strdup_printf(
        "• Total complete: %d / %d (%d%%)\n"
        "• Average per habit: %.1f / %d days\n"
        "• Best habit: %s (%d%%)\n"
        "• Needs focus: %s (%d%%)",
        checked, total, percent,
        average_per_habit, current_day_count,
        item_names[best_idx], best_percent,
        item_names[worst_idx], worst_percent);

    gtk_label_set_text(GTK_LABEL(stats_summary_label), summary);
    g_free(summary);

    int week_count = (current_day_count + 6) / 7;
    GString *weekly = g_string_new("");
    for (int w = 0; w < week_count; w++) {
        int start_day = (w * 7) + 1;
        int end_day = start_day + 6;
        if (end_day > current_day_count)
            end_day = current_day_count;

        int week_days = end_day - start_day + 1;
        int week_total = ITEM_COUNT * week_days;
        int week_checked = count_checked_in_week(w);
        int week_percent = (week_total > 0) ? (week_checked * 100) / week_total : 0;

        g_string_append_printf(weekly, "W%d (D%d-D%d): %d%%", w + 1, start_day, end_day, week_percent);
        if (w < week_count - 1) {
            if ((w + 1) % 3 == 0)
                g_string_append(weekly, "\n");
            else
                g_string_append(weekly, "    ");
        }
    }

    gtk_label_set_text(GTK_LABEL(weekly_label), weekly->str);
    g_string_free(weekly, TRUE);
}

static void refresh_all_ui(void)
{
    update_tracker_title();
    update_day_column_visibility();
    update_day_action_range();
    update_percentage();
    update_habit_row_labels();
    update_statistics_panel();
    if (progress_graph_area)
        gtk_widget_queue_draw(progress_graph_area);
}

static void on_toggle(GtkToggleButton *toggle, gpointer user_data)
{
    intptr_t idx = (intptr_t)user_data;
    int item = idx / MAX_DAY_COUNT;
    int day = idx % MAX_DAY_COUNT;
    day_states[item][day] = gtk_toggle_button_get_active(toggle);
    save_states();
    refresh_all_ui();
    gtk_widget_queue_draw(GTK_WIDGET(toggle));
}

static void on_day_count_changed(GtkComboBox *combo, gpointer user_data)
{
    (void)user_data;
    const gchar *id = gtk_combo_box_get_active_id(combo);
    if (!id)
        return;

    int new_day_count = normalize_day_count(atoi(id));
    if (new_day_count == current_day_count)
        return;

    current_day_count = new_day_count;
    save_settings();
    refresh_all_ui();
}

static void perform_full_reset(void)
{
    memset(day_states, 0, sizeof(day_states));
    save_states();

    for (int item = 0; item < ITEM_COUNT; item++) {
        for (int day = 0; day < MAX_DAY_COUNT; day++) {
            int idx = item * MAX_DAY_COUNT + day;
            g_signal_handlers_block_by_func(check_buttons[idx], on_toggle, NULL);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_buttons[idx]), FALSE);
            gtk_widget_queue_draw(check_buttons[idx]);
            g_signal_handlers_unblock_by_func(check_buttons[idx], on_toggle, NULL);
        }
    }

    refresh_all_ui();
}

static void on_reset(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Reset all habit progress for all days?");

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "This clears all checked boxes in the tracker.");

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_YES)
        perform_full_reset();
}

static void on_export_stats(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    FILE *f = fopen("stats_export.txt", "w");
    if (!f) {
        g_warning("could not open stats_export.txt for writing");
        return;
    }

    int total = ITEM_COUNT * current_day_count;
    int checked = count_checked();
    int total_percent = (total > 0) ? (checked * 100) / total : 0;

    fprintf(f, "%d-Day Tracker Export\n", current_day_count);
    fprintf(f, "===================\n\n");
    fprintf(f, "Overall: %d/%d (%d%%)\n\n", checked, total, total_percent);

    fprintf(f, "Per-habit completion:\n");
    for (int i = 0; i < ITEM_COUNT; i++) {
        int habit_checked = count_checked_for_habit(i);
        int habit_percent = (current_day_count > 0)
            ? (habit_checked * 100) / current_day_count
            : 0;
        fprintf(f, "- %s: %d/%d (%d%%)\n", item_names[i], habit_checked, current_day_count, habit_percent);
    }

    fprintf(f, "\nWeekly breakdown:\n");
    int week_count = (current_day_count + 6) / 7;
    for (int w = 0; w < week_count; w++) {
        int start_day = (w * 7) + 1;
        int end_day = start_day + 6;
        if (end_day > current_day_count)
            end_day = current_day_count;

        int week_days = end_day - start_day + 1;
        int week_total = ITEM_COUNT * week_days;
        int week_checked = count_checked_in_week(w);
        int week_percent = (week_total > 0) ? (week_checked * 100) / week_total : 0;

        fprintf(f, "- Week %d (Day %d-%d): %d/%d (%d%%)\n",
                w + 1, start_day, end_day, week_checked, week_total, week_percent);
    }

    fclose(f);

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Stats exported successfully.");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Saved to stats_export.txt in your app folder.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_rename_habit(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(rename_combo));
    const gchar *new_name = gtk_entry_get_text(GTK_ENTRY(rename_entry));

    if (selected < 0 || selected >= ITEM_COUNT)
        return;

    if (!new_name)
        return;

    gchar *trimmed = g_strstrip(g_strdup(new_name));
    if (!trimmed || trimmed[0] == '\0') {
        g_free(trimmed);
        return;
    }

    g_strlcpy(item_names[selected], trimmed, NAME_LEN);
    g_free(trimmed);

    save_habit_names();

    refresh_all_ui();
    rebuild_rename_combo(selected);
    gtk_entry_set_text(GTK_ENTRY(rename_entry), "");
}

static void on_clear_habit(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(rename_combo));
    if (selected < 0 || selected >= ITEM_COUNT)
        return;

    for (int day = 0; day < MAX_DAY_COUNT; day++) {
        int idx = selected * MAX_DAY_COUNT + day;
        day_states[selected][day] = FALSE;
        g_signal_handlers_block_by_func(check_buttons[idx], on_toggle, NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_buttons[idx]), FALSE);
        gtk_widget_queue_draw(check_buttons[idx]);
        g_signal_handlers_unblock_by_func(check_buttons[idx], on_toggle, NULL);
    }

    save_states();
    refresh_all_ui();
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    load_states();
    load_habit_names();
    load_settings();
    apply_css();

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1400, 800);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(main_window, "key-press-event", G_CALLBACK(on_window_key_press), NULL);

    GtkWidget *page_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(page_scroll), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(main_window), page_scroll);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_name(vbox, "app-root");
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(page_scroll), vbox);

    GtkWidget *header_frame = gtk_frame_new(NULL);
    gtk_widget_set_name(header_frame, "header-card");
    gtk_frame_set_shadow_type(GTK_FRAME(header_frame), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(vbox), header_frame, FALSE, FALSE, 0);

    GtkWidget *header_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
    gtk_container_set_border_width(GTK_CONTAINER(header_hbox), 14);
    gtk_container_add(GTK_CONTAINER(header_frame), header_hbox);

    GtkWidget *title_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    title_label = gtk_label_new("");
    gtk_widget_set_name(title_label, "title");
    make_label_interactive(title_label);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(title_vbox), title_label, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("Discipline > Motivation");
    gtk_widget_set_name(subtitle, "subtitle");
    make_label_interactive(subtitle);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(title_vbox), subtitle, FALSE, FALSE, 0);

    GtkWidget *picker_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(picker_row, GTK_ALIGN_START);

    GtkWidget *picker_label = gtk_label_new("Cycle Length:");
    gtk_widget_set_name(picker_label, "picker-label");
    make_label_interactive(picker_label);
    gtk_widget_set_halign(picker_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(picker_row), picker_label, FALSE, FALSE, 0);

    day_count_combo = gtk_combo_box_text_new();
    gtk_widget_set_name(day_count_combo, "cycle-combo");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(day_count_combo), "7", "7 Days");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(day_count_combo), "30", "30 Days");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(day_count_combo), "60", "60 Days");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(day_count_combo), "80", "80 Days");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(day_count_combo),
        (current_day_count == 7) ? "7" :
        (current_day_count == 30) ? "30" :
        (current_day_count == 80) ? "80" : "60");
    g_signal_connect(day_count_combo, "changed", G_CALLBACK(on_day_count_changed), NULL);
    gtk_widget_set_size_request(day_count_combo, 120, 34);
    gtk_widget_set_tooltip_text(day_count_combo, "Pick tracker cycle length (7, 30, 60, 80 days)");
    gtk_box_pack_start(GTK_BOX(picker_row), day_count_combo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(title_vbox), picker_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_hbox), title_vbox, TRUE, TRUE, 0);

    GtkWidget *stats_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(stats_hbox, GTK_ALIGN_END);

    GtkWidget *stat_chip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(stat_chip, "stat-chip");
    gtk_widget_set_halign(stat_chip, GTK_ALIGN_END);

    complete_label = gtk_label_new("0%");
    gtk_widget_set_name(complete_label, "stat-value");
    make_label_interactive(complete_label);
    gtk_widget_set_halign(complete_label, GTK_ALIGN_END);

    GtkWidget *complete_text = gtk_label_new("Complete");
    gtk_widget_set_name(complete_text, "stat-caption");
    make_label_interactive(complete_text);
    gtk_widget_set_halign(complete_text, GTK_ALIGN_END);

    gtk_box_pack_start(GTK_BOX(stat_chip), complete_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stat_chip), complete_text, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(stats_hbox), stat_chip, FALSE, FALSE, 0);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_set_name(reset_btn, "reset-btn");
    gtk_widget_set_tooltip_text(reset_btn, "Reset all progress (Ctrl+Shift+R)");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset), NULL);
    gtk_box_pack_end(GTK_BOX(stats_hbox), reset_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header_hbox), stats_hbox, FALSE, FALSE, 0);

    GtkWidget *tracker_frame = gtk_frame_new(NULL);
    gtk_widget_set_name(tracker_frame, "tracker-card");
    gtk_frame_set_shadow_type(GTK_FRAME(tracker_frame), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(vbox), tracker_frame, FALSE, FALSE, 0);

    GtkWidget *scrollwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrollwin), GTK_SHADOW_NONE);
    gtk_container_set_border_width(GTK_CONTAINER(scrollwin), 8);
    gtk_container_add(GTK_CONTAINER(tracker_frame), scrollwin);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    GtkWidget *hash_label = gtk_label_new("#");
    gtk_widget_set_name(hash_label, "day-header");
    gtk_widget_set_size_request(hash_label, 30, -1);
    gtk_grid_attach(GTK_GRID(grid), hash_label, 0, 0, 1, 1);

    GtkWidget *habits_label = gtk_label_new("Habits");
    gtk_widget_set_name(habits_label, "day-header");
    make_label_interactive(habits_label);
    gtk_widget_set_size_request(habits_label, 200, -1);
    gtk_widget_set_halign(habits_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), habits_label, 1, 0, 1, 1);

    for (int d = 0; d < MAX_DAY_COUNT; d++) {
        GtkWidget *day_label = gtk_label_new("");
        gtk_widget_set_name(day_label, "day-header");
        gchar *day_text = g_strdup_printf("%d", d + 1);
        gtk_label_set_text(GTK_LABEL(day_label), day_text);
        g_free(day_text);
        gtk_widget_set_size_request(day_label, 25, -1);
        gtk_grid_attach(GTK_GRID(grid), day_label, d + 2, 0, 1, 1);
        day_header_labels[d] = day_label;
    }

    check_buttons = g_malloc(ITEM_COUNT * MAX_DAY_COUNT * sizeof(GtkWidget *));

    for (int i = 0; i < ITEM_COUNT; i++) {
        GtkWidget *row_num_label = gtk_label_new("");
        gtk_widget_set_name(row_num_label, "grid-label");
        gchar *row_text = g_strdup_printf("%d", i + 1);
        gtk_label_set_text(GTK_LABEL(row_num_label), row_text);
        g_free(row_text);
        gtk_widget_set_size_request(row_num_label, 30, -1);
        gtk_grid_attach(GTK_GRID(grid), row_num_label, 0, i + 1, 1, 1);

        GtkWidget *habit_name = gtk_label_new("");
        gtk_widget_set_name(habit_name, "habit-name");
        make_label_interactive(habit_name);
        gtk_widget_set_size_request(habit_name, 200, -1);
        gtk_widget_set_halign(habit_name, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), habit_name, 1, i + 1, 1, 1);
        habit_name_labels[i] = habit_name;

        for (int d = 0; d < MAX_DAY_COUNT; d++) {
            GtkWidget *check = gtk_check_button_new();
            GtkStyleContext *check_ctx = gtk_widget_get_style_context(check);
            gtk_style_context_add_class(check_ctx, "habit-cell");
            gtk_widget_set_size_request(check, 25, 25);

            if (day_states[i][d])
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);

            int idx = i * MAX_DAY_COUNT + d;
            check_buttons[idx] = check;
            gtk_grid_attach(GTK_GRID(grid), check, d + 2, i + 1, 1, 1);
            g_signal_connect(check, "toggled", G_CALLBACK(on_toggle), GINT_TO_POINTER(idx));
        }
    }

    gtk_container_add(GTK_CONTAINER(scrollwin), grid);

    GtkWidget *graph = gtk_frame_new(NULL);
    gtk_widget_set_name(graph, "graph-card");
    gtk_frame_set_shadow_type(GTK_FRAME(graph), GTK_SHADOW_NONE);
    gtk_widget_set_size_request(graph, -1, 120);

    GtkWidget *graph_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(graph_box), 14);
    gtk_container_add(GTK_CONTAINER(graph), graph_box);

    GtkWidget *graph_title = gtk_label_new("Statistics & Insights");
    gtk_widget_set_name(graph_title, "graph-title");
    make_label_interactive(graph_title);
    gtk_widget_set_halign(graph_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), graph_title, FALSE, FALSE, 0);

    GtkWidget *overview_title = gtk_label_new("Overview");
    gtk_widget_set_name(overview_title, "section-title");
    make_label_interactive(overview_title);
    gtk_widget_set_halign(overview_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), overview_title, FALSE, FALSE, 0);

    stats_summary_label = gtk_label_new("");
    gtk_widget_set_name(stats_summary_label, "graph-body");
    make_label_interactive(stats_summary_label);
    gtk_label_set_xalign(GTK_LABEL(stats_summary_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(stats_summary_label), TRUE);
    gtk_box_pack_start(GTK_BOX(graph_box), stats_summary_label, FALSE, FALSE, 0);

    GtkWidget *sep_top = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(graph_box), sep_top, FALSE, FALSE, 0);

    GtkWidget *weekly_title = gtk_label_new("Weekly");
    gtk_widget_set_name(weekly_title, "section-title");
    make_label_interactive(weekly_title);
    gtk_widget_set_halign(weekly_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), weekly_title, FALSE, FALSE, 0);

    weekly_label = gtk_label_new("");
    gtk_widget_set_name(weekly_label, "graph-body");
    make_label_interactive(weekly_label);
    gtk_label_set_xalign(GTK_LABEL(weekly_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(weekly_label), TRUE);
    gtk_box_pack_start(GTK_BOX(graph_box), weekly_label, FALSE, FALSE, 0);

    GtkWidget *sep_graph = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(graph_box), sep_graph, FALSE, FALSE, 0);

    GtkWidget *trend_title = gtk_label_new("Daily Progress Trend");
    gtk_widget_set_name(trend_title, "section-title");
    make_label_interactive(trend_title);
    gtk_widget_set_halign(trend_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), trend_title, FALSE, FALSE, 0);

    progress_graph_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(progress_graph_area, -1, 170);
    gtk_widget_set_hexpand(progress_graph_area, TRUE);
    gtk_widget_set_vexpand(progress_graph_area, FALSE);
    gtk_widget_set_tooltip_text(progress_graph_area, "Hover to inspect exact daily and running-average percentages");
    gtk_widget_add_events(progress_graph_area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(progress_graph_area, "draw", G_CALLBACK(on_draw_progress_graph), NULL);
    g_signal_connect(progress_graph_area, "motion-notify-event", G_CALLBACK(on_progress_graph_motion), NULL);
    g_signal_connect(progress_graph_area, "leave-notify-event", G_CALLBACK(on_progress_graph_leave), NULL);
    gtk_box_pack_start(GTK_BOX(graph_box), progress_graph_area, FALSE, FALSE, 0);

    GtkWidget *sep_bottom = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(graph_box), sep_bottom, FALSE, FALSE, 0);

    GtkWidget *controls_title = gtk_label_new("Actions");
    gtk_widget_set_name(controls_title, "section-title");
    make_label_interactive(controls_title);
    gtk_widget_set_halign(controls_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), controls_title, FALSE, FALSE, 0);

    GtkWidget *controls_input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(controls_input_row, GTK_ALIGN_START);
    gtk_widget_set_hexpand(controls_input_row, FALSE);
    gtk_widget_set_size_request(controls_input_row, 440, -1);
    gtk_box_pack_start(GTK_BOX(graph_box), controls_input_row, FALSE, FALSE, 0);

    rename_combo = gtk_combo_box_text_new();
    gtk_widget_set_name(rename_combo, "habit-combo");
    gtk_widget_set_size_request(rename_combo, 180, 34);
    gtk_widget_set_hexpand(rename_combo, FALSE);
    gtk_box_pack_start(GTK_BOX(controls_input_row), rename_combo, FALSE, FALSE, 0);

    rename_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(rename_entry), "New habit name");
    gtk_entry_set_width_chars(GTK_ENTRY(rename_entry), 18);
    gtk_widget_set_size_request(rename_entry, 240, -1);
    gtk_widget_set_hexpand(rename_entry, FALSE);
    gtk_box_pack_start(GTK_BOX(controls_input_row), rename_entry, FALSE, FALSE, 0);

    GtkWidget *day_ops_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(day_ops_row, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(graph_box), day_ops_row, FALSE, FALSE, 0);

    GtkWidget *day_ops_label = gtk_label_new("Day Ops:");
    gtk_widget_set_name(day_ops_label, "picker-label");
    make_label_interactive(day_ops_label);
    gtk_box_pack_start(GTK_BOX(day_ops_row), day_ops_label, FALSE, FALSE, 0);

    day_action_display = gtk_entry_new();
    gtk_widget_set_size_request(day_action_display, 88, 34);
    gtk_entry_set_alignment(GTK_ENTRY(day_action_display), 0.5f);
    gtk_editable_set_editable(GTK_EDITABLE(day_action_display), FALSE);
    gtk_widget_set_can_focus(day_action_display, FALSE);
    gtk_widget_add_events(day_action_display, GDK_SCROLL_MASK);
    g_signal_connect(day_action_display, "scroll-event", G_CALLBACK(on_day_action_scroll), NULL);
    gtk_widget_set_tooltip_text(day_action_display, "Choose day number for quick fill/clear");
    gtk_box_pack_start(GTK_BOX(day_ops_row), day_action_display, FALSE, FALSE, 0);

    GtkWidget *day_minus_btn = gtk_button_new_with_label("-");
    gtk_widget_set_name(day_minus_btn, "action-btn");
    gtk_widget_set_size_request(day_minus_btn, 34, 34);
    gtk_widget_set_tooltip_text(day_minus_btn, "Previous day");
    g_signal_connect(day_minus_btn, "clicked", G_CALLBACK(on_day_action_minus), NULL);
    gtk_box_pack_start(GTK_BOX(day_ops_row), day_minus_btn, FALSE, FALSE, 0);

    GtkWidget *day_plus_btn = gtk_button_new_with_label("+");
    gtk_widget_set_name(day_plus_btn, "action-btn");
    gtk_widget_set_size_request(day_plus_btn, 34, 34);
    gtk_widget_set_tooltip_text(day_plus_btn, "Next day");
    g_signal_connect(day_plus_btn, "clicked", G_CALLBACK(on_day_action_plus), NULL);
    gtk_box_pack_start(GTK_BOX(day_ops_row), day_plus_btn, FALSE, FALSE, 0);

    GtkWidget *fill_day_btn = gtk_button_new_with_label("Fill Day");
    gtk_widget_set_name(fill_day_btn, "action-btn");
    gtk_widget_set_tooltip_text(fill_day_btn, "Mark all habits complete for selected day");
    g_signal_connect(fill_day_btn, "clicked", G_CALLBACK(on_fill_day), NULL);
    gtk_box_pack_start(GTK_BOX(day_ops_row), fill_day_btn, FALSE, FALSE, 0);

    GtkWidget *clear_day_btn = gtk_button_new_with_label("Clear Day");
    gtk_widget_set_name(clear_day_btn, "action-btn");
    gtk_widget_set_tooltip_text(clear_day_btn, "Clear all habits for selected day");
    g_signal_connect(clear_day_btn, "clicked", G_CALLBACK(on_clear_day), NULL);
    gtk_box_pack_start(GTK_BOX(day_ops_row), clear_day_btn, FALSE, FALSE, 0);

    GtkWidget *controls_buttons_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(controls_buttons_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(controls_buttons_flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(controls_buttons_flow), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(controls_buttons_flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(controls_buttons_flow), 8);
    gtk_widget_set_halign(controls_buttons_flow, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(controls_buttons_flow, TRUE);
    gtk_box_pack_start(GTK_BOX(graph_box), controls_buttons_flow, FALSE, FALSE, 0);

    GtkWidget *rename_btn = gtk_button_new_with_label("Rename Habit");
    gtk_widget_set_name(rename_btn, "action-btn");
    gtk_widget_set_tooltip_text(rename_btn, "Rename selected habit");
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_rename_habit), NULL);
    gtk_container_add(GTK_CONTAINER(controls_buttons_flow), rename_btn);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear Habit");
    gtk_widget_set_name(clear_btn, "action-btn");
    gtk_widget_set_tooltip_text(clear_btn, "Clear all checked days for selected habit");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_habit), NULL);
    gtk_container_add(GTK_CONTAINER(controls_buttons_flow), clear_btn);

    GtkWidget *export_btn = gtk_button_new_with_label("Export Stats");
    gtk_widget_set_name(export_btn, "action-btn");
    gtk_widget_set_tooltip_text(export_btn, "Export stats to file (Ctrl+E)");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_export_stats), NULL);
    gtk_container_add(GTK_CONTAINER(controls_buttons_flow), export_btn);

    gtk_box_pack_start(GTK_BOX(vbox), graph, TRUE, TRUE, 0);

    rebuild_rename_combo(0);
    refresh_all_ui();
    gtk_widget_show_all(main_window);
    gtk_main();

    g_free(check_buttons);
    return 0;
}
