#ifndef CLOUDSERVER_H
#define CLOUDSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_USERS 5
#define AES_BLOCK_SIZE 16

typedef struct {
    char username[50];
    char password[50];
    char secret[20]; // For TOTP
} User;

extern User users[MAX_USERS];

int authenticate(char *credentials);
char *
