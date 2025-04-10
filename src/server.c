#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ndbm.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8123
#define MAX_WORKERS 3
#define LIBRARY_PATH "../src/libhttp.so"
#define DBM_MODE 0666
#define SELECT_CHECK 100000

#ifdef __APPLE__
    #define SOCK_CLOEXEC 0
#endif

static volatile sig_atomic_t keep_looping = 1;    // NOLINT

typedef void (*handle_request_t)(int client_fd, DBM *db);

static handle_request_t load_shared_library(void);
static void             worker_process(int server_fd, handle_request_t handle_request) __attribute__((noreturn));
static void             monitor_process(void) __attribute__((noreturn));
void                    handle_shutdown(int sig);

void handle_shutdown(int sig)
{
    (void)sig;
    keep_looping = 0;
}

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

    //    printf("Loaded shared library: %s\n", LIBRARY_PATH);
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

    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    if(!db)
    {
        perror("Failed to open ndbm database");
        exit(EXIT_FAILURE);
    }
    while(keep_looping)
    {
        struct timeval timeout;
        int            ready;
        int            client_fd;
        fd_set         read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        timeout.tv_sec  = 0;
        timeout.tv_usec = SELECT_CHECK;

        if(!keep_looping)
        {
            break;
        }

        ready = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        if(ready < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            if(!keep_looping || errno == EBADF)
            {
                break;
            }
            perror("select failed");
            {
                continue;
            }
        }

        if(ready == 0)
        {
            continue;    // timeout — just loop back and check keep_looping
        }
        // ready > 0 and server_fd is ready to accept
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if(!keep_looping)
        {
            close(client_fd);
            break;
        }

        if(client_fd < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;    // Normal in non-blocking mode
            }
            if(errno == EINTR)
            {
                continue;
            }
            if(!keep_looping || errno == EBADF || errno == ENOTSOCK)
            {
                break;
            }
            perror("Accept failed");
            {
                continue;
            }
        }

        // Reload and handle
        handle_request = load_shared_library();
        handle_request(client_fd, db);
        //        close(client_fd);
    }

    dbm_close(db);
    close(server_fd);
    exit(0);
}

// Monitor process to reap zombie processes
void monitor_process(void)
{
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    while(keep_looping)
    {
        while(waitpid(-1, NULL, WNOHANG) > 0)
        {
        }
        sleep(1);
    }

    // Reap any leftovers before exiting
    while(waitpid(-1, NULL, WNOHANG) > 0)
    {
    }

    exit(0);
}

// Main function
int main(void)
{
    handle_request_t   handle_request;
    pid_t              monitor_pid;
    int                server_fd;
    int                opt = 1;
    struct sockaddr_in server_addr;
    pid_t              worker_id_array[MAX_WORKERS];
    int                flags;

    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    // Make the server_fd non-blocking
    flags = fcntl(server_fd, F_GETFL, 0);
    if(flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("Failed to make server socket non-blocking");
        exit(EXIT_FAILURE);
    }

    //    server_fd = socket(AF_INET, SOCK_STREAM, 0);
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

    // Load shared library once before forking
    handle_request = load_shared_library();

    // Pre-fork worker processes
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        pid_t pid = fork();
        if(pid == 0)
        {    // Worker process
            printf("Worker Process forked, worker id: %d\n", getpid());
            worker_process(server_fd, handle_request);
        }
        worker_id_array[i] = pid;
    }

    // Fork monitor process
    monitor_pid = fork();
    if(monitor_pid == 0)
    {
        monitor_process();
    }

    // Main server process just waits (monitor handles cleanup)
    while(keep_looping)
    {
        sleep(1);
    }

    printf("Server exiting...\n");

    // 1. Kill monitor
    kill(monitor_pid, SIGTERM);

    // 2. Kill and reap workers
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        kill(worker_id_array[i], SIGTERM);
    }
    for(int i = 0; i < MAX_WORKERS; i++)
    {
        waitpid(worker_id_array[i], NULL, 0);
        printf("Reaped worker %d\n", worker_id_array[i]);
    }

    // 3. Reap monitor last
    waitpid(monitor_pid, NULL, 0);
    printf("Reaped monitor %d\n", monitor_pid);
}
