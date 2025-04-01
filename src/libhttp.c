#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ndbm.h>

void handle_request(int client_fd, DBM *db);

// Function to handle HTTP requests
void handle_request(int client_fd, DBM *db) {
    char buffer[4096];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        return;  // Error or empty request
    }

    buffer[bytes_read] = '\0';  // Null-terminate the buffer to make it a valid string

    if (strncmp(buffer, "GET ", 4) == 0) {
        // Handle GET request (serve static files)
        const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
        write(client_fd, response, strlen(response));
    }
    else if (strncmp(buffer, "POST ", 5) == 0) {
        // Store POST data in ndbm
        char key_str[] = "post_data";
        datum key = { key_str, (int)strlen(key_str) };
        datum value = { buffer, (int)bytes_read };
        if (dbm_store(db, key, value, DBM_REPLACE) < 0) {
            write(client_fd, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
            return;
        }
        write(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\nPOST Received!", 52);
    }
    else {
        // Handle unsupported methods
        write(client_fd, "HTTP/1.1 405 Method Not Allowed\r\n\r\n", 36);
    }
}
