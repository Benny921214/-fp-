#include "unp.h"
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define MAX_DICE 5
#define SOCKET_PATH "/tmp/socket_game"

const int enable = 1;

typedef struct {
    int sockfd;
    char name[32];
    int dice[MAX_DICE];
    int active;
} Player;

typedef struct {
    int type;
    char data[MAXLINE];
} Message;

// 全域變數（簡易做法）
Player players[MAX_PLAYERS];
int dice_point[7] = { 0 }; //dice_point[0]紀錄1是否被喊了
int player_count = 0;
int current_player = 0;
int last_player = 0;
int last_point = 0;
int last_quantity = 0;
int game_over = 0;
bool isUdsCreated = false;

void sig_chld(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        if (WIFEXITED(stat)) {
            printf("子進程 %d 正常結束，退出狀態: %d\n", pid, WEXITSTATUS(stat));
        }
        else if (WIFSIGNALED(stat)) {
            printf("子進程 %d 因信號 %d 結束\n", pid, WTERMSIG(stat));
        }
    }
    return;
}

// 與遊戲流程相關的函式
void init_game(int* clifd, int num);
void init_player(int sockfd, int n);
void send_welcome_message(Player* p);
void send_dice_info(Player* p);
void broadcast(Message* msg);
void handle_client_message(int sockfd, fd_set* mset, int rfd);
void handle_open_cup(int sockfd, int rfd);
void player_disconnect(int sockfd, fd_set* mset, int rfd);
void next_player();
void send_to_player(Player* p, Message* msg);
void notify_lobby(int type, int sock, int rfd);

int main(int argc, char** argv) {
    int listenfd, connfd, maxfd;
    int player_num = 0;
    int server_fd = 0;
    // 大廳：最多容納 50 個連線
    int cli[50] = { 0 };
    // 各房間
    int p2room[2] = { 0 }, inp2room = 0;
    int p3room[3] = { 0 }, inp3room = 0;
    int p4room[4] = { 0 }, inp4room = 0;
    int roomid[30] = { 0 };

    ssize_t n;
    fd_set rset, master_set, processed_set, room_set;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    pid_t childpid;
    Message recvmsg, sendmsg;

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(12345);

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    Bind(listenfd, (SA*)&servaddr, sizeof(servaddr));
    Listen(listenfd, MAX_PLAYERS);

    printf("server start, wait for client connect...\n");

    FD_ZERO(&master_set);
    FD_ZERO(&room_set);
    FD_SET(listenfd, &master_set);

    Signal(SIGCHLD, sig_chld);

    while (1) {
        FD_ZERO(&processed_set);
        rset = master_set;
        maxfd = max(listenfd, server_fd);

        // 找出目前所有 socket 中的最大值
        for (int i = 0; i < 50; ++i) {
            if (cli[i]) maxfd = max(maxfd, cli[i]);
        }
        for (int i = 0; i < 30; ++i) {
            if (roomid[i]) maxfd = max(maxfd, roomid[i]);
        }
        for (int i = 0; i < 2; ++i) {
            if (p2room[i]) maxfd = max(maxfd, p2room[i]);
        }
        for (int i = 0; i < 3; ++i) {
            if (p3room[i]) maxfd = max(maxfd, p3room[i]);
        }
        for (int i = 0; i < 4; ++i) {
            if (p4room[i]) maxfd = max(maxfd, p4room[i]);
        }

        int err = Select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (err < 0) {
            perror("select failed");
            continue;
        }

        //玩家結束遊戲，回到大廳
        if (FD_ISSET(server_fd, &rset)) {
            int uds_fd = accept(server_fd, NULL, NULL);
            for (int i = 0; i < 30; ++i) {
                if (!roomid[i]) {
                    roomid[i] = uds_fd;
                    FD_SET(uds_fd, &master_set);
                    break;
                }
            }
        }

        for (int i = 0; i < 30; ++i) {
            if (!roomid[i]) continue;
            if (FD_ISSET(roomid[i], &rset) && player_num < 50) {
                n = Read(roomid[i], &recvmsg, sizeof(Message));
                if (n > 0) {
                    int tmp_fd;
                    sscanf(recvmsg.data, "%d", &tmp_fd);
                    if (recvmsg.type == 10 && FD_ISSET(tmp_fd, &room_set)) {
                        FD_CLR(tmp_fd, &room_set);
                        Close(tmp_fd);
                    }
                    else if (recvmsg.type == 11 && FD_ISSET(tmp_fd, &room_set)) {
                        FD_CLR(tmp_fd, &room_set);
                        FD_SET(tmp_fd, &master_set);
                        for (int i = 0; i < 50; ++i) {
                            if (!cli[i]) {
                                cli[i] = tmp_fd;
                                player_num++;
                                break;
                            }
                        }
                        // 告訴剛連線的玩家：請選擇遊戲模式
                        sendmsg.type = 9;
                        snprintf(sendmsg.data, sizeof(sendmsg.data),
                            "請選擇遊戲模式(1 : 2人房, 2 : 3人房, 3 : 4人房)");
                        Writen(tmp_fd, &sendmsg, sizeof(Message));
                    }
                }
                else {
                    FD_CLR(roomid[i], &master_set);
                    close(roomid[i]);
                    roomid[i] = 0;
                }
            }
        }

        // 檢查新連線
        if (FD_ISSET(listenfd, &rset) && player_num < 50) {
            clilen = sizeof(cliaddr);
            connfd = Accept(listenfd, (SA*)&cliaddr, &clilen); // 接受新連線
            for (int i = 0; i < 50; ++i) {
                if (!cli[i]) {
                    FD_SET(connfd, &master_set);
                    cli[i] = connfd;
                    player_num++;
                    break;
                }
            }
            // 告訴剛連線的玩家：請選擇遊戲模式
            sendmsg.type = 9;
            snprintf(sendmsg.data, sizeof(sendmsg.data),
                "請選擇遊戲模式(1 : 2人房, 2 : 3人房, 3 : 4人房)");
            Writen(connfd, &sendmsg, sizeof(Message));
        }

        // 依次檢查各房間
        // ---- 2人房 ----
        for (int i = 0; i < 2; ++i) {
            if (!p2room[i]) continue; // 空位
            if (FD_ISSET(p2room[i], &rset)) {
                FD_SET(p2room[i], &processed_set);
                n = Read(p2room[i], &recvmsg, sizeof(Message));
                // 若讀到 0 => 該用戶斷開
                if (n <= 0) {
                    printf("Debug: Detected disconnection for sockfd=%d.\n", p2room[i]);
                    // 該用戶斷線 => Close(..)，然後不會加回大廳
                    Close(p2room[i]);
                    FD_CLR(p2room[i], &master_set);
                    p2room[i] = 0;
                    inp2room--;

                    // 廣播房內剩餘玩家目前人數
                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/2, 按Ctrl+D可離開房間", inp2room);
                    for (int k = 0; k < 2; ++k) {
                        if (!p2room[k]) continue;
                        Writen(p2room[k], &sendmsg, sizeof(Message));
                    }
                }
                else if (recvmsg.type == 9) {
                    printf("Debug: Received type=9 from sockfd=%d.\n", p2room[i]);
                    // 按 Ctrl + D => 回大廳
                    // 1. 加回大廳
                    for (int j = 0; j < 50; ++j) {
                        if (!cli[j]) {
                            cli[j] = p2room[i];
                            player_num++;
                            // 2. 給此玩家新的大廳提示
                            sendmsg.type = 9;
                            snprintf(sendmsg.data, sizeof(sendmsg.data),
                                "你已回到大廳，請選擇遊戲模式(1 : 2人房, 2 : 3人房, 3 : 4人房)");
                            Writen(p2room[i], &sendmsg, sizeof(Message));
                            printf("Debug: Player sockfd=%d returned to lobby.\n", p2room[i]);
                            break;
                        }
                    }
                    // 3. 把房間位子清空
                    p2room[i] = 0;
                    inp2room--;

                    // 4. 廣播房內剩餘玩家目前人數
                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/2, 按Ctrl+D可離開房間", inp2room);
                    for (int k = 0; k < 2; ++k) {
                        if (!p2room[k]) continue;
                        Writen(p2room[k], &sendmsg, sizeof(Message));
                    }
                }
            }
        }


        // ---- 3人房 ----
        for (int i = 0; i < 3; ++i) {
            if (!p3room[i]) continue;
            if (FD_ISSET(p3room[i], &rset)) {
                FD_SET(p3room[i], &processed_set);
                n = Read(p3room[i], &recvmsg, sizeof(Message));
                if (n <= 0) {
                    Close(p3room[i]);
                    FD_CLR(p3room[i], &master_set);
                    p3room[i] = 0;
                    inp3room--;

                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/3, 按Ctrl+D可離開房間", inp3room);
                    for (int k = 0; k < 3; ++k) {
                        if (!p3room[k]) continue;
                        Writen(p3room[k], &sendmsg, sizeof(Message));
                    }
                }
                else if (recvmsg.type == 9) {
                    // 回大廳
                    for (int j = 0; j < 50; ++j) {
                        if (!cli[j]) {
                            cli[j] = p3room[i];
                            player_num++;
                            sendmsg.type = 9;
                            snprintf(sendmsg.data, sizeof(sendmsg.data),
                                "你已回到大廳，請選擇遊戲模式(1 : 2人房, 2 : 3人房, 3 : 4人房)");
                            Writen(p3room[i], &sendmsg, sizeof(Message));
                            break;
                        }
                    }
                    p3room[i] = 0;
                    inp3room--;

                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/3, 按Ctrl+D可離開房間", inp3room);
                    for (int k = 0; k < 3; ++k) {
                        if (!p3room[k]) continue;
                        Writen(p3room[k], &sendmsg, sizeof(Message));
                    }
                }
            }
        }

        // ---- 4人房 ----
        for (int i = 0; i < 4; ++i) {
            if (!p4room[i]) continue;
            if (FD_ISSET(p4room[i], &rset)) {
                FD_SET(p4room[i], &processed_set);
                n = Read(p4room[i], &recvmsg, sizeof(Message));
                if (n <= 0) {
                    Close(p4room[i]);
                    FD_CLR(p4room[i], &master_set);
                    p4room[i] = 0;
                    inp4room--;

                    // 廣播房內剩餘人數
                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/4, 按Ctrl+D可離開房間", inp4room);
                    for (int k = 0; k < 4; ++k) {
                        if (!p4room[k]) continue;
                        Writen(p4room[k], &sendmsg, sizeof(Message));
                    }
                }
                else if (recvmsg.type == 9) {
                    // 回大廳
                    for (int j = 0; j < 50; ++j) {
                        if (!cli[j]) {
                            cli[j] = p4room[i];
                            player_num++;
                            sendmsg.type = 9;
                            snprintf(sendmsg.data, sizeof(sendmsg.data),
                                "你已回到大廳，請選擇遊戲模式(1 : 2人房, 2 : 3人房, 3 : 4人房)");
                            Writen(p4room[i], &sendmsg, sizeof(Message));
                            break;
                        }
                    }
                    p4room[i] = 0;
                    inp4room--;

                    sendmsg.type = 1;
                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                        "目前房間人數%d/4, 按Ctrl+D可離開房間", inp4room);
                    for (int k = 0; k < 4; ++k) {
                        if (!p4room[k]) continue;
                        Writen(p4room[k], &sendmsg, sizeof(Message));
                    }
                }
            }
        }

        // ---- 大廳 ----
        for (int i = 0; i < 50; ++i) {
            if (!cli[i]) continue;
            if (FD_ISSET(cli[i], &rset) && !FD_ISSET(cli[i], &processed_set)) {
                n = Read(cli[i], &recvmsg, sizeof(Message));
                if (n == 0) {
                    // 真的斷線 => Close
                    Close(cli[i]);
                    FD_CLR(cli[i], &master_set);
                    cli[i] = 0;
                    player_num--;
                }
                else if (recvmsg.type == 8) {
                    // 表示要進入某房間
                    int choice;
                    sscanf(recvmsg.data, "%d", &choice); // 2 => 2人房, 3 =>3人房, 4 =>4人房

                    if (choice == 2) {
                        // 尋找2人房空位
                        for (int j = 0; j < 2; j++) {
                            if (!p2room[j]) {
                                p2room[j] = cli[i];
                                inp2room++;
                                cli[i] = 0;
                                player_num--;
                                // 如果2人房滿了，就 fork() 出子行程開始遊戲
                                if (inp2room == 2) {
                                    if ((childpid = Fork()) == 0) {
                                        // 子行程處理遊戲
                                        init_game(p2room, 2);
                                    }
                                    else {
                                        if (!isUdsCreated) {
                                            isUdsCreated = true;
                                            //create unix domain socket
                                            unlink(SOCKET_PATH);
                                            server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                                            if (server_fd < 0) {
                                                perror("socket failed");
                                                exit(EXIT_FAILURE);
                                            }
                                            struct sockaddr_un addr = { 0 };
                                            addr.sun_family = AF_UNIX;
                                            strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

                                            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                                                perror("bind failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }

                                            if (listen(server_fd, 5) < 0) {
                                                perror("listen failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }
                                            FD_SET(server_fd, &master_set);

                                        }

                                        // 父行程關閉 p2room 裡的 socket
                                        for (int k = 0; k < 2; k++) {
                                            FD_SET(p2room[k], &room_set);
                                            FD_CLR(p2room[k], &master_set);
                                        }
                                        inp2room = 0;
                                        bzero(&p2room, sizeof(p2room));
                                    }
                                }
                                else {
                                    // 未滿 => 通知房內現況
                                    sendmsg.type = 8;
                                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                                        "目前房間人數%d/2, 按Ctrl+D可離開房間", inp2room);
                                    Writen(p2room[j], &sendmsg, sizeof(sendmsg));
                                }
                                break;
                            }
                        }
                    }
                    else if (choice == 3) {
                        for (int j = 0; j < 3; j++) {
                            if (!p3room[j]) {
                                p3room[j] = cli[i];
                                inp3room++;
                                cli[i] = 0;
                                player_num--;
                                if (inp3room == 3) {
                                    if ((childpid = Fork()) == 0) {
                                        init_game(p3room, 3);
                                    }
                                    else {
                                        if (!isUdsCreated) {
                                            isUdsCreated = true;
                                            //create unix domain socket
                                            unlink(SOCKET_PATH);
                                            server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                                            if (server_fd < 0) {
                                                perror("socket failed");
                                                exit(EXIT_FAILURE);
                                            }
                                            struct sockaddr_un addr = { 0 };
                                            addr.sun_family = AF_UNIX;
                                            strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

                                            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                                                perror("bind failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }

                                            if (listen(server_fd, 5) < 0) {
                                                perror("listen failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }
                                            FD_SET(server_fd, &master_set);

                                        }

                                        for (int k = 0; k < 3; k++) {
                                            FD_SET(p3room[k], &room_set);
                                            FD_CLR(p3room[k], &master_set);
                                        }
                                        inp3room = 0;
                                        bzero(&p3room, sizeof(p3room));
                                    }
                                }
                                else {
                                    sendmsg.type = 8;
                                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                                        "目前房間人數%d/3, 按Ctrl+D可離開房間", inp3room);
                                    for (int k = 0; k < 3; ++k) {
                                        if (!p3room[k]) continue;
                                        Writen(p3room[k], &sendmsg, sizeof(sendmsg));
                                    }
                                }
                                break;
                            }
                        }
                    }
                    else if (choice == 4) {
                        for (int j = 0; j < 4; j++) {
                            if (!p4room[j]) {
                                p4room[j] = cli[i];
                                inp4room++;
                                cli[i] = 0;
                                player_num--;
                                if (inp4room == 4) {
                                    if ((childpid = Fork()) == 0) {
                                        init_game(p4room, 4);
                                    }
                                    else {
                                        if (!isUdsCreated) {
                                            isUdsCreated = true;
                                            //create unix domain socket
                                            unlink(SOCKET_PATH);
                                            server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                                            if (server_fd < 0) {
                                                perror("socket failed");
                                                exit(EXIT_FAILURE);
                                            }
                                            struct sockaddr_un addr = { 0 };
                                            addr.sun_family = AF_UNIX;
                                            strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

                                            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                                                perror("bind failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }

                                            if (listen(server_fd, 5) < 0) {
                                                perror("listen failed");
                                                close(server_fd);
                                                exit(EXIT_FAILURE);
                                            }
                                            FD_SET(server_fd, &master_set);

                                        }

                                        for (int k = 0; k < 4; k++) {
                                            FD_SET(p4room[k], &room_set);
                                            FD_CLR(p4room[k], &master_set);
                                        }
                                        inp4room = 0;
                                        bzero(&p4room, sizeof(p4room));
                                    }
                                }
                                else {
                                    sendmsg.type = 8;
                                    snprintf(sendmsg.data, sizeof(sendmsg.data),
                                        "目前房間人數%d/4, 按Ctrl+D可離開房間", inp4room);
                                    for (int k = 0; k < 4; ++k) {
                                        if (!p4room[k]) continue;
                                        Writen(p4room[k], &sendmsg, sizeof(sendmsg));
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

// ---------------- 房間子process使用的遊戲流程 ----------------

void init_game(int* clifd, int num) {
    int maxfd;
    ssize_t n;
    fd_set rset, master_set;
    Message msg;

    int room_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (room_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(room_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(room_fd);
        exit(EXIT_FAILURE);
    }

    // 初始化隨機數，讓每次骰子結果不同
    srand(time(NULL));

    // 通知房內所有玩家「遊戲開始！」
    msg.type = 8;
    snprintf(msg.data, sizeof(msg.data), "遊戲開始！你已進入 %d 人房", num);
    for (int i = 0; i < num; ++i) {
        Writen(clifd[i], &msg, sizeof(msg));
    }

    // 初始化 players、dice_point
    player_count = num;
    bzero(players, sizeof(players));
    bzero(dice_point, sizeof(dice_point));
    game_over = 0;
    current_player = 0;
    last_player = 0;
    last_point = 0;
    last_quantity = 0;

    FD_ZERO(&master_set);
    for (int i = 0; i < num; ++i) {
        init_player(clifd[i], i);
        FD_SET(clifd[i], &master_set);
    }
    //1可視為任一點
    for (int i = 2; i < 7; ++i) {
        dice_point[i] += dice_point[1];
    }

    // 通知第一位玩家行動
    Player* p = &players[current_player];
    if (p->active) {
        msg.type = 3;
        send_to_player(p, &msg);
    }

    while (!game_over) {
        rset = master_set;
        maxfd = 0;
        for (int i = 0; i < num; ++i) {
            if (players[i].active && players[i].sockfd > maxfd)
                maxfd = players[i].sockfd;
        }
        Select(maxfd + 1, &rset, NULL, NULL, NULL);

        for (int i = 0; i < num; ++i) {
            if (FD_ISSET(players[i].sockfd, &rset)) {
                // 如果是當前玩家 => 處理喊點或開盅
                if (i == current_player) {
                    handle_client_message(players[current_player].sockfd, &master_set, room_fd);
                }
                else {
                    // 不是當前玩家輸入 => 可能是斷線 / 中離
                    n = Read(players[i].sockfd, &msg, sizeof(Message));
                    if (n == 0) {
                        // 斷線 => 退出
                        player_disconnect(players[i].sockfd, &master_set, room_fd);
                    }
                    else if (msg.type == 7) {
                        // 中離
                        player_disconnect(players[i].sockfd, &master_set, room_fd);
                    }
                    else {
                        ;// 其它訊息都忽略
                    }
                }
            }
        }
    }
    exit(0);
}

void init_player(int sockfd, int n) {
    Player* p = &players[n];
    p->sockfd = sockfd;
    sprintf(p->name, "Player%d", n + 1);
    p->active = 1;

    // 擲骰子
    for (int i = 0; i < MAX_DICE; i++) {
        p->dice[i] = rand() % 6 + 1;
        dice_point[p->dice[i]] += 1;
    }

    // 歡迎訊息 + 傳送自己的骰子
    send_welcome_message(p);
    send_dice_info(p);
    printf("%s 已連接\n", p->name);
}

void send_welcome_message(Player* p) {
    Message msg;
    msg.type = 1;
    snprintf(msg.data, sizeof(msg.data),
        "welcome to game，%s！", p->name);
    send_to_player(p, &msg);
}

void send_dice_info(Player* p) {
    Message msg;
    msg.type = 2;
    snprintf(msg.data, sizeof(msg.data), "%d %d %d %d %d",
        p->dice[0], p->dice[1], p->dice[2], p->dice[3], p->dice[4]);
    send_to_player(p, &msg);
}

void broadcast(Message* msg) {
    for (int i = 0; i < player_count; i++) {
        if (players[i].active) {
            send_to_player(&players[i], msg);
        }
    }
}

void send_to_player(Player* p, Message* msg) {
    Writen(p->sockfd, msg, sizeof(Message));
}

void handle_client_message(int sockfd, fd_set* mset, int rfd) {
    int n;
    Message msg;
    n = Read(sockfd, &msg, sizeof(Message));
    if (n == 0) {
        // 斷線
        player_disconnect(sockfd, mset, rfd);
        return;
    }

    Player* p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (!p) return;

    if (msg.type == 4) {
        // 喊點
        int point, quantity;
        sscanf(msg.data, "%d %d", &point, &quantity);
        // 檢查是否比上一個喊點大
        if (quantity > last_quantity ||
            (quantity == last_quantity && point > last_point)) {

            if (point == 1 && !dice_point[0]) {
                //如果1被喊到,則1只能當作1
                dice_point[0] = 1;
                for (int i = 2; i < 7; ++i) {
                    dice_point[i] -= dice_point[1];
                }
            }
            last_point = point;
            last_quantity = quantity;
            last_player = current_player;

            Message broadcast_msg;
            broadcast_msg.type = 4;
            snprintf(broadcast_msg.data, sizeof(broadcast_msg.data),
                "%s 喊了%d個%d", p->name, quantity, point);
            broadcast(&broadcast_msg);

            next_player();
            // notify next player
            Player* next_p = &players[current_player];
            if (next_p->active) {
                Message msg2;
                msg2.type = 3;
                send_to_player(next_p, &msg2);
                printf("輪到 %s 行動\n", next_p->name);
            }
        }
        else {
            // 無效喊點 => 要求該玩家重試 (type=3)
            Message error_msg;
            error_msg.type = 3;
            send_to_player(p, &error_msg);
        }
    }
    else if (msg.type == 5) {
        // 開盅
        if (last_point > 0) {
            handle_open_cup(sockfd, rfd);
        }
        else {
            // 無效開盅 => 要求玩家重試
            Message error_msg;
            error_msg.type = 3;
            send_to_player(p, &error_msg);
        }
    }
    else if (msg.type == 7) {
        // 中離
        player_disconnect(sockfd, mset, rfd);
    }
    // 其它不處理
}

void handle_open_cup(int sockfd, int rfd) {
    Player* p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (!p) return;

    Message broadcast_msg;
    broadcast_msg.type = 5;
    snprintf(broadcast_msg.data, sizeof(broadcast_msg.data),
        "%s 選擇開盅", p->name);
    broadcast(&broadcast_msg);

    // 計算實際數量
    int actual_quantity = dice_point[last_point];

    // 勝負判斷
    Message result_msg;
    result_msg.type = 6;
    if (actual_quantity >= last_quantity) {
        snprintf(result_msg.data, sizeof(result_msg.data),
            "實際數量為 %d，%s 赢了！",
            actual_quantity, players[last_player].name);
    }
    else {
        snprintf(result_msg.data, sizeof(result_msg.data),
            "實際數量為 %d，%s 赢了！",
            actual_quantity, p->name);
    }
    broadcast(&result_msg);

    // 遊戲結束 => 關閉所有連線
    Message msg;
    int n;
    fd_set rset;
    FD_ZERO(&rset);
    int maxfd = 0;
    while (1) {
        int remain = 0;
        FD_ZERO(&rset);
        for (int i = 0; i < player_count; i++) {
            if (players[i].active) {
                FD_SET(players[i].sockfd, &rset);
                maxfd = max(maxfd, players[i].sockfd);
                remain++;
            }
        }
        if (remain) {
            Select(maxfd + 1, &rset, NULL, NULL, NULL);
            for (int i = 0; i < player_count; i++) {
                if (players[i].active && FD_ISSET(players[i].sockfd, &rset)) {
                    n = Read(players[i].sockfd, &msg, sizeof(Message));
                    if (n == 0) {
                        notify_lobby(10, players[i].sockfd, rfd);
                        players[i].active = 0;
                        Close(players[i].sockfd);
                    }
                    else if (msg.type == 10) {
                        notify_lobby(10, players[i].sockfd, rfd);
                        players[i].active = 0;
                        Close(players[i].sockfd);
                    }
                    else if (msg.type == 11) {
                        notify_lobby(11, players[i].sockfd, rfd);
                        players[i].active = 0;
                    }
                }
            }
        }
        else break;
    }
    game_over = 1;
}

void player_disconnect(int sockfd, fd_set* mset, int rfd) {
    Player* p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (!p) return;

    FD_CLR(sockfd, mset);
    printf("%s 中離\n", p->name);
    p->active = 0;
    Close(sockfd);

    // 廣播通知其他玩家
    Message msg;
    msg.type = 7;
    snprintf(msg.data, sizeof(msg.data),
        "%s 已中離, 他的點數是: %d %d %d %d %d",
        p->name,
        p->dice[0], p->dice[1], p->dice[2], p->dice[3], p->dice[4]);
    broadcast(&msg);

    // 若只剩一位玩家 => 遊戲結束
    int active_players = 0;
    Player* last_player = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].active) {
            active_players++;
            last_player = &players[i];
        }
    }
    if (active_players == 1) {
        Message result_msg;
        result_msg.type = 6;
        snprintf(result_msg.data, sizeof(result_msg.data),
            "只剩 %s 在線，遊戲結束！ %s 獲勝!", last_player->name, last_player->name);
        broadcast(&result_msg);

        int n;
        n = Read(last_player->sockfd, &msg, sizeof(Message));
        if (n == 0) {
            notify_lobby(10, last_player->sockfd, rfd);
            Close(last_player->sockfd);
        }
        else if (msg.type == 10) {
            notify_lobby(10, last_player->sockfd, rfd);
            Close(last_player->sockfd);
        }
        else if (msg.type == 11) {
            notify_lobby(11, last_player->sockfd, rfd);
        }
        game_over = 1;
    }

    // 如果當前玩家中離，換下位玩家行動
    if ((p - players) == current_player) {
        next_player();
        if (!game_over) {
            Player* next_p = &players[current_player];
            if (next_p->active) {
                Message msg2;
                msg2.type = 3;
                send_to_player(next_p, &msg2);
                printf("輪到 %s 行動\n", next_p->name);
            }
        }
    }
}

void next_player() {
    do {
        current_player = (current_player + 1) % player_count;
    } while (!players[current_player].active);
}

void notify_lobby(int type, int sock, int rfd) {


    Message return_msg;
    return_msg.type = type;
    snprintf(return_msg.data, sizeof(return_msg.data), "%d", sock);
    Writen(rfd, &return_msg, sizeof(Message));
}
