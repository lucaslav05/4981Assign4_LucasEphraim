#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <ndbm.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>

#define MEDIA_FOLDER "../media/"

const char* get_mime_type(const char *file_ext);
void serve_file(int client_fd, const char *file_path);
void handle_request(int client_fd, DBM *db);

// Helper function to determine MIME type based on file extension.
const char* get_mime_type(const char *file_ext) {
    if(strcmp(file_ext, "html") == 0) {
        return "text/html";
    } else if(strcmp(file_ext, "txt") == 0) {
        return "text/plain";
    } else if(strcmp(file_ext, "png") == 0) {
        return "image/png";
    } else if(strcmp(file_ext, "jpg") == 0 || strcmp(file_ext, "jpeg") == 0) {
        return "image/jpeg";
    } else if(strcmp(file_ext, "css") == 0) {
        return "text/css";
    } else if(strcmp(file_ext, "gif") == 0) {
        return "image/gif";
    } else if(strcmp(file_ext, "js") == 0) {
        return "application/javascript";
    }
    return "application/octet-stream";
}

// Function to serve a file from the media folder
void serve_file(int client_fd, const char *file_path) {
    char full_path[1024];
    int file_fd;
    struct stat file_stat;
    char *mime_type_seperator;;
    const char *mime_type;
    char header[512];
    int header_len;
    char buffer[4096];
    ssize_t bytes_read;
    ssize_t header_bytes_sent;
    ssize_t content_bytes_sent;

    snprintf(full_path, sizeof(full_path), "%s%s", MEDIA_FOLDER, file_path);

    file_fd = open(full_path, O_RDONLY);
    if (file_fd == -1)
    {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
        write(client_fd, not_found, strlen(not_found));
        return;
    }

    //Checks validity of file descriptor and sends, the clinet, error code 500 if fd does not pass the check.
    if (fstat(file_fd, &file_stat) == -1)
    {
        const char *server_error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 25\r\n\r\n500 Internal Server Error";
        write(client_fd, server_error, strlen(server_error));
        close(file_fd);
        return;
    }

    // Extract file extension for MIME type determination.
    mime_type_seperator = strrchr(file_path, '.');

    mime_type = (mime_type_seperator) ? get_mime_type(++mime_type_seperator) : "application/octet-stream";

    // Build and send the HTTP header.
    header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              mime_type, (long)file_stat.st_size);

    // Send header to the client first
    header_bytes_sent = write(client_fd, header, (size_t)header_len);

    if (header_bytes_sent < 0)
    {
        perror("Header not sent to client");
        close(client_fd);
        close(file_fd);
    }

    // Stream the file contents.
    while (1) {
        bytes_read = read(file_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            if (errno == EINTR) {
                continue; // Try again
            } else {
                perror("read failed");
                break;
            }
        }
        if (bytes_read == 0) {
            break; // EOF
        }

        content_bytes_sent = write(client_fd, buffer, (size_t)bytes_read);
        if (content_bytes_sent != (size_t)bytes_read)
        {
            perror("Write failed");
            close(client_fd);
            break;
        }
    }

    close(file_fd);
}

// Function to handle HTTP requests
void handle_request(int client_fd, DBM *db)
{
    char buffer[4096];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0)
    {
        return;  // Error or empty request
    }
    buffer[bytes_read] = '\0';  // Null-terminate the buffer to make it a valid string

//    printf("test dynamic library\n");
    // Check if it is a GET request
    if (strncmp(buffer, "GET ", 4) == 0)
    {
        char *file_path;
        // Parse the GET request to extract the requested path.
        char method[8];
        char path[1024];
        char protocol[16];

        if (sscanf(buffer, "%7s %1023s %15s", method, path, protocol) != 3)
        {
            const char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\n\r\n400 Bad Request";
            write(client_fd, bad_request, strlen(bad_request));
            return;
        }
        // Skip the leading '/' if present.
        file_path = (path[0] == '/') ? path + 1 : path;

        if (strlen(file_path) == 0)
        {
            // Default file if none specified.
            file_path = "index.html";
        }

        printf("Worker Handling get request, worker id: %d\n", getpid());
        serve_file(client_fd, file_path);
    }
    else if (strncmp(buffer, "POST ", 5) == 0)
    {
        datum key;
        datum value;
        time_t now = time(NULL);
        char key_str[64];

        snprintf(key_str, sizeof(key_str), "post_%ld", now);

        key.dptr = key_str;
        key.dsize = (size_t)strlen(key_str);
        value.dptr = buffer;
        value.dsize = (size_t)bytes_read;

        if (dbm_store(db, key, value, DBM_INSERT) < 0)
        {

            // Fallback in case of key collision
            snprintf(key_str, sizeof(key_str), "post_%ld_%d", now, rand() % 10000);
            key.dptr = key_str;
            key.dsize = (size_t)strlen(key_str);
            if (dbm_store(db, key, value, DBM_INSERT) < 0)
            {
                write(client_fd, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
                return;
            }
        }

        printf("Worker Handling post request: %d\n", getpid());
        write(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\nPOST Received!", 52);
    }
    else
    {
        // Handle unsupported methods
        write(client_fd, "HTTP/1.1 405 Method Not Allowed\r\n\r\n", 36);
    }

    close(client_fd);
}
