#include "unp.h"

typedef struct {
    int type;
    char data[MAXLINE];
} Message;

void parse_dice_info(char *data);
void player_action(int sockfd);

int my_turn = 0;

int main(int argc, char **argv) {
    int sockfd, n;
    struct sockaddr_in servaddr;
    Message msg;

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

    printf("以連接server\n");

    fd_set rset;
    int maxfd = sockfd > fileno(stdin) ? sockfd : fileno(stdin);

    while (1) {
        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        FD_SET(fileno(stdin), &rset);

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
                        exit(0);
                        break;
                    case 7:
                        // 玩家中離
                        printf("%s\n", msg.data);
                        break;
                    default:
                        break;
                }
            }
        }
        // 處理玩家輸入
        if (FD_ISSET(fileno(stdin), &rset)) {
            player_action(sockfd);
        }
    }

    return 0;
}

void parse_dice_info(char *data) {
    int dice[5];
    sscanf(data, "%d %d %d %d %d", &dice[0], &dice[1], &dice[2], &dice[3], &dice[4]);
    printf("你的骰子點數為：%d %d %d %d %d\n", dice[0], dice[1], dice[2], dice[3], dice[4]);
}

void player_action(int sockfd) {
    if (!my_turn) {
        // 如果不是自己的回合，忽略輸入
        printf("現在不是你的回合，請等待其他玩家。\n");
        // 清空stdin
        while (getchar() != '\n');
        return;
    }

    int choice, point, quantity;
    Message msg;

    if (scanf("%d", &choice) != 1) {
        printf("輸入錯誤，請輸入數字。\n");
        while (getchar() != '\n');
        return;
    }
    if (choice == 1) {
        printf("請輸入點數（1-6）：");
        if (scanf("%d", &point) != 1) {
            printf("輸入錯誤，請輸入數字（1-6）。\n");
            while (getchar() != '\n');
            return;
        }
        printf("請輸入數量：");
        if (scanf("%d", &quantity) != 1) {
            printf("輸入錯誤，請輸入數量。\n");
            while (getchar() != '\n');
            return;
        }
        msg.type = 4;
        snprintf(msg.data, sizeof(msg.data), "%d %d", point, quantity);
        Writen(sockfd, &msg, sizeof(Message));
    }
    else if (choice == 2) {
        msg.type = 5;
        Writen(sockfd, &msg, sizeof(Message));
    }
    else {
        printf("選擇無效，請重新選擇動作。\n");
    }

    my_turn = 0;
}
