/*
=====================================
Name: Vanshika Tulsyan
Roll number: 22EE10075
=====================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>

#define BUF_SIZE 1024
#define EXP_LEN 100
#define MAX_CLIENTS 100
#define MAX_TASKS 1000
#define SM_KEY ftok("/", 'a')
#define SM_CLI ftok("/", 'z')
#define SEM_KEY ftok("/", 'e')
#define SEM_CLI ftok("/", 'f')
#define SM_CNT ftok("/", 'h')

typedef struct _task{
    char expr[EXP_LEN];
    int is_assigned;
} task;

typedef struct _client{
    pid_t pid;
    int sockfd;
    int has_task;
    int req_cnt;
} client;

int task_count = 0; 

// Shared memory and semaphore IDs
int shmid, semid, shmcli, semcli, shm_cnt;

// Get shared memory
int get_shm_tasks(key_t key) {
    int id = shmget(key, sizeof(task) * MAX_TASKS, IPC_CREAT | 0666);
    if (id == -1) {
        perror("shmget failed");
        exit(1);
    }
    return id;
}

// Get shared memory for clients
int get_shm_clients(key_t key) {
    int id = shmget(key, sizeof(client) * MAX_CLIENTS, IPC_CREAT | 0666);
    if (id == -1) {
        perror("shmget failed");
        exit(1);
    }
    return id;
}

// Get semaphore for tasks
int get_sem(key_t key) {
    int id = semget(key, 1, IPC_CREAT | 0666);
    if (id == -1) {
        perror("semget failed");
        exit(1);
    }
    return id;
}

// Get semaphore for clients
int get_sem_cli(key_t key) {
    int id = semget(key, 1, IPC_CREAT | 0666);
    if (id == -1) {
        perror("semget failed");
        exit(1);
    }
    return id;
}

// Update semaphore value
void update_sem(int semid, int val) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = val;
    sb.sem_flg = 0;
    
    if (semop(semid, &sb, 1) == -1) {
        perror("semop failed");
        exit(1);
    }
}

void load_tasks(char* filename) {
    // Initialize the shared memory and semaphore
    shmid = get_shm_tasks(SM_KEY);
    semid = get_sem(SEM_KEY);
    shmcli = get_shm_clients(SM_CLI);
    semcli = get_sem_cli(SEM_CLI);

    shm_cnt = shmget(SM_CNT, sizeof(int), IPC_CREAT | 0666);
    int *client_count = (int *)shmat(shm_cnt, NULL, 0);
    *client_count = 0;

    // Initialize semaphore value to 1
    if (semctl(semid, 0, SETVAL, 1) == -1) {
        perror("semctl failed");
        exit(1);
    }

    if (semctl(semcli, 0, SETVAL, 1) == -1) {
        perror("semctl failed");
        exit(1);
    }

    task *tasks = (task *)shmat(shmid, NULL, 0);
    
    // Load tasks from tasks.txt
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Error opening tasks.txt");
        exit(1);
    }
    
    char line[EXP_LEN];
    while (fgets(line, EXP_LEN, fp) && task_count < MAX_TASKS) { // Read line by line
        line[strcspn(line, "\n")] = 0; // Remove newline character
        
        // Add task to array
        strcpy(tasks[task_count].expr, line);
        tasks[task_count].is_assigned = 0;
        task_count++;
    }
    
    fclose(fp);
    printf("Loaded %d tasks\n", task_count);
    
}

void sigchld_handler(int signo) { // Signal handler for SIGCHLD
    pid_t pid;
    int status;

    client *clients = (client *)shmat(shmcli, NULL, 0);
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { // Child with pid has terminated
        for (int i = 0; i < MAX_CLIENTS; i++) {
            update_sem(semcli, -1); // Lock
            if (clients[i].pid == pid) {
                printf("Child process %d terminated\n", pid);

                // If client had a task, make it available again
                if (clients[i].has_task) {
                    task *shared_tasks = (task *)shmat(shmid, NULL, 0);
                    if (shared_tasks == (task *)-1) {
                        perror("shmat failed");
                        update_sem(semcli, 1); // Unlock
                        exit(1);
                    }
                    
                    // Update the task status
                    update_sem(semid, -1); // Lock
                    
                    // search for the task
                    for (int j = 0; j < MAX_TASKS; j++) {
                        if (shared_tasks[j].is_assigned == clients[i].pid) {
                            shared_tasks[j].is_assigned = 0;
                            break;
                        }
                    }
                    
                    update_sem(semid, 1); // Unlock
                    
                    // Detach shared memory
                    if (shmdt(shared_tasks) == -1) {
                        perror("shmdt failed");
                        update_sem(semcli, 1); // Unlock
                        exit(1);
                    }
                }
                
                // Close the socket
                close(clients[i].sockfd);
                update_sem(semcli, 1); // Unlock
                break;
            }
            update_sem(semcli, 1); // Unlock
        }
    }
}

// Function to handle client connections
void client_socket(int id) {
    char buf[BUF_SIZE];
    char expr[EXP_LEN];
    client *clients = (client *)shmat(shmcli, NULL, 0);

    update_sem(semcli, -1); // Lock
    int sockfd = clients[id].sockfd;
    update_sem(semcli, 1); // Unlock
    
    // Make non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    while(1) {
        // Wait for getting GET_TASK from client for 5s at max
        int retries = 5000;
        int received = 0;
        int got_res = 0;
        
        while(retries-- && !received) {
            memset(buf, 0, BUF_SIZE);
            int bytes_recv = recv(sockfd, buf, BUF_SIZE, 0);
            if(bytes_recv < 0) { 
                if(errno == EAGAIN || errno == EWOULDBLOCK) { // If no data is available, wait for retries
                    usleep(1000);
                    continue;
                } else {
                    perror("Error receiving data");
                    exit(1);
                }
            } else if(bytes_recv == 0) {
                // Client closed the connection
                close(sockfd);
                exit(0);
            } else {
                received = 1;
                // Check if client wants to exit
                if(strncmp(buf, "EXIT", 4) == 0) {
                    printf("Client with PID: %d is exiting\n", clients[id].pid);
                    
                    // If client had a task, make it available again
                    update_sem(semcli, -1); // Lock
                    if(clients[id].has_task) {
                        // Get the shared memory
                        task *shared_tasks = (task *)shmat(shmid, NULL, 0);
                        if(shared_tasks == (task *)-1) {
                            perror("shmat failed");
                            update_sem(semcli, 1); // Unlock
                            exit(1);
                        }
                        
                        // Update the task status
                        update_sem(semid, -1); // Lock
                        
                        // At max one task can be assigned to the client
                        for(int i = 0; i < MAX_TASKS; i++) {
                            if(shared_tasks[i].is_assigned == clients[id].pid) {
                                shared_tasks[i].is_assigned = 0;
                                break;
                            }
                        }
                        
                        update_sem(semid, 1); // Unlock
                        
                        // Detach shared memory
                        if(shmdt(shared_tasks) == -1) {
                            perror("shmdt failed");
                            update_sem(semcli, 1); // Unlock
                            exit(1);
                        }
                    }
                    update_sem(semcli, 1); // Unlock    
                    
                    close(sockfd);
                    exit(0);
                }
                
                // Check if client wants a task
                if(strncmp(buf, "GET_TASK", 8) == 0) {
                    clients[id].req_cnt++;
                    break;
                }
                
                // Check if client is sending a result
                if(strncmp(buf, "RESULT", 6) == 0) {
                    int result;
                    sscanf(buf + 7, "%d", &result);
                    printf("Received result: %d from client with PID: %d\n", result, clients[id].pid);
                    clients[id].req_cnt--;
                    // Mark the client as not having a task
                    update_sem(semcli, -1); // Lock
                    clients[id].has_task = 0;
                    update_sem(semcli, 1); // Unlock
                    
                    // Get the shared memory
                    task *shared_tasks = (task *)shmat(shmid, NULL, 0);
                    if(shared_tasks == (task *)-1) {
                        perror("shmat failed");
                        exit(1);
                    }
                    // Continue to wait for next request
                    got_res =1;
                    continue;
                }
            }
        }

        if(got_res) {
            continue;
        }
        
        // If retries are exhausted, close the connection
        if(retries <= 0 && !received) {
            close(sockfd);
            printf("Client with PID: %d is not responding anymore....Closing it\n", clients[id].pid);
            exit(0);
        }
        
        // Check if client already has a task
        update_sem(semcli, -1); // Lock
        if(clients[id].has_task) {
            memset(buf, 0, BUF_SIZE);
            int close_client = 0;
            if(clients[id].req_cnt > 5){
                strcpy(buf, "You have reached the maximum number of requests without results. Terminating you...\n");
                close_client = 1;
            }
            else strcpy(buf, "You already have a task assigned. Complete it first.");
            
            // Non-blocking send
            int total_bytes_sent = 0;
            while(1) {
                int bytes_sent = send(sockfd, buf + total_bytes_sent, strlen(buf) - total_bytes_sent, 0);
                if(bytes_sent < 0) {
                    if(errno == EAGAIN || errno == EWOULDBLOCK) {
                        usleep(1000);
                        continue;
                    } else {
                        perror("Error sending data");
                        update_sem(semcli, 1); // Unlock
                        exit(1);
                    }
                } else {
                    total_bytes_sent += bytes_sent;
                    if(total_bytes_sent >= strlen(buf)) {
                        break;
                    }
                }
            }
            update_sem(semcli, 1); // Unlock

            if(close_client){
                // If client had a task, make it available again
                update_sem(semcli, -1); // Lock
                if(clients[id].has_task) {
                    // Get the shared memory
                    task *shared_tasks = (task *)shmat(shmid, NULL, 0);
                    if(shared_tasks == (task *)-1) {
                        perror("shmat failed");
                        update_sem(semcli, 1); // Unlock
                        exit(1);
                    }
                    
                    // Update the task status
                    update_sem(semid, -1); // Lock
                    
                    // At max one task can be assigned to the client
                    for(int i = 0; i < MAX_TASKS; i++) {
                        if(shared_tasks[i].is_assigned == clients[id].pid) {
                            shared_tasks[i].is_assigned = 0;
                            break;
                        }
                    }
                    
                    update_sem(semid, 1); // Unlock
                    
                    // Detach shared memory
                    if(shmdt(shared_tasks) == -1) {
                        perror("shmdt failed");
                        update_sem(semcli, 1); // Unlock
                        exit(1);
                    }
                }
                update_sem(semcli, 1); // Unlock   

                close(sockfd);
                exit(0);
            }
            
            continue;
        }
        update_sem(semcli, 1); // Unlock
        
        // Get the shared memory
        task *shared_tasks = (task *)shmat(shmid, NULL, 0);
        if(shared_tasks == (task *)-1) {
            perror("shmat failed");
            exit(1);
        }
        
        // Get a task from the shared memory
        update_sem(semid, -1); // Lock
        
        int task_found = 0;
        for(int i = 0; i < MAX_TASKS; i++) {
            if(shared_tasks[i].is_assigned == 0 && shared_tasks[i].expr[0] != '\0') {
                // Assign task to client
                shared_tasks[i].is_assigned = clients[id].pid;
                strcpy(expr, shared_tasks[i].expr);
                clients[id].has_task = 1;
                task_found = 1;
                break;
            }
        }

        update_sem(semid, 1); // Unlock
        
        // Detach shared memory
        if(shmdt(shared_tasks) == -1) {
            perror("shmdt failed");
            exit(1);
        }
        
        // Send task to client
        memset(buf, 0, BUF_SIZE);
        if(task_found) {
            sprintf(buf, "TASK: %s", expr);
            printf("Assigned task: %s to client with PID: %d\n", expr, clients[id].pid);
        } else {
            strcpy(buf, "No tasks available");
            printf("No tasks available for client with PID: %d\n", clients[id].pid);
        }
        
        // Non-blocking send of the task or no task message
        int total_bytes_sent = 0;
        while(1) {
            int bytes_sent = send(sockfd, buf + total_bytes_sent, strlen(buf) - total_bytes_sent, 0);
            if(bytes_sent < 0) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    usleep(1000);
                    continue;
                } else {
                    perror("Error sending data");
                    exit(1);
                }
            } else {
                total_bytes_sent += bytes_sent;
                if(total_bytes_sent >= strlen(buf)) {
                    break;
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        exit(1);
    }
    char *filename = argv[1];
    // Set up signal handler for SIGCHLD
    signal(SIGCHLD, sigchld_handler);
    load_tasks(filename);

    int sockfd;
    // TCP server side program
    struct sockaddr_in server_addr, client_addr;


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
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the server address
    if(bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error binding socket");
        exit(1);
    }

    // Listen for incoming connections
    if(listen(sockfd, 5) < 0){
        perror("Error listening for connections");
        exit(1);
    }

    // Accept incoming connections
    while(1){
        int clientfd;
        int client_len = sizeof(client_addr);
        clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if(clientfd < 0){
            perror("Error accepting connection");
            exit(1);
        }

        
        pid_t pid = fork();
        // fork a new process for each client
        if(pid == 0){
            close(sockfd);
            update_sem(semcli, -1); // Lock
            client *clients = (client *)shmat(shmcli, NULL, 0);
            int *client_count = (int *)shmat(shm_cnt, NULL, 0);

            // initialise the client
            clients[*client_count].pid = getpid();
            clients[*client_count].sockfd = clientfd;
            clients[*client_count].has_task = 0;
            clients[*client_count].req_cnt = 0;
            *client_count = *client_count + 1;
            update_sem(semcli, 1); // Unlock
            client_socket((*client_count)-1);
        }else{
            close(clientfd);
        }
    }
}
