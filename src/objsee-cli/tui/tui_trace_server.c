//
//  new_main.c
//  objsee-cli
//
//  Created by Ethan Arbuckle on 12/21/24.
//
#include <CoreFoundation/CoreFoundation.h>
#include <json-c/json_tokener.h>
#include <netinet/in.h>
#include "format.h"

#if TARGET_OS_MAC && !TARGET_OS_IPHONE
#include <curses.h>
#else
#include <ncurses.h>
#endif

#define UPDATE_THRESHOLD_EVENT_COUNT 20
#define INACTIVE_THRESHOLD 3
#define MAX_COLUMNS 5
#define BORDER_WIDTH 3
#define MIN_COLUMN_WIDTH 45

#define COLOR_PAIR_THREAD1 1
#define COLOR_PAIR_THREAD2 2
#define COLOR_PAIR_THREAD3 3
#define COLOR_PAIR_THREAD4 4
#define COLOR_PAIR_THREAD5 5
#define COLOR_PAIR_THREAD6 6
#define COLOR_PAIR_THREAD7 7
#define COLOR_PAIR_THREAD8 8
#define COLOR_PAIR_NORMAL 9
#define COLOR_PAIR_METHOD 10
#define COLOR_PAIR_CLASS 11
#define COLOR_PAIR_ARGS 12
#define COLOR_PAIR_DEPTH 13
#define COLOR_PAIR_HEADER 14
#define COLOR_MEMORY_SIZE 3

typedef struct {
    WINDOW *win;
    uint64_t thread_id;
    int current_line;
    int max_lines;
    int scroll_pos;
    char **line_buffer;
    int buffer_size;
    int buffer_capacity;
    time_t last_activity;
    int min_visible_depth;
    int color_pair;
    int keep_visible;
} thread_view_t;

typedef struct {
    WINDOW *header;
    thread_view_t **threads;
    int thread_count;
    bool running;
    int active_thread;
} tracer_ui_t;


static int server_fd = -1;
static int client_fd = -1;
static int last_used_colors[COLOR_MEMORY_SIZE] = {0};
static int color_memory_index = 0;
static int update_counter = 0;
static tracer_ui_t *g_ui = NULL;

static void redraw_thread_window(thread_view_t *tv);

static void setup_colors(void) {
    start_color();
    use_default_colors();
    
    init_pair(COLOR_PAIR_THREAD1, COLOR_BLUE, -1);
    init_pair(COLOR_PAIR_THREAD2, COLOR_CYAN, -1);
    init_pair(COLOR_PAIR_THREAD3, COLOR_GREEN, -1);
    init_pair(COLOR_PAIR_THREAD4, COLOR_MAGENTA, -1);
    init_pair(COLOR_PAIR_THREAD5, COLOR_YELLOW, -1);
    init_pair(COLOR_PAIR_THREAD6, COLOR_RED, -1);
    init_pair(COLOR_PAIR_THREAD7, COLOR_WHITE, -1);
    init_pair(COLOR_PAIR_THREAD8, COLOR_RED, -1);
    init_pair(COLOR_PAIR_NORMAL, COLOR_WHITE, -1);
    init_pair(COLOR_PAIR_METHOD, COLOR_GREEN, -1);
    init_pair(COLOR_PAIR_CLASS, COLOR_YELLOW, -1);
    init_pair(COLOR_PAIR_ARGS, COLOR_CYAN, -1);
    init_pair(COLOR_PAIR_DEPTH, COLOR_MAGENTA, -1);
    init_pair(COLOR_PAIR_HEADER, COLOR_WHITE, -1);
    
    for (int i = 0; i < COLOR_MEMORY_SIZE; i++) {
        last_used_colors[i] = -1;
    }
}

static void record_used_color(int color_index) {
    last_used_colors[color_memory_index] = color_index;
    color_memory_index = (color_memory_index + 1) % COLOR_MEMORY_SIZE;
}

static bool was_color_recently_used(int color_index) {
    for (int i = 0; i < COLOR_MEMORY_SIZE; i++) {
        if (last_used_colors[i] == color_index) {
            return true;
        }
    }
    return false;
}

static int get_next_color(void) {
    bool used_colors[8] = {false};
    for (int i = 0; i < g_ui->thread_count; i++) {
        if (g_ui->threads[i]) {
            int color_index = (g_ui->threads[i]->color_pair - COLOR_PAIR_THREAD1);
            if (color_index >= 0 && color_index < 8) {
                used_colors[color_index] = true;
            }
        }
    }
    
    for (int i = 0; i < 8; i++) {
        if (!used_colors[i] && !was_color_recently_used(i)) {
            int color = COLOR_PAIR_THREAD1 + i;
            record_used_color(i);
            return color;
        }
    }
    
    for (int i = 0; i < 8; i++) {
        if (!used_colors[i]) {
            int color = COLOR_PAIR_THREAD1 + i;
            record_used_color(i);
            return color;
        }
    }
    
    int color = COLOR_PAIR_THREAD1 + (g_ui->thread_count % 8);
    return color;
}

static void apply_active_highlight(WINDOW *win, int color_pair, bool is_active) {
    if (is_active) {
        int highlight_pair = COLOR_WHITE;
        wattron(win, COLOR_PAIR(highlight_pair) | A_BOLD);
        wbkgd(win, COLOR_PAIR(highlight_pair) | A_BOLD);
    }
    else {
        wattroff(win, A_BOLD);
        wbkgd(win, COLOR_PAIR(color_pair));
    }
}

static int calculate_indent_depth(const char *line) {
    int depth = 0;
    // todo fixme
    while ((*line == ' ' || *line == '|') && *(line + 1) != '[') {
        depth++;
        line++;
    }
    return depth;
}

static void normalize_visible_indents(thread_view_t *tv) {
    if (tv == NULL || tv->line_buffer == NULL) {
        return;
    }
    
    tv->min_visible_depth = INT_MAX;
    for (int i = tv->scroll_pos; i < tv->scroll_pos + tv->max_lines && i < tv->buffer_size; i++) {
        if (tv->line_buffer[i]) {
            int depth = calculate_indent_depth(tv->line_buffer[i]);
            if (depth < tv->min_visible_depth) {
                tv->min_visible_depth = depth;
            }
        }
    }
}

static void cleanup_thread_view(thread_view_t *tv) {
    if (tv == NULL) {
        return;
    }
    
    if (tv->win) {
        delwin(tv->win);
    }
    
    if (tv->line_buffer) {
        for (int i = 0; i < tv->buffer_size; i++) {
            free(tv->line_buffer[i]);
        }
        free(tv->line_buffer);
    }
    free(tv);
}

static void resize_all_windows(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    int available_width = max_x;
    int column_width = (available_width / g_ui->thread_count);
    if (column_width < MIN_COLUMN_WIDTH) {
        column_width = MIN_COLUMN_WIDTH;
    }
    
    int x_pos = 0;
    for (int i = 0; i < g_ui->thread_count; i++) {
        thread_view_t* tv = g_ui->threads[i];
        if (tv == NULL || tv->win == NULL) {
            continue;
        }
        
        wresize(tv->win, max_y - 2, column_width);
        mvwin(tv->win, 2, x_pos);
        
        x_pos += column_width;
        werase(tv->win);
    }
    
    wclear(stdscr);
    wnoutrefresh(stdscr);
    
    for (int i = 0; i < g_ui->thread_count; i++) {
        if (g_ui->threads[i]) {
            redraw_thread_window(g_ui->threads[i]);
        }
    }
    
    wresize(g_ui->header, 2, max_x);
    wrefresh(g_ui->header);
    doupdate();
}

static void cleanup_inactive_threads(void) {
    if (g_ui == NULL || g_ui->threads == NULL) {
        return;
    }
    
    if (g_ui->thread_count <= MAX_COLUMNS) {
        return;
    }
    
    time_t current_time = time(NULL);
    bool did_cleanup = false;
    
    for (int i = g_ui->thread_count - 1; i >= 0; i--) {
        if (g_ui->threads[i] && g_ui->threads[i]->keep_visible == 0 && (current_time - g_ui->threads[i]->last_activity) > INACTIVE_THRESHOLD) {
            
            cleanup_thread_view(g_ui->threads[i]);
            
            for (size_t j = i; j < g_ui->thread_count - 1; j++) {
                g_ui->threads[j] = g_ui->threads[j + 1];
            }
            g_ui->threads[g_ui->thread_count - 1] = NULL;
            g_ui->thread_count--;
            did_cleanup = true;
        }
    }
    
    if (did_cleanup) {
        resize_all_windows();
    }
}

static thread_view_t *get_or_create_thread_view(uint64_t thread_id, bool *did_create) {
    if (g_ui == NULL || g_ui->threads == NULL) {
        printf("UI not initialized\n");
        return NULL;
    }
    
    if (did_create) {
        *did_create = false;
    }

    thread_view_t *tv = NULL;
    for (size_t i = 0; i < g_ui->thread_count; i++) {
        if (g_ui->threads[i] && g_ui->threads[i]->thread_id == thread_id) {
            g_ui->threads[i]->last_activity = time(NULL);
            return g_ui->threads[i];
        }
    }
        
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int width = (max_x / (g_ui->thread_count + 1)) - BORDER_WIDTH;
    if (width < MIN_COLUMN_WIDTH || g_ui->thread_count >= MAX_COLUMNS) {
        return NULL;
    }
    
    tv = calloc(1, sizeof(thread_view_t));
    if (tv == NULL) {
        return NULL;
    }
    
    tv->thread_id = thread_id;
    tv->current_line = 0;
    tv->last_activity = time(NULL);
    tv->color_pair = get_next_color();
    tv->buffer_capacity = 2000;
    tv->buffer_size = 0;
    tv->scroll_pos = 0;
    tv->max_lines = (max_y - 2) - 2;
    tv->keep_visible = (g_ui->thread_count == 0);
    tv->line_buffer = calloc(tv->buffer_capacity, sizeof(char *));
    if (tv->line_buffer == NULL) {
        free(tv);
        return NULL;
    }
    
    wattron(tv->win, tv->color_pair | A_BOLD);
    wbkgd(tv->win, tv->color_pair);
    
    tv->win = newwin(max_y - 2, width, 2, g_ui->thread_count  *(width + BORDER_WIDTH));
    if (tv->win == NULL) {
        free(tv);
        free(tv->line_buffer);
        return NULL;
    }
    
    for (int i = 0; i < g_ui->thread_count; i++) {
        thread_view_t *existing = g_ui->threads[i];
        if (existing && existing->win) {
            
            wresize(existing->win, max_y - 2, width);
            mvwin(existing->win, 2, i * width);
            
            box(existing->win, 0, 0);
            wattron(existing->win, A_BOLD);
            mvwprintw(existing->win, 0, 2, " Thread %llu ", existing->thread_id);
            wattroff(existing->win, A_BOLD);
            wrefresh(existing->win);
        }
    }
    
    tv->win = newwin(max_y - 2, width, 2, g_ui->thread_count * width);
    if (tv->win == NULL) {
        free(tv);
        free(tv->line_buffer);
        printf("Failed to create window\n");
        return NULL;
    }
    
    scrollok(tv->win, TRUE);
    
    thread_view_t **new_threads = realloc(g_ui->threads, (g_ui->thread_count + 1)  *sizeof(thread_view_t *));
    if (new_threads == NULL) {
        free(tv->line_buffer);
        delwin(tv->win);
        free(tv);
        return NULL;
    }
    
    g_ui->threads = new_threads;
    g_ui->threads[g_ui->thread_count++] = tv;
    
    box(tv->win, 0, 0);
    wattron(tv->win, A_BOLD);
    mvwprintw(tv->win, 0, 2, " Thread %llu ", thread_id);
    wattroff(tv->win, A_BOLD);
    wrefresh(tv->win);
    
    resize_all_windows();
    
    if (did_create) {
        *did_create = true;
    }
    return tv;
}

static void record_line_for_thread(thread_view_t *tv, const char *line) {
    if (tv == NULL || line == NULL) {
        return;
    }
    
    tv->last_activity = time(NULL);
    bool was_at_bottom = (tv->scroll_pos >= tv->buffer_size - tv->max_lines);
    
    if (tv->buffer_size >= tv->buffer_capacity) {
        free(tv->line_buffer[0]);
        memmove(tv->line_buffer, tv->line_buffer + 1, (tv->buffer_capacity - 1)  *sizeof(char*));
        tv->buffer_size--;
    }
    
    tv->line_buffer[tv->buffer_size] = strdup(line);
    if (!tv->line_buffer[tv->buffer_size]) {
        return;
    }
    
    tv->buffer_size++;
    tv->current_line++;
    
    if (was_at_bottom && tv->buffer_size > tv->max_lines) {
        tv->scroll_pos = tv->buffer_size - tv->max_lines;
    }
}

static void redraw_thread_window(thread_view_t *tv) {
    if (tv == NULL || tv->win == NULL || tv->line_buffer == NULL) {
        return;
    }
    
    werase(tv->win);
    int max_y, max_x;
    getmaxyx(tv->win, max_y, max_x);
    
    bool is_active = (g_ui->threads[g_ui->active_thread] == tv);
    if (!is_active) {
        wbkgd(tv->win, 0);
    }
    
    wattron(tv->win, tv->color_pair | (is_active ? A_BOLD : 0));
    box(tv->win, ACS_VLINE, ACS_HLINE);
    
    int title_len = snprintf(NULL, 0, " Thread %llu ", tv->thread_id);
    int title_pos = (max_x - title_len) / 2;
    mvwprintw(tv->win, 0, title_pos, " Thread %llu ", tv->thread_id);
    
    apply_active_highlight(tv->win, tv->color_pair, is_active);
    if (is_active) {
        wattron(tv->win, COLOR_PAIR(tv->color_pair) | A_BOLD);
    }
    
    normalize_visible_indents(tv);
    for (int i = 0; i < tv->max_lines && (i + tv->scroll_pos) < tv->buffer_size; i++) {
        if (tv->line_buffer[i + tv->scroll_pos]) {
            const char *line = tv->line_buffer[i + tv->scroll_pos];
            
            int orig_depth = calculate_indent_depth(line);
            int normalized_depth = orig_depth - tv->min_visible_depth;

            wmove(tv->win, i + 1, 1);
            

            for (int j = 0; j < normalized_depth; j++) {
                waddch(tv->win, '|');
            }
            
            while (*line == ' ' || *line == '|') {
                line++;
            }
            wprintw(tv->win, "%s", line);
        }
    }
    
    if (is_active) {
        wattroff(tv->win, COLOR_PAIR(tv->color_pair) | A_BOLD);
    }
    else {
        wattroff(tv->win, COLOR_PAIR(tv->color_pair | 0));
    }
    
    wnoutrefresh(tv->win);
}

static void redraw_all_windows(void) {
    for (size_t i = 0; i < g_ui->thread_count; i++) {
        if (g_ui->threads[i]) {
            redraw_thread_window(g_ui->threads[i]);
        }
    }
    doupdate();
}

static void process_trace(const char *json_str) {
    json_object *trace = json_tokener_parse(json_str);
    if (trace == NULL) {
        return;
    }
    
    json_object *obj;
    tracer_event_t event = {0};
    event.thread_id = 0;
    if (json_object_object_get_ex(trace, "thread_id", &obj)) {
        event.thread_id = json_object_get_int64(obj);
    }
    
    if (event.thread_id == 0) {
        json_object_put(trace);
        return;
    }
    
    bool did_create_tv = false;
    thread_view_t *tv = get_or_create_thread_view(event.thread_id, &did_create_tv);
    if (tv == NULL) {
        json_object_put(trace);
        return;
    }
    
    if (json_object_object_get_ex(trace, "formatted_output", &obj)) {
        const char *formatted = json_object_get_string(obj);
        if (formatted) {
            record_line_for_thread(tv, formatted);
            update_counter++;

            if (did_create_tv || tv->current_line <= 25) {
                redraw_thread_window(tv);
                cleanup_inactive_threads();
            }
            else if (update_counter >= UPDATE_THRESHOLD_EVENT_COUNT) {
                update_counter = 0;
                redraw_all_windows();
                cleanup_inactive_threads();
            }
        }
    }
    json_object_put(trace);
}

static void handle_signal(int sig) {
    if (g_ui) {
        g_ui->running = false;
    }
}

static void cleanup_ui(void) {
    if (g_ui == NULL) {
        return;
    }
    
    for (size_t i = 0; i < g_ui->thread_count; i++) {
        if (g_ui->threads[i]) {
            if (g_ui->threads[i]->win) {
                delwin(g_ui->threads[i]->win);
            }
            
            if (g_ui->threads[i]->line_buffer) {
                for (int j = 0; j < g_ui->threads[i]->buffer_size; j++) {
                    free(g_ui->threads[i]->line_buffer[j]);
                }
                free(g_ui->threads[i]->line_buffer);
            }
            free(g_ui->threads[i]);
        }
    }
    free(g_ui->threads);
    
    if (g_ui->header) {
        delwin(g_ui->header);
    }
    
    endwin();
    free(g_ui);
    g_ui = NULL;
}

int run_tui_trace_server(tracer_config_t *config) {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    intrflush(stdscr, FALSE);
    
    setup_colors();
    
    g_ui = calloc(1, sizeof(tracer_ui_t));
    if (g_ui == NULL) {
        endwin();
        return 1;
    }
    
    g_ui->threads = calloc(30, sizeof(thread_view_t *));
    if (g_ui->threads == NULL) {
        free(g_ui);
        endwin();
        return 1;
    }
    g_ui->thread_count = 0;
    g_ui->active_thread = 0;
    
    int max_x, _;
    getmaxyx(stdscr, _, max_x);
    g_ui->header = newwin(2, max_x, 0, 0);
    wattron(g_ui->header, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(g_ui->header, 1, 0, " Press 'q' to exit");
    wattroff(g_ui->header, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    
    g_ui->running = true;
    
    struct sigaction sa = {
        .sa_handler = handle_signal,
        .sa_flags = 0,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    int send_buffer_size = 1024 * 1024 * 4;
    int recv_buffer_size = 1024 * 1024 * 4;
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
        printf("Failed to set receive buffer size\n");
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd);
        return 1;
    }
    
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(config->transport_config.port)
    };
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Bind failed\n");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        return 1;
    }
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        close(server_fd);
        return 1;
    }
    
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
        printf("Failed to set send buffer size\n");
    }
    
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    bool connection_active = true;
    char buffer[8192];
    size_t buffer_pos = 0;
    
    wattron(g_ui->header, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(g_ui->header, 0, 0, " Connected to process - Press 'q' to quit ");
    wattroff(g_ui->header, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    wrefresh(g_ui->header);
    
    while (g_ui->running) {
        int ch = getch();
        if (ch != ERR) {
            thread_view_t *active_tv = g_ui->threads[g_ui->active_thread];
            bool need_redraw = false;
            
            switch (ch) {
                case 'q': {
                    g_ui->running = false;
                    break;
                }
                    
                case KEY_RIGHT: {
                    if (g_ui->active_thread < g_ui->thread_count - 1) {
                        g_ui->active_thread++;
                        need_redraw = true;
                    }
                    break;
                }
                    
                case KEY_LEFT: {
                    if (g_ui->active_thread > 0) {
                        g_ui->active_thread--;
                        need_redraw = true;
                    }
                    break;
                }
                    
                case KEY_DOWN: {
                    if (active_tv && active_tv->scroll_pos + active_tv->max_lines < active_tv->buffer_size) {
                        active_tv->scroll_pos += active_tv->max_lines / 2;
                        if (active_tv->scroll_pos > active_tv->buffer_size - active_tv->max_lines) {
                            active_tv->scroll_pos = active_tv->buffer_size - active_tv->max_lines;
                        }
                        need_redraw = true;
                    }
                    break;
                }
                    
                case KEY_UP: {
                    if (active_tv && active_tv->scroll_pos > 0) {
                        active_tv->scroll_pos -= active_tv->max_lines / 2;
                        if (active_tv->scroll_pos < 0) {
                            active_tv->scroll_pos = 0;
                        }
                        need_redraw = true;
                    }
                    break;
                }
            }
            
            if (need_redraw) {
                for (size_t i = 0; i < g_ui->thread_count; i++) {
                    redraw_thread_window(g_ui->threads[i]);
                }
            }
        }
        
        if (connection_active) {
            ssize_t bytes_read = recv(client_fd, buffer + buffer_pos, sizeof(buffer) - buffer_pos, 0);
            if (bytes_read > 0) {
                buffer[buffer_pos + bytes_read] = '\0';
                
                char *line_start = buffer;
                char *line_end;
                while ((line_end = strchr(line_start, '\n'))) {
                    *line_end = '\0';
                    process_trace(line_start);
                    line_start = line_end + 1;
                }
                
                buffer_pos = strlen(line_start);
                if (buffer_pos > 0) {
                    memmove(buffer, line_start, buffer_pos);
                }
            }
            else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                time_t now = time(NULL);
                char timestr[64];
                strftime(timestr, sizeof(timestr), "%H:%M:%S", localtime(&now));
                mvwprintw(g_ui->header, 0, max_x - 20, "[Detached: %s]", timestr);
                wrefresh(g_ui->header);
            }
        }
        usleep(1000);
    }
    
    if (g_ui->running == 1) {
        
        g_ui->running = false;
        close(client_fd);
        close(server_fd);
        
        endwin();
        printf("\nConnection closed. Output preserved.\n");
        printf("Press Enter to exit...\n");
        while (getchar() != '\n');
    }
    
    cleanup_ui();
    return 0;
}
