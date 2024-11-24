#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define PORT 8080
#define BUF_SIZE 1024
#define MAX_NAME_SIZE 256       // Max size for username
#define MAX_FILE_NAME 512       // Max size for file basename
#define MAX_PATH_SIZE 1024      // Max size for full path

void *handle_client(void *socket_desc);
void ensure_user_directory(const char *username);

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    pthread_t thread_id;

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d...\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Client accept failed");
            continue;
        }

        printf("Client connected\n");
        if (pthread_create(&thread_id, NULL, handle_client, (void *)&client_fd) < 0) {
            perror("Thread creation failed");
            close(client_fd);
            continue;
        }
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}

void *handle_client(void *socket_desc) {
    int client_fd = *(int *)socket_desc;
    char buffer[BUF_SIZE];
    char username[MAX_NAME_SIZE];
    char command[BUF_SIZE];

    // Receive username
    if (recv(client_fd, username, sizeof(username), 0) <= 0) {
        fprintf(stderr, "Error receiving username.\n");
        close(client_fd);
        pthread_exit(NULL);
    }

    // Ensure username does not exceed the maximum allowed size
    if (strlen(username) >= MAX_NAME_SIZE - 1) {
        fprintf(stderr, "Username exceeds maximum allowed size.\n");
        close(client_fd);
        pthread_exit(NULL);
    }
    ensure_user_directory(username);

    while (recv(client_fd, buffer, sizeof(buffer), 0) > 0) {
        sscanf(buffer, "%s", command);

        if (strcmp(command, "$UPLOAD$") == 0) {
            // Handle file upload
            char filepath[MAX_PATH_SIZE];
            char file_basename[MAX_FILE_NAME];

            if (sscanf(buffer + strlen("$UPLOAD$ "), "%s", filepath) != 1) {
                fprintf(stderr, "Error parsing upload filepath.\n");
                continue;
            }

            // Extract the file basename
            const char *basename = strrchr(filepath, '/');
            if (basename) {
                strncpy(file_basename, basename + 1, MAX_FILE_NAME - 1);
                file_basename[MAX_FILE_NAME - 1] = '\0';
            } else {
                strncpy(file_basename, filepath, MAX_FILE_NAME - 1);
                file_basename[MAX_FILE_NAME - 1] = '\0';
            }

            // Ensure basename fits within limits
            if (strlen(file_basename) == 0 || strlen(file_basename) >= MAX_FILE_NAME) {
                fprintf(stderr, "Invalid or too long filename.\n");
                continue;
            }

            // Construct the user directory path
            char user_dir[MAX_PATH_SIZE];
            if (snprintf(user_dir, sizeof(user_dir), "./%s/%s", username, file_basename) >= sizeof(user_dir)) {
                fprintf(stderr, "File path too long.\n");
                continue;
            }

            FILE *file = fopen(user_dir, "wb");
            if (file) {
                int bytes;
                while ((bytes = recv(client_fd, buffer, BUF_SIZE, 0)) > 0) {
                    char *end_marker = strstr(buffer, "$END$");
                    if (end_marker) {
                        fwrite(buffer, 1, end_marker - buffer, file);
                        break;
                    }
                    fwrite(buffer, 1, bytes, file);
                }
                fclose(file);
                printf("File uploaded to %s\n", user_dir);
                send(client_fd, "File uploaded successfully\n", strlen("File uploaded successfully\n"), 0);
            } else {
                perror("File creation failed");
            }

        } else if (strcmp(command, "$DOWNLOAD$") == 0) {
            // Handle file download
            char filename[MAX_FILE_NAME];
            if (sscanf(buffer + strlen("$DOWNLOAD$ "), "%s", filename) != 1) {
                fprintf(stderr, "Error parsing filename for download.\n");
                continue;
            }

            // Ensure filename is valid
            if (strlen(filename) >= MAX_FILE_NAME) {
                fprintf(stderr, "Filename too long.\n");
                continue;
            }

            // Construct the user file path
            char user_file[MAX_PATH_SIZE];
            if (snprintf(user_file, sizeof(user_file), "./%s/%s", username, filename) >= sizeof(user_file)) {
                fprintf(stderr, "File path too long.\n");
                continue;
            }

            FILE *file = fopen(user_file, "rb");
            if (file) {
                while (!feof(file)) {
                    int bytes = fread(buffer, 1, BUF_SIZE, file);
                    send(client_fd, buffer, bytes, 0);
                }
                send(client_fd, "$END$", strlen("$END$"), 0);
                fclose(file);
                printf("File sent: %s\n", filename);
            } else {
                perror("File not found or inaccessible");
            }

        } else if (strcmp(command, "$VIEW$") == 0) {
            // Handle the view command
            char user_dir[MAX_PATH_SIZE];
            snprintf(user_dir, sizeof(user_dir), "./%s", username);

            DIR *dir = opendir(user_dir);
            if (!dir) {
                perror("Failed to open user directory");
                send(client_fd, "Error reading directory\n", strlen("Error reading directory\n"), 0);
                send(client_fd, "$END$", strlen("$END$"), 0);
                continue;
            }

            struct dirent *entry;
            int found = 0;

            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    send(client_fd, entry->d_name, strlen(entry->d_name), 0);
                    send(client_fd, "\n", 1, 0);
                    found = 1;
                }
            }

            closedir(dir);

            if (!found) {
                send(client_fd, "No files found\n", strlen("No files found\n"), 0);
            }

            send(client_fd, "$END$", strlen("$END$"), 0);
        }
    }

    close(client_fd);
    printf("Client disconnected\n");
    return NULL;
}
void ensure_user_directory(const char *username) {
    char user_dir[MAX_PATH_SIZE];

    // Sanitize username
    if (username == NULL || strlen(username) == 0) {
        fprintf(stderr, "Invalid username\n");
        return;
    }

    // Construct the directory path
    if (snprintf(user_dir, sizeof(user_dir), "./%s", username) >= sizeof(user_dir)) {
        fprintf(stderr, "Directory path too long for username: %s\n", username);
        return;
    }

    // Debug output
    printf("Creating directory: %s\n", user_dir);

    // Create the directory
    if (mkdir(user_dir, 0777) < 0) {
        if (errno == EEXIST) {
            printf("Directory already exists: %s\n", user_dir);
        } else {
            perror("mkdir failed");
        }
    } else {
        printf("Directory created: %s\n", user_dir);
    }
}

