#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 5000
#define DEBUG_FLAG "--debug"

/* Debug settings */
BOOL debug_enabled = FALSE;

#define DEBUG_PRINT(fmt, ...) \
    if (debug_enabled) { \
        printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
    }

/* Define socket base */
struct Library *SocketBase = NULL;
extern struct ExecBase *SysBase;

void show_usage(const char* program) {
    printf("Usage: %s [host] [port] [options]\n", program);
    printf("Options:\n");
    printf("  --debug    Enable debug output\n");
}

/* Initialize socket library */
BOOL init_socket_library(void) {
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        printf("Failed to open bsdsocket.library v4\n");
        return FALSE;
    }
    return TRUE;
}

/* Cleanup resources */
void cleanup(void) {
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

/* Parse message from server format: MSG:message text\n */
void parse_message(const char *msg, char *output, size_t output_size) {
    DEBUG_PRINT("Parsing message: '%s'", msg);
    
    if (strncmp(msg, "MSG:", 4) == 0) {
        // Skip the MSG: prefix
        strncpy(output, msg + 4, output_size - 1);
        output[output_size - 1] = '\0';
        DEBUG_PRINT("Extracted message: '%s'", output);
    } else {
        // If no prefix found, copy as-is
        strncpy(output, msg, output_size - 1);
        output[output_size - 1] = '\0';
        DEBUG_PRINT("No prefix found, using full message: '%s'", output);
    }
}

/* Main program */
int main(int argc, char *argv[]) {
    LONG sock = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];
    char input[BUFFER_SIZE];
    fd_set readfds;
    struct timeval tv;
    LONG result;
    const char *host = "localhost";
    UWORD port = DEFAULT_PORT;
    int arg_index;

    /* Parse command line arguments */
    for (arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], DEBUG_FLAG) == 0) {
            debug_enabled = TRUE;
        } else if (arg_index == 1 && argv[arg_index][0] != '-') {
            host = argv[arg_index];
        } else if (arg_index == 2 && argv[arg_index][0] != '-') {
            port = atoi(argv[arg_index]);
        } else {
            show_usage(argv[0]);
            return 20;
        }
    }

    DEBUG_PRINT("Starting client with host=%s, port=%d", host, port);

    if (!init_socket_library()) {
        return 20;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket, errno=%d\n", errno);
        cleanup();
        return 20;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    struct hostent *he = gethostbyname((char *)host);
    if (!he) {
        printf("Failed to resolve hostname, errno=%d\n", errno);
        CloseSocket(sock);
        cleanup();
        return 20;
    }
    
    memcpy(&server_addr.sin_addr, he->h_addr, he->h_length);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to connect, errno=%d\n", errno);
        CloseSocket(sock);
        cleanup();
        return 20;
    }

    printf("Connected to server! Type /help for commands\n");

    /* Set non-blocking mode */
    LONG yes = 1;
    if (IoctlSocket(sock, FIONBIO, (char *)&yes) < 0) {
        printf("Failed to set non-blocking mode, errno=%d\n", errno);
        CloseSocket(sock);
        cleanup();
        return 20;
    }

    BOOL running = TRUE;
    while (running) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET((LONG)Input(), &readfds);

        tv.tv_sec = 0;
        tv.tv_usec = 100000;  /* 100ms */

        result = WaitSelect(FD_SETSIZE, &readfds, NULL, NULL, &tv, NULL);
        if (result > 0) {
            if (FD_ISSET(sock, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE);
                LONG bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes <= 0) {
                    if (bytes < 0 && errno == EWOULDBLOCK) {
                        continue;
                    }
                    printf("\nDisconnected from server\n");
                    running = FALSE;
                    break;
                }
                
                buffer[bytes] = '\0';  // Ensure null termination
                DEBUG_PRINT("Received raw data (%ld bytes): %s", bytes, buffer);
                
                // Split multiple messages if any
                char *start = buffer;
                char *end;
                
                while ((end = strchr(start, '\n')) != NULL) {
                    *end = '\0';  // Temporarily null terminate this message
                    parse_message(start, message, BUFFER_SIZE);
                    printf("%s\n", message);
                    start = end + 1;  // Move to start of next message
                }
                
                // Process any remaining data
                if (*start) {
                    parse_message(start, message, BUFFER_SIZE);
                    printf("%s\n", message);
                }
                
                printf(">> ");
                fflush(stdout);
            }

            if (FD_ISSET((LONG)Input(), &readfds)) {
                if (fgets(input, BUFFER_SIZE - 1, stdin) != NULL) {
                    input[strcspn(input, "\n")] = 0;
                    
                    if (strcmp(input, "/quit") == 0) {
                        char cmd[BUFFER_SIZE];
                        snprintf(cmd, sizeof(cmd), "%s\n", input);
                        send(sock, cmd, strlen(cmd), 0);
                        running = FALSE;
                        break;
                    }
                    
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "%s\n", input);
                    if (send(sock, msg, strlen(msg), 0) < 0) {
                        printf("Failed to send message, errno=%d\n", errno);
                        running = FALSE;
                        break;
                    }
                }
            }
        }
        if (result < 0 && errno != EINTR) {
            printf("Select error, errno=%d\n", errno);
            running = FALSE;
            break;
        }
    }

    CloseSocket(sock);
    cleanup();
    return 0;
}