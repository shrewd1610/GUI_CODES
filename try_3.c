#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <ncurses.h>
#include <math.h>

#define SOCKET_PATH "/tmp/master_slave_socket"
#define COMMUNICATION_DURATION 60
#define MESSAGE_INTERVAL 2
#define NUM_CORES 2  // Assuming 2 cores for master/slave

typedef struct {
    WINDOW *win;
    int height;
    int width;
    int y;
    int x;
} Window;

typedef struct {
    unsigned long user;
    unsigned long nice;
    unsigned long system;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
} CPUStats;

Window main_win, master_win, slave_win, comm_win, status_win, cpu_win;

void init_window(Window *window, int h, int w, int y, int x, int border, const char *title);
void init_gui();
void update_status(const char *msg);
void show_transmission(const char *from, const char *to, const char *data, int duration);
void log_communication(const char *from, const char *to, const char *data, int is_sending);
void update_master(const char *msg);
void update_slave(const char *msg);
void pin_to_core(int core_id);
void run_master();
void run_slave();
void get_cpu_stats(CPUStats stats[NUM_CORES]);
float calculate_cpu_usage(CPUStats *prev, CPUStats *curr);
void update_cpu_usage();

void init_window(Window *window, int h, int w, int y, int x, int border, const char *title) {
    window->height = h;
    window->width = w;
    window->y = y;
    window->x = x;
    window->win = newwin(h, w, y, x);

    if (border) {
        wattron(window->win, COLOR_PAIR(5));
        box(window->win, 0, 0);
        wattroff(window->win, COLOR_PAIR(5));
        if (title) {
            int title_pos = (w - strlen(title) - 4) / 2;
            mvwprintw(window->win, 0, title_pos > 0 ? title_pos : 1, " %s ", title);
        }
    }

    wrefresh(window->win);
}

void init_gui() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    refresh();

    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLUE);
    init_pair(6, COLOR_BLACK, COLOR_GREEN);
    init_pair(7, COLOR_MAGENTA, COLOR_BLACK);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int main_height = max_y - 6;
    int main_width = max_x - 10;
    int main_y = 3;
    int main_x = 5;

    init_window(&main_win, main_height, main_width, main_y, main_x, 1, "Master-Slave Communication System");

    int sub_width = (main_width - 8) / 2;
    int master_y = main_y + 2;
    int master_x = main_x + 2;
    int slave_y = master_y;
    int slave_x = master_x + sub_width + 2;

    init_window(&master_win, main_height/2 - 4, sub_width, master_y, master_x, 1, "Master (Core 0)");
    init_window(&slave_win, main_height/2 - 4, sub_width, slave_y, slave_x, 1, "Slave (Core 1)");

    init_window(&cpu_win, 4, main_width - 4, master_y + main_height/2 - 3, main_x + 2, 1, "CPU Usage");
    int comm_y = master_y + main_height/2 + 1;
    init_window(&comm_win, main_height/2 - 3, main_width - 4, comm_y, main_x + 2, 1, "Communication Log");
    int status_y = main_y + main_height - 3;
    init_window(&status_win, 3, main_width - 4, status_y, main_x + 2, 1, "Status");

    wattron(master_win.win, COLOR_PAIR(1));
    mvwprintw(master_win.win, 1, 2, "Initializing...");
    mvwprintw(master_win.win, 2, 2, "Process started");
    wattroff(master_win.win, COLOR_PAIR(1));

    wattron(slave_win.win, COLOR_PAIR(2));
    mvwprintw(slave_win.win, 1, 2, "Initializing...");
    mvwprintw(slave_win.win, 2, 2, "Process started");
    wattroff(slave_win.win, COLOR_PAIR(2));

    wattron(cpu_win.win, COLOR_PAIR(7));
    mvwprintw(cpu_win.win, 1, 2, "Core 0: 0.0%%");
    mvwprintw(cpu_win.win, 2, 2, "Core 1: 0.0%%");
    wattroff(cpu_win.win, COLOR_PAIR(7));

    wattron(comm_win.win, COLOR_PAIR(3));
    mvwprintw(comm_win.win, 1, 2, "Waiting for communication...");
    wattroff(comm_win.win, COLOR_PAIR(3));

    wattron(status_win.win, COLOR_PAIR(4));
    mvwprintw(status_win.win, 1, 2, "System ready");
    wattroff(status_win.win, COLOR_PAIR(4));

    wrefresh(master_win.win);
    wrefresh(slave_win.win);
    wrefresh(cpu_win.win);
    wrefresh(comm_win.win);
    wrefresh(status_win.win);
}

void update_status(const char *msg) {
    werase(status_win.win);
    box(status_win.win, 0, 0);
    wattron(status_win.win, COLOR_PAIR(4));
    mvwprintw(status_win.win, 1, 2, "%s", msg);
    wattroff(status_win.win, COLOR_PAIR(4));
    wrefresh(status_win.win);
}

void show_transmission(const char *from, const char *to, const char *data, int duration) {
    werase(comm_win.win);
    box(comm_win.win, 0, 0);
    wattron(comm_win.win, COLOR_PAIR(3));
    mvwprintw(comm_win.win, 1, 2, "%s -> %s : %s", from, to, data);
    wattroff(comm_win.win, COLOR_PAIR(3));
    wrefresh(comm_win.win);
    napms(duration * 1000 / 2);  // half second delay for animation
}

void log_communication(const char *from, const char *to, const char *data, int is_sending) {
    static int line = 2;
    if (line >= comm_win.height - 2) {
        werase(comm_win.win);
        box(comm_win.win, 0, 0);
        line = 2;
    }
    wattron(comm_win.win, is_sending ? COLOR_PAIR(6) : COLOR_PAIR(3));
    mvwprintw(comm_win.win, line++, 2, "%s -> %s : %s", from, to, data);
    wattroff(comm_win.win, is_sending ? COLOR_PAIR(6) : COLOR_PAIR(3));
    wrefresh(comm_win.win);
}

void update_master(const char *msg) {
    mvwprintw(master_win.win, 4, 2, "%s", msg);
    wrefresh(master_win.win);
}

void update_slave(const char *msg) {
    mvwprintw(slave_win.win, 4, 2, "%s", msg);
    wrefresh(slave_win.win);
}

void pin_to_core(int core_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
    }
}

void get_cpu_stats(CPUStats stats[NUM_CORES]) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;
    char line[256];
    fgets(line, sizeof(line), fp); // skip aggregate
    for (int i = 0; i < NUM_CORES; i++) {
        if (fgets(line, sizeof(line), fp)) {
            sscanf(line, "cpu%d %lu %lu %lu %lu %lu %lu %lu",
                   &i, &stats[i].user, &stats[i].nice, &stats[i].system,
                   &stats[i].idle, &stats[i].iowait, &stats[i].irq, &stats[i].softirq);
        }
    }
    fclose(fp);
}

float calculate_cpu_usage(CPUStats *prev, CPUStats *curr) {
    unsigned long prev_total = prev->user + prev->nice + prev->system +
                               prev->idle + prev->iowait + prev->irq + prev->softirq;
    unsigned long curr_total = curr->user + curr->nice + curr->system +
                               curr->idle + curr->iowait + curr->irq + curr->softirq;
    unsigned long total_diff = curr_total - prev_total;
    if (total_diff == 0) return 0.0;
    unsigned long idle_diff = curr->idle - prev->idle;
    return 100.0f * (total_diff - idle_diff) / total_diff;
}

void update_cpu_usage() {
    static CPUStats prev_stats[NUM_CORES] = {0};
    CPUStats curr_stats[NUM_CORES];
    get_cpu_stats(curr_stats);
    wattron(cpu_win.win, COLOR_PAIR(7));
    for (int i = 0; i < NUM_CORES; i++) {
        float usage = calculate_cpu_usage(&prev_stats[i], &curr_stats[i]);
        mvwprintw(cpu_win.win, i + 1, 2, "Core %d: %5.1f%%", i, usage);
        prev_stats[i] = curr_stats[i];
    }
    wattroff(cpu_win.win, COLOR_PAIR(7));
    box(cpu_win.win, 0, 0);
    wrefresh(cpu_win.win);
}

void run_master() {
    pin_to_core(0);
    update_master("Process started");
    update_status("Master: Initializing socket...");

    int server_fd, new_socket;
    struct sockaddr_un address;
    char buffer[1024] = {0};
    char message_num = '0';

    unlink(SOCKET_PATH);
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); endwin(); exit(EXIT_FAILURE); }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); endwin(); exit(EXIT_FAILURE);
    }

    listen(server_fd, 1);
    update_master("Waiting for connection");
    update_status("Master: Ready for slave");

    new_socket = accept(server_fd, NULL, NULL);
    if (new_socket < 0) { perror("accept"); endwin(); exit(EXIT_FAILURE); }

    time_t start_time = time(NULL);
    while (time(NULL) - start_time < COMMUNICATION_DURATION) {
        update_cpu_usage();

        ssize_t bytes_received = read(new_socket, buffer, sizeof(buffer));
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            update_master("Data received");
            log_communication("Slave", "Master", buffer, 0);
            show_transmission("Slave", "Master", buffer, 1);
            update_status("Master: Processing data...");
        }

        char response[32];
        snprintf(response, sizeof(response), "ACK_%c", message_num);
        message_num = (message_num == '9') ? '0' : message_num + 1;
        update_status("Master: Sending response...");
        show_transmission("Master", "Slave", response, 1);
        send(new_socket, response, strlen(response), 0);
        update_master("Response sent");
        log_communication("Master", "Slave", response, 1);

        sleep(MESSAGE_INTERVAL);
    }

    close(new_socket);
    close(server_fd);
    unlink(SOCKET_PATH);
}

void run_slave() {
    sleep(1); // wait for master
    pin_to_core(1);
    update_slave("Process started");
    update_status("Slave: Connecting...");

    int sock;
    struct sockaddr_un serv_addr;
    char buffer[1024] = {0};
    char message_num = '0';

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); endwin(); exit(EXIT_FAILURE); }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, SOCKET_PATH, sizeof(serv_addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); endwin(); exit(EXIT_FAILURE);
    }

    time_t start_time = time(NULL);
    while (time(NULL) - start_time < COMMUNICATION_DURATION) {
        char message[32];
        snprintf(message, sizeof(message), "DATA_%c", message_num);
        message_num = (message_num == '9') ? '0' : message_num + 1;

        update_status("Slave: Sending data...");
        show_transmission("Slave", "Master", message, 1);
        send(sock, message, strlen(message), 0);
        update_slave("Data sent");
        log_communication("Slave", "Master", message, 1);

        update_status("Slave: Waiting for response...");
        ssize_t bytes_received = read(sock, buffer, sizeof(buffer));
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            update_slave("Response received");
            log_communication("Master", "Slave", buffer, 0);
            show_transmission("Master", "Slave", buffer, 1);
        }

        sleep(MESSAGE_INTERVAL);
    }

    close(sock);
}

int main() {
    init_gui();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork"); endwin(); return 1;
    }

    if (pid == 0) run_master();
    else {
        run_slave();
        wait(NULL);
    }

    update_status("Communication complete! Press any key...");
    getch();
    endwin();
    return 0;
}
