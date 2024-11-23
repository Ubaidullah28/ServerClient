#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h> // For struct stat and file attributes

#define PORT 8080
#define BUF_SIZE 4096
#define FILE_DIR "server_files"

void handle_client(int client_sock);
void send_file(int client_sock, const char *filepath);
void receive_file(int client_sock, const char *destination);
void list_files(int client_sock);

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", PORT);

    // Ensure file directory exists
    mkdir(FILE_DIR, 0755);

    while (1) {
        addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Connection failed");
            continue;
        }

        printf("Client connected\n");
        if (fork() == 0) {
            // Child process to handle client
            close(server_sock);
            handle_client(client_sock);
            close(client_sock);
            exit(0);
        }
        close(client_sock);
    }

    close(server_sock);
    return 0;
}

void handle_client(int client_sock) {
    char buffer[BUF_SIZE];
    char username[100];
    char *filepath = malloc(BUF_SIZE); // Dynamically allocate memory

    if (!filepath) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    // Receive username
    recv(client_sock, username, sizeof(username), 0);
    printf("Username: %s\n", username);

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes = recv(client_sock, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        buffer[bytes] = '\0';
        printf("Received command: %s\n", buffer);

        if (strncmp(buffer, "$UPLOAD$", strlen("$UPLOAD$")) == 0) {
            sscanf(buffer + strlen("$UPLOAD$ "), "%s", filepath);

            // Ensure memory safety with snprintf
            if (snprintf(filepath, BUF_SIZE, "%s/%s", FILE_DIR, filepath) >= BUF_SIZE) {
                fprintf(stderr, "Error: File path exceeds buffer size.\n");
                continue;
            }

            printf("Receiving file: %s\n", filepath);
            receive_file(client_sock, filepath);
        } else if (strncmp(buffer, "$DOWNLOAD$", strlen("$DOWNLOAD$")) == 0) {
            sscanf(buffer + strlen("$DOWNLOAD$ "), "%s", filepath);

            // Ensure memory safety with snprintf
            if (snprintf(filepath, BUF_SIZE, "%s/%s", FILE_DIR, filepath) >= BUF_SIZE) {
                fprintf(stderr, "Error: File path exceeds buffer size.\n");
                continue;
            }

            printf("Sending file: %s\n", filepath);
            send_file(client_sock, filepath);
        } else if (strcmp(buffer, "$VIEW$") == 0) {
            list_files(client_sock);
        }
    }

    free(filepath); // Free allocated memory
    printf("Client disconnected\n");
}



void send_file(int client_sock, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    char buffer[BUF_SIZE];

    if (!file) {
        perror("File not found");
        send(client_sock, "$ERROR$", strlen("$ERROR$"), 0);
        return;
    }

    while (!feof(file)) {
        int bytes = fread(buffer, 1, BUF_SIZE, file);
        send(client_sock, buffer, bytes, 0);
    }
    send(client_sock, "$END$", strlen("$END$"), 0);
    fclose(file);
    printf("File sent: %s\n", filepath);
}

void receive_file(int client_sock, const char *destination) {
    FILE *file = fopen(destination, "wb");
    char buffer[BUF_SIZE];

    if (!file) {
        perror("Failed to open file");
        return;
    }

    int bytes;
    while ((bytes = recv(client_sock, buffer, BUF_SIZE, 0)) > 0) {
        if (strstr(buffer, "$END$")) {
            fwrite(buffer, 1, bytes - strlen("$END$"), file);
            break;
        }
        fwrite(buffer, 1, bytes, file);
    }
    fclose(file);
    printf("File received: %s\n", destination);
}

void list_files(int client_sock) {
    DIR *dir;
    struct dirent *entry;
    char buffer[BUF_SIZE];
    char full_path[BUF_SIZE];
    struct stat file_stat;

    dir = opendir(FILE_DIR);
    if (!dir) {
        perror("Failed to open directory");
        send(client_sock, "$ERROR$", strlen("$ERROR$"), 0);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        // Construct the full file path
        snprintf(full_path, BUF_SIZE, "%s/%s", FILE_DIR, entry->d_name);

        // Use stat to check if it's a regular file
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            snprintf(buffer, BUF_SIZE, "%s\n", entry->d_name);
            send(client_sock, buffer, strlen(buffer), 0);
        }
    }

    closedir(dir);
    send(client_sock, "$END$", strlen("$END$"), 0);
    printf("File list sent\n");
}
