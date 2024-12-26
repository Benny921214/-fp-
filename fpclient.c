#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "unp.h"

typedef struct {
    int type;
    char data[MAXLINE];
} Message;

void parse_dice_info(char *data);
void player_action(int sockfd, const char *input);

int my_turn = 0;

int main(int argc, char **argv) {
    int sockfd, n;
    struct sockaddr_in servaddr;
    Message msg;
    bool inRoom = false, gameStart = false, gameFinish = false;
    bool is_eof = false; // 新增旗標
    char rline[MAXLINE];

    if (argc != 2) {
        printf("用法：%s <IP address>\n", argv[0]);
        exit(1);
    }

    sockfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(12345);
    Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    Connect(sockfd, (SA *)&servaddr, sizeof(servaddr));

    printf("已連接server\n");

    fd_set rset;
    int maxfd = (sockfd > fileno(stdin)) ? sockfd : fileno(stdin);

    while (1) {
        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        if (!is_eof) { // 只有在未處理 EOF 時才監聽 stdin
            FD_SET(fileno(stdin), &rset);
        }

        Select(maxfd + 1, &rset, NULL, NULL, NULL);

        if (FD_ISSET(sockfd, &rset)) {
            n = Read(sockfd, &msg, sizeof(Message));
            if (n == 0) {
                printf("server已關閉connection\n");
                exit(0);
            } else {
                switch (msg.type) {
                    case 1:
                        // 歡迎訊息
                        printf("%s\n", msg.data);
                        break;
                    case 2:
                        // 接收骰子點數
                        parse_dice_info(msg.data);
                        gameStart = true;
                        break;
                    case 3:
                        // 當前行動
                        printf("輪到你行動，請選擇：\n1. 喊點\n2. 開盅\n");
                        my_turn = 1;
                        break;
                    case 4:
                        // 其他玩家喊點
                        printf("%s\n", msg.data);
                        break;
                    case 5:
                        // 開盅
                        printf("%s\n", msg.data);
                        break;
                    case 6:
                        // 結果
                        printf("%s\n", msg.data);
                        printf("是否回到大廳(1:是 2:否)\n");
                        gameFinish = true;
                        break;
                    case 7:
                        // 玩家中離
                        printf("%s\n", msg.data);
                        break;
                    case 8:
                        // 進入房間
                        printf("%s\n", msg.data);
                        inRoom = true;
                        break;
                    case 9:
                        printf("%s\n", msg.data);
                        // 進入/回到大廳
                        inRoom = false;
                        gameStart = false;
                        gameFinish = false;
                        is_eof = false;
                        clearerr(stdin);
                        break;
                    case 12:
                        // 確認訊息
                        printf("%s\n", msg.data);
                        break;
                    default:
                        printf("未知的訊息類型: %d\n", msg.type);
                        break;
                }
            }
        }

        // 處理玩家輸入
        if (!is_eof && FD_ISSET(fileno(stdin), &rset)) {
            // 讀一行字串，若按 Ctrl + D => fgets() 回傳 NULL
            if (fgets(rline, sizeof(rline), stdin) == NULL) {
                // Ctrl + D => 回大廳
                msg.type = 9;
                snprintf(msg.data, sizeof(msg.data), "leaveRoom");
                printf("Debug: Sending type=9 message to server.\n");
                Writen(sockfd, &msg, sizeof(Message));

                inRoom = false;
                gameStart = false;
                gameFinish = false;
                is_eof = true; // 設置旗標，表示已處理 EOF
                printf("已發送回大廳請求，請等待伺服器回應。\n");
                // 從 fd_set 中移除 stdin
                // 由於在下一次迴圈中已經不會再加上 stdin，所以不需要額外操作
                continue;
            }

            if (inRoom) {
                if (gameFinish) {
                    int choice;
                    if (sscanf(rline, "%d", &choice) != 1) {
                        printf("輸入錯誤，請輸入數字。\n");
                        continue;
                    }

                    if (choice == 1) {
                        msg.type = 11;
                        Writen(sockfd, &msg, sizeof(Message));
                    }
                    else if(choice == 2) {
                        msg.type = 10;
                        Writen(sockfd, &msg, sizeof(Message));
                        Close(sockfd);
                        exit(0);
                    }
                    else {
                        printf("輸入錯誤，請輸入正確的數字。\n");
                    }
                }
                else if (gameStart) {
                    player_action(sockfd, rline);
                }
                else {
                    printf("（房間內，遊戲尚未開始）輸入：%s", rline);
                }
            }
            else {
                // 在大廳內
                int choice;
                if (sscanf(rline, "%d", &choice) != 1) {
                    printf("輸入錯誤，請輸入數字。\n");
                    continue;
                }
                if (choice >= 1 && choice <= 3) {
                    msg.type = 8; 
                    snprintf(msg.data, sizeof(msg.data), "%d", choice + 1);
                    // printf("Debug: Sending type=8 message to server with choice=%d.\n", choice + 1);
                    Writen(sockfd, &msg, sizeof(Message));
                } else {
                    printf("輸入錯誤，請輸入正確的數字。\n");
                }
            }
        }
    }
    return 0;
}

void parse_dice_info(char *data) {
    int dice[5];
    sscanf(data, "%d %d %d %d %d", &dice[0], &dice[1], &dice[2], &dice[3], &dice[4]);
    printf("你的骰子點數為：%d %d %d %d %d\n", 
           dice[0], dice[1], dice[2], dice[3], dice[4]);
}

// -------------- 改成含兩個參數的實作 --------------
void player_action(int sockfd, const char *input) {
    if (!my_turn) {
        // 如果不是自己的回合，忽略輸入
        printf("現在不是你的回合，請等待其他玩家。\n");
        return;
    }

    int choice, point, quantity;
    Message msg;

    if (sscanf(input, "%d", &choice) != 1) {
        printf("輸入錯誤，請輸入數字。\n");
        return;
    }

    if (choice == 1) {
        // 把 point/quantity 一起輸入
        int cnt = sscanf(input, "%d %d %d", &choice, &point, &quantity);
        if (cnt < 3) {
            // 沒帶到 point/quantity => 手動詢問
            char buf[MAXLINE];
            printf("請輸入點數（1-6）：");
            fflush(stdout);
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                // Ctrl+D => 離房
                msg.type = 9;
                snprintf(msg.data, sizeof(msg.data), "leaveRoom");
                // printf("Debug: Sending type=9 message to server from player_action.\n");
                Writen(sockfd, &msg, sizeof(Message));
                my_turn = 0;
                return;
            }
            if (sscanf(buf, "%d", &point) != 1 || point < 1 || point > 6) {
                printf("點數輸入錯誤\n");
                my_turn = 0;
                return;
            }

            printf("請輸入數量：");
            fflush(stdout);
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                msg.type = 9;
                snprintf(msg.data, sizeof(msg.data), "leaveRoom");
                // printf("Debug: Sending type=9 message to server from player_action.\n");
                Writen(sockfd, &msg, sizeof(Message));
                my_turn = 0;
                return;
            }
            if (sscanf(buf, "%d", &quantity) != 1 || quantity <= 0) {
                printf("數量輸入錯誤\n");
                my_turn = 0;
                return;
            }
        } else {
            // cnt==3: 已經一次讀到 1 3 2 => choice=1, point=3, quantity=2
            if (point < 1 || point > 6 || quantity <= 0) {
                printf("輸入點數或數量有誤\n");
                my_turn = 0;
                return;
            }
        }

        msg.type = 4;
        snprintf(msg.data, sizeof(msg.data), "%d %d", point, quantity);
        // printf("Debug: Sending type=4 message to server with point=%d, quantity=%d.\n", point, quantity);
        Writen(sockfd, &msg, sizeof(Message));
    }
    else if (choice == 2) {
        msg.type = 5;
        // printf("Debug: Sending type=5 message to server to open cup.\n");
        Writen(sockfd, &msg, sizeof(Message));
    }
    else {
        printf("選擇無效，請重新選擇動作。\n");
    }

    my_turn = 0;
}
