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
#include <pthread.h>

#define SOCKET_PATH "/tmp/master_slave_socket"
#define COMMUNICATION_DURATION 20
#define MESSAGE_INTERVAL 2
#define CPU_UPDATE_INTERVAL 1

typedef struct {
    WINDOW *win;
    int height;
    int width;
    int y;
    int x;
} Window;

Window main_win, master_win, slave_win, comm_win, status_win, cpu_win;

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

    init_window(&master_win, main_height / 3 - 1, sub_width, master_y, master_x, 1, "Master (Core 0)");
    init_window(&slave_win, main_height / 3 - 1, sub_width, slave_y, slave_x, 1, "Slave (Core 1)");

    int comm_y = master_y + main_height / 3;
    int comm_x = master_x;
    init_window(&comm_win, main_height / 3 - 1, main_width - 4, comm_y, comm_x, 1, "Communication Log");

    int cpu_y = comm_y + main_height / 3 - 1;
    init_window(&cpu_win, 6, main_width - 4, cpu_y, comm_x, 1, "Live CPU Usage");

    int status_y = cpu_y + 6;
    init_window(&status_win, 3, main_width - 4, status_y, comm_x, 1, "Status");

    mvwprintw(status_win.win, 1, 2, "System initialized...");
    wrefresh(status_win.win);
}

void update_status(const char *msg) {
    werase(status_win.win);
    wattron(status_win.win, COLOR_PAIR(4));
    box(status_win.win, 0, 0);
    mvwprintw(status_win.win, 0, 2, " Status ");
    mvwprintw(status_win.win, 1, 2, "%s", msg);
    wattroff(status_win.win, COLOR_PAIR(4));
    wrefresh(status_win.win);
}

void update_comm_log(const char *msg) {
    static int line = 1;
    if (line >= comm_win.height - 2) {
        werase(comm_win.win);
        box(comm_win.win, 0, 0);
        mvwprintw(comm_win.win, 0, 2, " Communication Log ");
        line = 1;
    }
    mvwprintw(comm_win.win, line++, 2, "%s", msg);
    wrefresh(comm_win.win);
}

void *cpu_monitor_thread(void *arg) {
    FILE *fp;
    char line[256];

    while (1) {
        fp = popen("mpstat -P ALL 1 1 | grep -E '^ *[0-9]+'", "r");
        if (fp == NULL) {
            perror("mpstat failed");
            break;
        }

        werase(cpu_win.win);
        box(cpu_win.win, 0, 0);
        mvwprintw(cpu_win.win, 0, 2, " Live CPU Usage ");

        int row = 1;
        while (fgets(line, sizeof(line), fp)) {
            int cpu;
            float usr, sys, idle;
            if (sscanf(line, " %d %*f %*f %f %f %*f %*f %*f %*f %*f %f",
                       &cpu, &usr, &sys, &idle) == 4) {
                mvwprintw(cpu_win.win, row++, 2, "CPU %d: usr=%.1f%% sys=%.1f%% idle=%.1f%%",
                          cpu, usr, sys, idle);
            }
        }
        pclose(fp);
        wrefresh(cpu_win.win);
        sleep(CPU_UPDATE_INTERVAL);
    }

    return NULL;
}

void run_master() {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);

    int sockfd, connfd;
    struct sockaddr_un addr;
    char buf[128];

    unlink(SOCKET_PATH);
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(sockfd, 1);
    update_status("Master waiting for connection...");
    connfd = accept(sockfd, NULL, NULL);
    update_status("Slave connected!");

    for (int i = 0; i < COMMUNICATION_DURATION / MESSAGE_INTERVAL; ++i) {
        snprintf(buf, sizeof(buf), "Message %d from Master", i + 1);
        write(connfd, buf, strlen(buf) + 1);
        update_comm_log(buf);
        sleep(MESSAGE_INTERVAL);
    }

    close(connfd);
    close(sockfd);
}

void run_slave() {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(1, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);

    sleep(1);  // wait for master
    int sockfd;
    struct sockaddr_un addr;
    char buf[128];

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    while (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        sleep(1);
    }

    update_status("Slave connected to master");
    while (read(sockfd, buf, sizeof(buf)) > 0) {
        update_comm_log(buf);
    }
    close(sockfd);
}

int main() {
    init_gui();

    pthread_t cpu_thread;
    pthread_create(&cpu_thread, NULL, cpu_monitor_thread, NULL);

    pid_t pid = fork();
    if (pid == 0) {
        run_master();
    } else {
        run_slave();
        wait(NULL);
    }

    update_status("Communication complete! Press any key to exit...");
    wrefresh(status_win.win);
    getch();

    endwin();
    return 0;
}
