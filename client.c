/*
=====================================
Name: Vanshika Tulsyan
Roll number: 22EE10075
=====================================
*/

#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/fcntl.h>
#include<errno.h>
#include<netinet/tcp.h>

#define BUF_SIZE 1024
#define EXP_LEN 100

int calc_result(char* expr){
    // expr is of the form <number> <op> <number>
    int num1, num2;
    char op;
    sscanf(expr, "%d %c %d", &num1, &op, &num2);
    int ans = 0;
    switch(op){
        case '+':
            ans = num1 + num2;
            break;
        case '-':
            ans = num1 - num2;
            break;
        case '*':
            ans = num1 * num2;
            break;
        case '/':
            ans = num1 / num2;
            break;
    }
    return ans;
}

// <---------Code for normal functioning client--------->

int main(){
    int sockfd;
    struct sockaddr_in server_addr;
    char buf[BUF_SIZE];
    char expr[EXP_LEN];

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        perror("Error creating socket");
        exit(1);
    }

    // Enable TCP keep-alive
    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        perror("Error enabling SO_KEEPALIVE");
        exit(EXIT_FAILURE);
    }

    // Increase TCP keep-alive idle time (e.g., 600 seconds = 10 minutes)
    optval = 600;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0) {
        perror("Error setting TCP_KEEPIDLE");
        exit(EXIT_FAILURE);
    }

    // Increase TCP keep-alive interval between probes (e.g., 60 seconds)
    optval = 60;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0) {
        perror("Error setting TCP_KEEPINTVL");
        exit(EXIT_FAILURE);
    }

    // Increase the number of keep-alive probes (e.g., 10 probes before dropping)
    optval = 10;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) < 0) {
        perror("Error setting TCP_KEEPCNT");
        exit(EXIT_FAILURE);
    }

    // Initialize the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error connecting to server");
        exit(1);
    }

    // make it non blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    while(1){
        // send GET_TASK request
        memset(buf, 0, BUF_SIZE);
        strcpy(buf, "GET_TASK");

        // non blocking send
        int total_bytes_sent = 0;
        while(1){
            int bytes_sent = send(sockfd, buf, strlen(buf), 0);
            if(bytes_sent < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    usleep(1000);
                    continue;
                }else{
                    perror("Error sending data");
                    exit(1);
                }
            }else{
                total_bytes_sent += bytes_sent;
                if(total_bytes_sent >= strlen(buf)){
                    break;
                }
            }
        }

        printf("Sent GET_TASK request\n");

        // non blocking receive of the assigned task
        while(1){
            memset(buf, 0, BUF_SIZE);
            memset(expr, 0, EXP_LEN);
            int bytes_recv = recv(sockfd, expr, EXP_LEN, 0);
            if(bytes_recv < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    usleep(1000);
                    continue;
                }else{
                    perror("Error receiving data");
                    exit(1);
                }
            }else if(bytes_recv == 0){
                printf("Server closed the connection\n");
                break;
            }else break;
        }

        // check for "No tasks available" 
        if(strncmp(expr, "No tasks available", 18) == 0){
            printf("No tasks available....Exiting\n");
            memset(buf, 0, BUF_SIZE);
            strcpy(buf, "EXIT");
            send(sockfd, buf, strlen(buf), 0);
            break;
        }
        // calculate the result
        printf("%s\n", expr);

        int result = calc_result(expr+6); // TASK: <expr>
        // send RESULT result
        memset(buf, 0, BUF_SIZE);
        total_bytes_sent = 0;
        sprintf(buf, "RESULT %d", result);
        while(1){
            int bytes_sent = send(sockfd, buf, strlen(buf), 0);
            if(bytes_sent < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    usleep(1000);
                    continue;
                }else{
                    perror("Error sending data");
                    exit(1);
                }
            }else{
                total_bytes_sent += bytes_sent;
                if(total_bytes_sent >= strlen(buf)){
                    break;
                }
            }
        }

        sleep(1); // This has been added so that each client takes some time before next GET_TASK Request so that multiple clients can be tested
    }
    close(sockfd);
    return 0;
}

// <---------Code for client that just connects and does nothing--------->
// These processes will be terminated or released from the server side
// int main(){
//     int sockfd;
//     struct sockaddr_in server_addr;
//     char buf[BUF_SIZE];
//     char expr[EXP_LEN];

//     // Create a TCP socket
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if(sockfd < 0){
//         perror("Error creating socket");
//         exit(1);
//     }

//     // Initialize the server address
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(8080);
//     server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

//     // Connect to the server
//     if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
//         perror("Error connecting to server");
//         exit(1);
//     }


//     while(1); // Do nothing
//     close(sockfd);
//     return 0;
// }

//<---------Code for client that connects and keeps sending GET_TASK request--------->
// int main(){
//     int sockfd;
//     struct sockaddr_in server_addr;
//     char buf[BUF_SIZE];
//     char expr[EXP_LEN];

//     // Create a TCP socket
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if(sockfd < 0){
//         perror("Error creating socket");
//         exit(1);
//     }

//     // Initialize the server address
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(8080);
//     server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

//     // Connect to the server
//     if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
//         perror("Error connecting to server");
//         exit(1);
//     }


//     while(1){
//         // send GET_TASK request
//         memset(buf, 0, BUF_SIZE);
//         strcpy(buf, "GET_TASK");
//         send(sockfd, buf, strlen(buf), 0);
//         printf("Sent GET_TASK request\n");

//         memset(buf, 0, BUF_SIZE);
//         recv(sockfd, buf, BUF_SIZE, 0);
//         printf("Received: %s\n", buf);
//         sleep(1);
//     }
//     close(sockfd);
//     return 0;
// }

//<---------Code for client that connects takes a task and does nothing--------->

// int main(){
//     int sockfd;
//     struct sockaddr_in server_addr;
//     char buf[BUF_SIZE];
//     char expr[EXP_LEN];

//     // Create a TCP socket
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if(sockfd < 0){
//         perror("Error creating socket");
//         exit(1);
//     }

//     // Initialize the server address
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(8080);
//     server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

//     // Connect to the server
//     if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
//         perror("Error connecting to server");
//         exit(1);
//     }


//     while(1); // Do nothing
//     close(sockfd);
//     return 0;
// }

//<---------Code for client that connects and keeps sending GET_TASK request--------->
// int main(){
//     int sockfd;
//     struct sockaddr_in server_addr;
//     char buf[BUF_SIZE];
//     char expr[EXP_LEN];

//     // Create a TCP socket
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if(sockfd < 0){
//         perror("Error creating socket");
//         exit(1);
//     }

//     // Initialize the server address
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(8080);
//     server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

//     // Connect to the server
//     if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
//         perror("Error connecting to server");
//         exit(1);
//     }


//     while(1){
//         // send GET_TASK request
//         memset(buf, 0, BUF_SIZE);
//         strcpy(buf, "GET_TASK");
//         send(sockfd, buf, strlen(buf), 0);
//         printf("Sent GET_TASK request\n");

//         memset(buf, 0, BUF_SIZE);
//         recv(sockfd, buf, BUF_SIZE, 0);
//         printf("Received: %s\n", buf);
//         while(1);
//     }
//     close(sockfd);
//     return 0;
// }
