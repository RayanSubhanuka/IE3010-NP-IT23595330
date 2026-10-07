/*
 * NetMessenger Client - IE3010 Network Programming
 * Reg No: IT23595330 | Port: 11330 | Node ID: NID:5953
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define DEFAULT_PORT 11330
#define DEFAULT_IP   "127.0.0.1"
#define BUF_SIZE     4096

static int sock_fd = -1;
static volatile int running = 1;

/* Background receiver thread: handles server messages and file downloads */
void* recv_thread(void *arg) {
    (void)arg;

    char rx[BUF_SIZE];
    int rx_len = 0;

    mkdir("./downloads", 0755);

    while (running) {
        ssize_t n = recv(
            sock_fd,
            rx + rx_len,
            sizeof(rx) - rx_len - 1,
            0
        );

        if (n <= 0) {
            if (running)
                printf("\n[Disconnected from server]\n");

            running = 0;
            break;
        }

        rx_len += (int)n;
        rx[rx_len] = '\0';

        while (1) {
            char *nl = strchr(rx, '\n');

            if (!nl)
                break;

            *nl = '\0';

            char line[BUF_SIZE];

            snprintf(line, sizeof(line), "%s", rx);

            if (strlen(line) > 0 &&
                line[strlen(line) - 1] == '\r') {
                line[strlen(line) - 1] = '\0';
            }

            int consumed = (int)(nl - rx) + 1;

            memmove(rx, rx + consumed, rx_len - consumed);
            rx_len -= consumed;
            rx[rx_len] = '\0';

            /* Check for incoming file transfer */
            if (strncmp(line, "MSG FILE ", 9) == 0) {
                char sender[32] = {0};
                char fname[128] = {0};
                char path[256];

                long long fsize = 0;

                if (sscanf(
                        line + 9,
                        "%31s %127s %lld",
                        sender,
                        fname,
                        &fsize
                    ) != 3 || fsize < 0) {
                    printf("\n[ERROR] Invalid file header\n");
                    continue;
                }

                printf(
                    "\n[INCOMING FILE] '%s' (%lld bytes) from '%s'\n",
                    fname,
                    fsize,
                    sender
                );

                /* Avoid writing received files outside downloads */
                if (strchr(fname, '/') ||
                    strchr(fname, '\\') ||
                    strcmp(fname, ".") == 0 ||
                    strcmp(fname, "..") == 0) {
                    printf("[ERROR] Invalid filename\n");
                    running = 0;
                    shutdown(sock_fd, SHUT_RDWR);
                    break;
                }

                snprintf(
                    path,
                    sizeof(path),
                    "./downloads/%s",
                    fname
                );

                FILE *fp = fopen(path, "wb");

                if (!fp)
                    perror("[ERROR] Cannot create download file");

                long long left = fsize;
                int buffered = rx_len;

                if (buffered > 0 && left > 0) {
                    int w = (buffered > left)
                          ? (int)left
                          : buffered;

                    if (fp)
                        fwrite(rx, 1, w, fp);

                    left -= w;

                    int rem = buffered - w;

                    if (rem > 0)
                        memmove(rx, rx + w, rem);

                    rx_len = rem;
                    rx[rx_len] = '\0';
                }

                char chunk[BUF_SIZE];
                int transfer_ok = 1;

                while (left > 0) {
                    int r = (left > (long long)sizeof(chunk))
                          ? (int)sizeof(chunk)
                          : (int)left;

                    ssize_t in = recv(sock_fd, chunk, r, 0);

                    if (in <= 0) {
                        transfer_ok = 0;
                        break;
                    }

                    if (fp &&
                        fwrite(chunk, 1, (size_t)in, fp) != (size_t)in) {
                        transfer_ok = 0;
                    }

                    left -= in;
                }

                if (fp)
                    fclose(fp);

                if (transfer_ok && fp) {
                    printf("[SUCCESS] File saved to %s\n", path);
                } else {
                    printf("[ERROR] File transfer was incomplete\n");
                    if (fp)
                        remove(path);
                }

                printf("> ");
                fflush(stdout);
            } else {
                printf("%s\n> ", line);
                fflush(stdout);
            }
        }

        if (rx_len >= (int)sizeof(rx) - 1) {
            printf("\n[ERROR] Incoming message is too long\n");
            running = 0;
            shutdown(sock_fd, SHUT_RDWR);
            break;
        }
    }

    return NULL;
}

/* Send all bytes, handling partial TCP writes */
int send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

/* Upload local file using SENDFILE */
void send_file(const char *target, const char *path) {
    FILE *fp = fopen(path, "rb");

    if (!fp) {
        printf("[ERROR] Cannot open file '%s'\n", path);
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        printf("[ERROR] Cannot determine file size\n");
        fclose(fp);
        return;
    }

    long long fsize = (long long)ftell(fp);

    if (fsize < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        printf("[ERROR] Cannot read file size\n");
        fclose(fp);
        return;
    }

    const char *fname = strrchr(path, '/');

    if (!fname)
        fname = strrchr(path, '\\');

    fname = fname ? fname + 1 : path;

    if (strlen(fname) == 0 ||
        strchr(fname, ' ') ||
        strchr(fname, '\n') ||
        strchr(fname, '\r')) {
        printf("[ERROR] Filename must not contain spaces or line breaks\n");
        fclose(fp);
        return;
    }

    char hdr[512];

    int hdr_len = snprintf(
        hdr,
        sizeof(hdr),
        "SENDFILE %s %s %lld\n",
        target,
        fname,
        fsize
    );

    if (hdr_len < 0 || (size_t)hdr_len >= sizeof(hdr)) {
        printf("[ERROR] File header is too long\n");
        fclose(fp);
        return;
    }

    /*
     * The protocol requires the header followed by exactly
     * fsize raw file bytes, without an extra newline.
     */
    if (send_all(sock_fd, hdr, (size_t)hdr_len) < 0) {
        printf("[ERROR] Failed to send file header\n");
        fclose(fp);
        return;
    }

    char chunk[BUF_SIZE];
    size_t n;
    long long remaining = fsize;

    while (remaining > 0 &&
           (n = fread(
                chunk,
                1,
                remaining < (long long)sizeof(chunk)
                    ? (size_t)remaining
                    : sizeof(chunk),
                fp
            )) > 0) {

        if (send_all(sock_fd, chunk, n) < 0) {
            printf("[ERROR] Failed while sending file data\n");
            fclose(fp);
            return;
        }

        remaining -= (long long)n;
    }

    if (remaining != 0) {
        printf("[ERROR] Could not read the complete file\n");
        fclose(fp);
        return;
    }

    fclose(fp);

    printf(
        "[SENDFILE] Sent '%s' (%lld bytes) to '%s'\n",
        fname,
        fsize,
        target
    );
}

int main(int argc, char *argv[]) {
    const char *ip = (argc > 1) ? argv[1] : DEFAULT_IP;
    int port = (argc > 2) ? atoi(argv[2]) : DEFAULT_PORT;

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0) {
        perror("socket error");
        return 1;
    }

    /* Initialize server address */
    struct sockaddr_in serv;

    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);

    /* Resolve hostname or IP address */
    struct hostent *he = gethostbyname(ip);

    if (he != NULL && he->h_addr_list[0] != NULL) {
        memcpy(
            &serv.sin_addr,
            he->h_addr_list[0],
            (size_t)he->h_length
        );
    } else if (inet_pton(AF_INET, ip, &serv.sin_addr) != 1) {
        fprintf(stderr, "Invalid server address: %s\n", ip);
        close(sock_fd);
        return 1;
    }

    if (connect(
            sock_fd,
            (struct sockaddr *)&serv,
            sizeof(serv)
        ) < 0) {
        perror("connect failed");
        close(sock_fd);
        return 1;
    }

    /* Display local and remote socket information */
    struct sockaddr_in local, peer;

    socklen_t local_len = sizeof(local);
    socklen_t peer_len = sizeof(peer);

    if (getsockname(
            sock_fd,
            (struct sockaddr *)&local,
            &local_len
        ) == 0) {
        printf(
            "[getsockname] Local client port: %d\n",
            ntohs(local.sin_port)
        );
    }

    if (getpeername(
            sock_fd,
            (struct sockaddr *)&peer,
            &peer_len
        ) == 0) {
        printf(
            "[getpeername] Connected to server at %s:%d\n",
            inet_ntoa(peer.sin_addr),
            ntohs(peer.sin_port)
        );
    }

    printf(
        "NetMessenger Client (IT23595330)\n"
        "Server: %s:%d | Node ID: NID:5953\n",
        ip,
        port
    );

    printf(
        "Type commands (e.g. REGISTER <name>, "
        "BCAST <msg>, QUIT):\n\n"
    );

    /* Start background receiver thread */
    pthread_t tid;

    if (pthread_create(&tid, NULL, recv_thread, NULL) != 0) {
        perror("pthread_create");
        close(sock_fd);
        return 1;
    }

    pthread_detach(tid);

    char line[1024];

    while (running) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\r\n")] = '\0';

        if (strlen(line) == 0)
            continue;

        /* SENDFILE <target> <filepath> */
        if (strncasecmp(line, "SENDFILE ", 9) == 0) {
            char target[64] = {0};
            char fpath[256] = {0};

            if (sscanf(line + 9, "%63s %255s", target, fpath) >= 2) {
                send_file(target, fpath);
                continue;
            }

            printf("Usage: SENDFILE <target> <filepath>\n");
            continue;
        }

        /* Send command to server */
        char out[1050];

        int out_len = snprintf(
            out,
            sizeof(out),
            "%s\n",
            line
        );

        if (out_len < 0 || (size_t)out_len >= sizeof(out)) {
            printf("[ERROR] Command is too long\n");
            continue;
        }

        if (send_all(sock_fd, out, (size_t)out_len) < 0) {
            printf("[ERROR] Failed to send command\n");
            break;
        }

        if (strcasecmp(line, "QUIT") == 0) {
            break;
        }

        /* Brief pause to avoid unnecessary busy output */
        struct timespec delay = {0, 50000000L};
        nanosleep(&delay, NULL);
    }

    running = 0;

    /* Shutdown before close */
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    sock_fd = -1;

    return 0;
}