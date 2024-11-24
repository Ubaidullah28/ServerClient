#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

void send_file(int sock, const char *filepath);
void receive_file(int sock, const char *destination);

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];
    char command[BUF_SIZE];
    char username[100];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    printf("Enter username: ");
    scanf("%s", username);
    send(sock, username, strlen(username), 0);

    FILE *cmd_file = fopen("Commands.txt", "r");
    if (!cmd_file) {
        perror("Failed to open Commands.txt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    while (fgets(buffer, BUF_SIZE, cmd_file)) {
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline
        send(sock, buffer, strlen(buffer), 0);

        if (strncmp(buffer, "$UPLOAD$", strlen("$UPLOAD$")) == 0) {
            char filepath[BUF_SIZE];
            sscanf(buffer + strlen("$UPLOAD$ "), "%s", filepath);
            send_file(sock, filepath);
        } else if (strncmp(buffer, "$DOWNLOAD$", strlen("$DOWNLOAD$")) == 0) {
            char filename[BUF_SIZE], destination[BUF_SIZE];
            sscanf(buffer + strlen("$DOWNLOAD$ "), "%s", filename);
            printf("Enter download location: ");
            scanf("%s", destination);
            receive_file(sock, destination);
        } else if (strcmp(buffer, "$VIEW$") == 0) {
            printf("Files:\n");
            while (recv(sock, buffer, sizeof(buffer), 0) > 0) {
                if (strstr(buffer, "$END$")) break;
                printf("%s", buffer);
            }
        }
    }

    fclose(cmd_file);
    close(sock);
    return 0;
}

void send_file(int sock, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    char buffer[BUF_SIZE];

    if (!file) {
        perror("File not found");
        return;
    }

    while (!feof(file)) {
        int bytes = fread(buffer, 1, BUF_SIZE, file);
        send(sock, buffer, bytes, 0);
    }
    send(sock, "$END$", strlen("$END$"), 0);
    fclose(file);
}

void receive_file(int sock, const char *destination) {
    FILE *file = fopen(destination, "wb");
    char buffer[BUF_SIZE];

    if (!file) {
        perror("Failed to open file");
        return;
    }

    int bytes;
    while ((bytes = recv(sock, buffer, BUF_SIZE, 0)) > 0) {
        if (strstr(buffer, "$END$")) {
            fwrite(buffer, 1, bytes - strlen("$END$"), file);
            break;
        }
        fwrite(buffer, 1, bytes, file);
    }
    fclose(file);
}