#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8080
#define MAX_WORKERS 3
#define LIBRARY_PATH "../src/libhttp.so"
#define DBM_MODE 0666

typedef void (*handle_request_t)(int client_fd, DBM *db);

static handle_request_t load_shared_library(void);
static void             worker_process(int server_fd, handle_request_t handle_request) __attribute__((noreturn));
static void             monitor_process(void) __attribute__((noreturn));

handle_request_t load_shared_library(void)
{
    handle_request_t handle_request;
    static void     *handle = NULL;

    if(handle)
    {
        dlclose(handle);
    }

    handle = dlopen(LIBRARY_PATH, RTLD_LAZY);
    if(!handle)
    {
        fprintf(stderr, "Error loading shared library: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    handle_request = (handle_request_t)dlsym(handle, "handle_request");
    if(!handle_request)
    {
        fprintf(stderr, "Error loading function: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    printf("Loaded shared library: %s\n", LIBRARY_PATH);
    return handle_request;
}

// Worker process function
void worker_process(int server_fd, handle_request_t handle_request)
{
    struct sockaddr_in client_addr;
    socklen_t          client_len = sizeof(client_addr);
    char               key_str[]  = "post_data";
    DBM               *db         = dbm_open(key_str, O_RDWR | O_CREAT, DBM_MODE);
    key_str[1]                    = 'h';

    if(!db)
    {
        perror("Failed to open ndbm database");
        exit(EXIT_FAILURE);
    }

    while(1)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if(client_fd < 0)
        {
            perror("Accept failed");
            continue;
        }

        // Reload shared library if updated
        handle_request = load_shared_library();

        // Process HTTP request
        handle_request(client_fd, db);

        close(client_fd);
    }
}

// Monitor process to reap zombie processes
void monitor_process(void)
{
    while(1)
    {
        while(waitpid(-1, NULL, WNOHANG) > 0)
        {
        }
        sleep(1);
    }
}

// Main function
int main(void)
{
    handle_request_t   handle_request;
    pid_t              monitor_pid;
    int                server_fd;
    int                opt = 1;
    struct sockaddr_in server_addr;

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }

#ifdef SO_REUSEPORT
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEPORT) failed");
        exit(EXIT_FAILURE);
    }
#endif

    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if(listen(server_fd, MAX_WORKERS) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Fork monitor process
    monitor_pid = fork();
    if(monitor_pid == 0)
    {
        monitor_process();
    }

    // Load shared library once before forking
    handle_request = load_shared_library();

    // Pre-fork worker processes
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        pid_t pid = fork();
        if(pid == 0)
        {    // Worker process
            printf("Worker Process forked: %d\n", getpid());
            worker_process(server_fd, handle_request);
        }
    }

    // Main server process just waits (monitor handles cleanup)
    while(1)
    {
        sleep(1);
    }
}
