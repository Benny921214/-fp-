#include "unp.h"
#include <time.h>

#define MAX_PLAYERS 5
#define MIN_PLAYERS 2
#define MAX_DICE 5
#define TIMEOUT 20

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

Player players[MAX_PLAYERS];
int player_count = 0;
int current_player = 0;
int last_point = 0;
int last_quantity = 0;
int game_started = 0;
int game_over = 0;

void init_player(int sockfd);
void send_welcome_message(Player *p);
void send_dice_info(Player *p);
void broadcast(Message *msg);
void handle_client_message(int sockfd);
void handle_open_cup(int sockfd);
void player_disconnect(int sockfd);
void next_player();
void handle_timeout();
void send_to_player(Player *p, Message *msg);


int main(int argc, char **argv) {
    int listenfd, connfd, maxfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    Message msg;

    srand(time(NULL));

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(12345);

    Bind(listenfd, (SA *)&servaddr, sizeof(servaddr));

    Listen(listenfd, MAX_PLAYERS);

    printf("server start, wait for client connect...\n");

    while (!game_over) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(listenfd, &rset);
        maxfd = listenfd;

        // player socket 加到 rset
        if (game_started && players[current_player].active) {
            FD_SET(players[current_player].sockfd, &rset);
            if (players[current_player].sockfd > maxfd) {
                maxfd = players[current_player].sockfd;
            }
        }

        struct timeval tv;
        struct timeval *timeout = NULL;
        if (game_started && players[current_player].active) {
            tv.tv_sec = TIMEOUT;
            tv.tv_usec = 0;
            timeout = &tv;
        }
        else {
            // 遊戲還沒開始 檢查是否可以開始遊戲的等待時間
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            timeout = &tv;
        }
        int select_ret = Select(maxfd + 1, &rset, NULL, NULL, timeout);

        if (select_ret == 0) {
            // timeout
            if (game_started && players[current_player].active) {
                printf("玩家 %s 超時\n", players[current_player].name);
                handle_timeout();
            }
            else if (player_count >= MIN_PLAYERS && game_started == 0) {
                // 遊戲還沒開始，檢查是否可以開始遊戲
                game_started = 1;
                Player *p = &players[current_player];
                if (p->active) {
                    msg.type = 3;
                    send_to_player(p, &msg);
                    printf("game start，輪到 %s 行動\n", p->name);
                }
            }
            continue; // next loop
        }

        if (FD_ISSET(listenfd, &rset)) {
            clilen = sizeof(cliaddr);
            connfd = Accept(listenfd, (SA *)&cliaddr, &clilen); // 接受新連線
            // 還沒滿 >> 初始化玩家
            if (player_count < MAX_PLAYERS) {
                init_player(connfd);
            }
            // 已滿 >> 關閉連線
            else {
                Close(connfd);
            }

            // 玩家數量到達要求，且遊戲還沒開始 >> 通知player1開始
            if (player_count >= MIN_PLAYERS && game_started == 0) {
                game_started = 1;
                Player *p = &players[current_player];
                if (p->active) {
                    msg.type = 3;
                    send_to_player(p, &msg);
                    printf("game start，輪到 %s 行動\n", p->name);
                }
            }

            continue;
        }

        // 處理當前玩家回應
        if (game_started && players[current_player].active && FD_ISSET(players[current_player].sockfd, &rset)) {
            handle_client_message(players[current_player].sockfd);
        }
    }
    printf("遊戲結束，server關閉。\n");
    return 0;
}

void init_player(int sockfd) {
    Player *p = &players[player_count];
    p->sockfd = sockfd;
    sprintf(p->name, "Player%d", player_count + 1);
    p->active = 1;
    for (int i = 0; i < MAX_DICE; i++) {
        p->dice[i] = rand() % 6 + 1;
    }
    send_welcome_message(p);
    send_dice_info(p);
    player_count++;
    printf("%s 已連接\n", p->name);
}

void send_welcome_message(Player *p) {
    Message msg;
    msg.type = 1;
    snprintf(msg.data, sizeof(msg.data), "welcome to game，%s！", p->name);
    send_to_player(p, &msg);
}

void send_dice_info(Player *p) {
    Message msg;
    msg.type = 2;
    snprintf(msg.data, sizeof(msg.data), "%d %d %d %d %d",
             p->dice[0], p->dice[1], p->dice[2], p->dice[3], p->dice[4]);
    send_to_player(p, &msg);
}

void broadcast(Message *msg) {
    for (int i = 0; i < player_count; i++) {
        if (players[i].active) {
            send_to_player(&players[i], msg);
        }
    }
}

void send_to_player(Player *p, Message *msg) {
    Writen(p->sockfd, msg, sizeof(Message));
}

void handle_client_message(int sockfd) {
    int n;
    Message msg;
    n = Read(sockfd, &msg, sizeof(Message));
    if (n == 0) {
        player_disconnect(sockfd);
        return;
    }
    Player *p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (p == NULL)
        return;

    if (msg.type == 4) { // 喊點數
        int point, quantity;
        sscanf(msg.data, "%d %d", &point, &quantity);
        if (quantity > last_quantity || (quantity == last_quantity && point > last_point)) {
            last_point = point;
            last_quantity = quantity;
            Message broadcast_msg;
            broadcast_msg.type = 4;
            snprintf(broadcast_msg.data, sizeof(broadcast_msg.data),
                     "%s 喊 %d %d", p->name, point, quantity);
            broadcast(&broadcast_msg);
            next_player();

            // notify next player
            Player *next_p = &players[current_player];
            if (next_p->active) {
                Message msg;
                msg.type = 3;
                send_to_player(next_p, &msg);
                printf("輪到 %s 行動\n", next_p->name);
            }
        }
        else {
            // 無效喊點 >> 通知玩家
            Message error_msg;
            error_msg.type = 3;
            send_to_player(p, &error_msg);
        }
    }
    else if (msg.type == 5) { // 開盅
        handle_open_cup(sockfd);
    }
    else if (msg.type == 7) { // 玩家請求中離
        player_disconnect(sockfd);
    }
}

void handle_open_cup(int sockfd) {
    Player *p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (p == NULL)
        return;

    Message broadcast_msg;
    broadcast_msg.type = 5;
    snprintf(broadcast_msg.data, sizeof(broadcast_msg.data), "%s 選擇開盅", p->name);
    broadcast(&broadcast_msg);

    // 計算實際數量
    int actual_quantity = 0;
    for (int i = 0; i < player_count; i++) {
        if (players[i].active) {
            for (int j = 0; j < MAX_DICE; j++) {
                if (players[i].dice[j] == last_point) {
                    actual_quantity++;
                }
            }
        }
    }

    // 勝負判斷
    Message result_msg;
    result_msg.type = 6;
    if (actual_quantity >= last_quantity) {
        snprintf(result_msg.data, sizeof(result_msg.data), "實際數量為 %d，%s 赢了！", actual_quantity, players[(current_player - 1 + player_count) % player_count].name);
    }
    else {
        snprintf(result_msg.data, sizeof(result_msg.data), "實際數量為 %d，%s 赢了！", actual_quantity, p->name);
    }
    broadcast(&result_msg);

    // 遊戲結束 >> 關閉所有連線
    for (int i = 0; i < player_count; i++) {
        Close(players[i].sockfd);
    }
    game_over = 1;
}

void player_disconnect(int sockfd) {
    Player *p = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].sockfd == sockfd) {
            p = &players[i];
            break;
        }
    }
    if (p == NULL)
        return;
    printf("%s 中離\n", p->name);
    p->active = 0;
    Close(sockfd);

    Message msg;
    msg.type = 7;
    snprintf(msg.data, sizeof(msg.data), "%s 已中離", p->name);
    broadcast(&msg);

    // case 只剩一位玩家
    int active_players = 0;
    Player *last_player = NULL;
    for (int i = 0; i < player_count; i++) {
        if (players[i].active) {
            active_players++;
            last_player = &players[i];
        }
    }
    if (active_players == 1 && game_started) {
        Message result_msg;
        result_msg.type = 6;
        snprintf(result_msg.data, sizeof(result_msg.data),
                 "只剩 %s 在線，遊戲結束！", last_player->name);
        broadcast(&result_msg);
        Close(last_player->sockfd);
        game_over = 1;
    }

    // 如果為當前玩家中離，調整 current_player 為下一位玩家
    if (p - players == current_player) {
        next_player();
        // notify next player
        Player *next_p = &players[current_player];
        if (next_p->active) {
            Message msg;
            msg.type = 3;
            send_to_player(next_p, &msg);
            printf("輪到 %s 行動\n", next_p->name);
        }
    }
}

void next_player() {
    do {
        current_player = (current_player + 1) % player_count;
    } while (!players[current_player].active);
}

void handle_timeout() {
    printf("玩家 %s 超過時限，系統自動選擇開盅。\n", players[current_player].name);
    handle_open_cup(players[current_player].sockfd);
}
