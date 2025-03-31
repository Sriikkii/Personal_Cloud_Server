#include "cloudserver.h"

User users[MAX_USERS] = { {"admin", "pass123", "JBSWY3DPEHPK3PXP"}, {"user", "1234", "JBSWY3DPEHPK3PXP"} };

int authenticate(char *credentials) {
    char username[50], password[50];
    sscanf(credentials, "%s %s", username, password);
    for (int i = 0; i < MAX_USERS; i++) {
        if (strcmp(username, users[i].username) == 0 && strcmp(password, users[i].password) == 0) {
            return 1;
        }
    }
    return 0;
}

char *generate_totp(char *secret) {
    time_t now = time(NULL);
    int interval = now / 30; // TOTP interval is 30 seconds
    unsigned char *hmac_result = HMAC(EVP_sha1(), secret, strlen(secret), (unsigned char *)&interval, sizeof(interval), NULL, NULL);
    // Convert HMAC result to TOTP code
    // This is a simplified version; in practice, you'd use a more robust TOTP implementation
    char *totp_code = malloc(7);
    snprintf(totp_code, 7, "%06d", *(int *)hmac_result % 1000000);
    return totp_code;
}

int verify_totp(char *user_totp, char *secret) {
    char *server_totp = generate_totp(secret);
    int result = strcmp(user_totp, server_totp) == 0;
    free(server_totp);
    return result;
}

void encrypt_file(char *filename, char *key) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open file for encryption");
        return;
    }

    unsigned char iv[AES_BLOCK_SIZE];
    RAND_bytes(iv, AES_BLOCK_SIZE);

    AES_KEY aes_key;
    AES_set_encrypt_key((unsigned char *)key, 256, &aes_key);

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    unsigned char *plaintext = (unsigned char *)malloc(file_size);
    fread(plaintext, 1, file_size, file);
    fclose(file);

    unsigned char *ciphertext = (unsigned char *)malloc(file_size + AES_BLOCK_SIZE);
    AES_cbc_encrypt(plaintext, ciphertext, file_size, &aes_key, iv, AES_ENCRYPT);

    file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Failed to open file for writing encrypted content");
        free(plaintext);
        free(ciphertext);
        return;
    }
    fwrite(iv, 1, AES_BLOCK_SIZE, file);
    fwrite(ciphertext, 1, file_size + AES_BLOCK_SIZE, file);
    fclose(file);

    free(plaintext);
    free(ciphertext);
}

void setup_firewall() {
    system("iptables -A INPUT -p tcp --dport 22 -j ACCEPT");
    system("iptables -A INPUT -p tcp --dport 80 -j ACCEPT");
    system("iptables -A INPUT -p tcp --dport 443 -j ACCEPT");
    system("iptables -P INPUT DROP");
}

void log_action(char *username, char *action, char *filename, char *ip_address) {
    FILE *log_file = fopen("server.log", "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return;
    }
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    fprintf(log_file, "%s - %s - %s - %s - %s\n", time_str, username, action, filename, ip_address);
    fclose(log_file);
}

void print_help() {
    printf("Usage:\n");
    printf("  cloudserver login --username <username> --password <password>\n");
    printf("  cloudserver upload --file <filename>\n");
    printf("  cloudserver download --file <filename>\n");
    printf("  cloudserver delete --file <filename>\n");
    printf("  cloudserver share --file <filename> --user <username>\n");
    printf("  cloudserver log --user <username>\n");
    printf("  cloudserver exit\n");
}

void *handle_client(void *client_socket) {
    int sock = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    write(sock, "Enter username password:", 25);
    read(sock, buffer, BUFFER_SIZE);
    if (!authenticate(buffer)) {
        write(sock, "AUTH_FAILED", 11);
        close(sock);
        pthread_exit(NULL);
    }
    write(sock, "AUTH_SUCCESS", 12);
    
    // MFA
    write(sock, "Enter TOTP code:", 16);
    read(sock, buffer, BUFFER_SIZE);
    int user_index = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (strcmp(buffer, users[i].username) == 0) {
            user_index = i;
            break;
        }
    }
    if (user_index == -1 || !verify_totp(buffer, users[user_index].secret)) {
        write(sock, "MFA_FAILED", 10);
        close(sock);
        pthread_exit(NULL);
    }
    write(sock, "MFA_SUCCESS", 11);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        read(sock, buffer, BUFFER_SIZE);
        if (strncmp(buffer, "UPLOAD", 6) == 0) {
            char filename[256];
            sscanf(buffer + 7, "%s", filename);
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
            encrypt_file(filename, users[user_index].secret);
            write(sock, "SUCCESS", 7);
            log_action(users[user_index].username, "UPLOAD", filename, inet_ntoa(((struct sockaddr_in *)&client_socket)->sin_addr));
        } else if (strncmp(buffer, "DOWNLOAD", 8) == 0) {
            char filename[256];
            sscanf(buffer + 9, "%s", filename);
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
            log_action(users[user_index].username, "DOWNLOAD", filename, inet_ntoa(((struct sockaddr_in *)&client_socket)->sin_addr));
        } else if (strncmp(buffer, "EXIT", 4) == 0) {
            break;
        }
    }
    close(sock);
    pthread_exit(NULL);
}

void test_authentication() {
    assert(authenticate("admin pass123") == 1);
    assert(authenticate("user 1234") == 1);
    assert(authenticate("admin wrongpass") == 0);
}

void test_file_operations() {
    // Test upload, download, and delete operations
    // ...
}

int main() {
    test_authentication();
    test_file_operations();
    setup_firewall();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);
    printf("Server listening on port %d\n", PORT);
    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) continue;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, &client_socket);
        pthread_detach(thread);
    }
    close(server_socket);
    return 0;
}
