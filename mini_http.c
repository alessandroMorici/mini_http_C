// mini_http.c - A tiny single-threaded HTTP server in C
// Compile: gcc -O2 -Wall -o mini_http mini_http.c
// Run: sudo ./mini_http 8080
// Then open http://localhost:8080/ in your browser

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 10
#define BUF_SIZE 8192

// Simple MIME mapping for common types
const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (strcmp(ext, "html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, "htm")  == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, "css")  == 0) return "text/css";
    if (strcmp(ext, "js")   == 0) return "application/javascript";
    if (strcmp(ext, "png")  == 0) return "image/png";
    if (strcmp(ext, "jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, "gif")  == 0) return "image/gif";
    if (strcmp(ext, "txt")  == 0) return "text/plain; charset=utf-8";
    if (strcmp(ext, "json") == 0) return "application/json";
    if (strcmp(ext, "svg")  == 0) return "image/svg+xml";
    return "application/octet-stream";
}

// URL-decode simple (in-place)
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            a = (char)( (a >= 'a') ? (a - 'a' + 10) : ((a >= 'A') ? (a - 'A' + 10) : (a - '0')) );
            b = (char)( (b >= 'a') ? (b - 'a' + 10) : ((b >= 'A') ? (b - 'A' + 10) : (b - '0')) );
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Prevent path traversal: collapse ".." and ensure path begins with '/'
int safe_path(char *out, const char *in) {
    // decode first
    char dec[1024];
    url_decode(dec, in);
    // if empty or root, serve index
    if (strcmp(dec, "/") == 0 || strlen(dec) == 0) {
        strcpy(out, "./index.html");
        return 0;
    }
    // must start with '/'
    if (dec[0] != '/') dec[0] = '/';
    // collapse paths naively:
    char tmp[1024];
    char *parts[256];
    int np = 0;
    char *tok = strtok(dec, "/");
    while (tok && np < 255) {
        if (strcmp(tok, "") == 0 || strcmp(tok, ".") == 0) {
            // skip
        } else if (strcmp(tok, "..") == 0) {
            if (np > 0) np--;
        } else {
            parts[np++] = tok;
        }
        tok = strtok(NULL, "/");
    }
    strcpy(tmp, "./");
    for (int i = 0; i < np; ++i) {
        strcat(tmp, parts[i]);
        if (i < np-1) strcat(tmp, "/");
    }
    if (np == 0) strcat(tmp, "index.html");
    // final safety check length
    if (strlen(tmp) >= 1023) return -1;
    strcpy(out, tmp);
    return 0;
}

void handle_connection(int client_fd, struct sockaddr_in *addr) {
    char buf[BUF_SIZE];
    ssize_t n = recv(client_fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';
    // Very simple request line parse: METHOD PATH HTTP/VERSION
    char method[16], path[1024], version[32];
    if (sscanf(buf, "%15s %1023s %31s", method, path, version) != 3) {
        // Bad request
        const char *resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        close(client_fd);
        return;
    }
    printf("%s %s from %s:%d\n", method, path, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    if (strcmp(method, "GET") != 0) {
        const char *resp = "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        close(client_fd);
        return;
    }
    char file_path[2048];
    // make a copy because safe_path uses strtok (modifies)
    char path_copy[1024];
    strncpy(path_copy, path, sizeof(path_copy)-1);
    path_copy[sizeof(path_copy)-1] = '\0';
    if (safe_path(file_path, path_copy) < 0) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        close(client_fd);
        return;
    }
    // open file
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        // 404
        const char *notfound_body = "<html><body><h1>404 Not Found</h1></body></html>\n";
        char header[256];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 strlen(notfound_body));
        send(client_fd, header, strlen(header), 0);
        send(client_fd, notfound_body, strlen(notfound_body), 0);
        close(client_fd);
        return;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        close(client_fd);
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        const char *forbid = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, forbid, strlen(forbid), 0);
        close(client_fd);
        return;
    }
    const char *mime = get_mime_type(file_path);
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %lld\r\n"
             "Connection: close\r\n"
             "\r\n",
             mime, (long long)st.st_size);
    send(client_fd, header, strlen(header), 0);
    // send file in chunks
    ssize_t sent = 0;
    while (1) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        ssize_t w = 0;
        while (w < r) {
            ssize_t s = send(client_fd, buf + w, r - w, 0);
            if (s < 0) { perror("send"); break; }
            w += s;
        }
        if (w < r) break;
        sent += w;
    }
    close(fd);
    close(client_fd);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\nExample: %s 8080\n", argv[0], argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port.\n");
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) { perror("bind"); close(listen_fd); return 1; }
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); close(listen_fd); return 1; }

    printf("Mini HTTP server listening on port %d\n", port);
    while (1) {
        struct sockaddr_in client;
        socklen_t clilen = sizeof(client);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client, &clilen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        // single-threaded: handle inline
        handle_connection(client_fd, &client);
    }

    close(listen_fd);
    return 0;
}
