#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_USERS 5

typedef struct {
    char username[50];
    char password[50];
} User;

User users[MAX_USERS] = { {"admin", "pass123"}, {"user", "1234"} };

int authenticate(char *credentials) {
    char username[50], password[50];
    if (sscanf(credentials, "%49s %49s", username, password) != 2) return 0;
    for (int i = 0; i < MAX_USERS; i++) {
        if (strcmp(username, users[i].username) == 0 && strcmp(password, users[i].password) == 0) {
            return 1;
        }
    }
    return 0;
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    
    write(sock, "Enter username password:", 25);
    if (read(sock, buffer, BUFFER_SIZE) <= 0 || !authenticate(buffer)) {
        write(sock, "AUTH_FAILED", 11);
        close(sock);
        return NULL;
    }
    write(sock, "AUTH_SUCCESS", 12);
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        if (read(sock, buffer, BUFFER_SIZE) <= 0) break;

        if (strncmp(buffer, "UPLOAD", 6) == 0) {
            char filename[256];
            if (sscanf(buffer + 7, "%255s", filename) != 1) {
                write(sock, "ERROR", 5);
                continue;
            }
            int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (file < 0) {
                write(sock, "ERROR", 5);
                continue;
            }
            while (1) {
                int bytes_received = read(sock, buffer, BUFFER_SIZE);
                if (bytes_received <= 0 || strcmp(buffer, "EOF") == 0) break;
                write(file, buffer, bytes_received);
            }
            close(file);
            write(sock, "SUCCESS", 7);
        } else if (strncmp(buffer, "DOWNLOAD", 8) == 0) {
            char filename[256];
            if (sscanf(buffer + 9, "%255s", filename) != 1) {
                write(sock, "ERROR", 5);
                continue;
            }
            int file = open(filename, O_RDONLY);
            if (file < 0) {
                write(sock, "ERROR", 5);
                continue;
            }
            int bytes_read;
            while ((bytes_read = read(file, buffer, BUFFER_SIZE)) > 0) {
                write(sock, buffer, bytes_read);
            }
            write(sock, "EOF", 3);
            close(file);
        } else if (strncmp(buffer, "EXIT", 4) == 0) {
            break;
        }
    }
    close(sock);
    return NULL;
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    while (1) {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) continue;
        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (*client_socket < 0) {
            free(client_socket);
            continue;
        }
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket);
        pthread_detach(thread);
    }
    close(server_socket);
    return 0;
}
