#include <stdio.h>
#include <string.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_SLOTS 10

typedef struct {
    int valid;
    char uri[MAXLINE];
    char data[MAX_OBJECT_SIZE];
    int size;
    unsigned long long timestamp;
} cache_entry;

static cache_entry cache[CACHE_SLOTS];

static unsigned long long cache_clock = 0;

/* All threads share the cache, so protect it with a mutex */
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";


/* Parse a URI into hostname, port, and path */
void parse_uri(const char *uri, char *hostname, char *port, char *path)
{
    const char *host_start;
    const char *path_start;
    char hostport[MAXLINE];
    char *colon;
    size_t len;

    host_start = uri;

    /* Skip "http://" */
    if (strncmp(uri, "http://", 7) == 0) {
        host_start = uri + 7;
    }

    /* Find the beginning of the path */
    path_start = strchr(host_start, '/');

    if (path_start != NULL) {
        strcpy(path, path_start);
        len = path_start - host_start;
    } else {
        strcpy(path, "/");
        len = strlen(host_start);
    }

    /* Copy host[:port] part */
    memcpy(hostport, host_start, len);
    hostport[len] = '\0';

    /* Separate hostname and port */
    colon = strchr(hostport, ':');

    if (colon != NULL) {
        *colon = '\0';
        strcpy(hostname, hostport);
        strcpy(port, colon + 1);
    } else {
        strcpy(hostname, hostport);
        strcpy(port, "80");
    }
}

int cache_get(const char *uri, char *data)
{
    int i;
    int size = -1;

    pthread_mutex_lock(&cache_mutex);

    for (i = 0; i < CACHE_SLOTS; i++) {
        if (cache[i].valid &&
            strcmp(cache[i].uri, uri) == 0) {

            memcpy(data, cache[i].data, cache[i].size);
            size = cache[i].size;

            cache[i].timestamp = ++cache_clock;
            break;
        }
    }

    pthread_mutex_unlock(&cache_mutex);

    return size;
}

void cache_put(const char *uri, const char *data, int size)
{
    int i;
    int victim = -1;

    if (size > MAX_OBJECT_SIZE) {
        return;
    }

    pthread_mutex_lock(&cache_mutex);

    /* First look for an empty slot */
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (!cache[i].valid) {
            victim = i;
            break;
        }
    }

    /* No empty slot: evict the least recently used object */
    if (victim == -1) {
        victim = 0;

        for (i = 1; i < CACHE_SLOTS; i++) {
            if (cache[i].timestamp < cache[victim].timestamp) {
                victim = i;
            }
        }
    }

    strcpy(cache[victim].uri, uri);
    memcpy(cache[victim].data, data, size);

    cache[victim].size = size;
    cache[victim].valid = 1;
    cache[victim].timestamp = ++cache_clock;

    pthread_mutex_unlock(&cache_mutex);
}


/* Handle one client connection */
void handle_client(int connfd)
{
    int serverfd;

    rio_t rio;
    rio_t server_rio;

    char buf[MAXLINE];
    char request[MAXLINE * 4];

    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];

    char hostname[MAXLINE];
    char port[16];
    char path[MAXLINE];

    ssize_t n;
    char cached_data[MAX_OBJECT_SIZE];
    char object_data[MAX_OBJECT_SIZE];

    int cached_size;
    int object_size = 0;
    int can_cache = 1;


    /* Read request line from client */
    Rio_readinitb(&rio, connfd);

    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return;
    }

    printf("Request line: %s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    printf("Method: %s\n", method);
    printf("URI: %s\n", uri);
    printf("Version: %s\n", version);

    /* Parse URI */
    parse_uri(uri, hostname, port, path);

    printf("Hostname: %s\n", hostname);
    printf("Port: %s\n", port);
    printf("Path: %s\n", path);

    /* Read and discard the rest of the client's headers */
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        printf("Header: %s", buf);

        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
    }

    /* Check whether this object is already cached */
    cached_size = cache_get(uri, cached_data);

    if (cached_size >= 0) {
        printf("Cache hit: %s\n", uri);

        Rio_writen(connfd, cached_data, cached_size);

        return;
    }

    printf("Cache miss: %s\n", uri);


    /* Connect to the real web server */
    serverfd = Open_clientfd(hostname, port);

    printf("Connected to server %s:%s\n", hostname, port);

    /* Build a new HTTP request */
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s:%s\r\n"
             "%s"
             "Connection: close\r\n"
             "Proxy-Connection: close\r\n"
             "\r\n",
             path, hostname, port, user_agent_hdr);

    printf("Sending request to server:\n%s", request);

    /* Send request to server */
    Rio_writen(serverfd, request, strlen(request));

    /* Read response from server and forward it to the client */
    Rio_readinitb(&server_rio, serverfd);

    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {

        /* Send the response to the client as usual */
        Rio_writen(connfd, buf, n);

        /* At the same time, save a copy if it is small enough */
        if (can_cache) {
            if (object_size + n <= MAX_OBJECT_SIZE) {
                memcpy(object_data + object_size, buf, n);
                object_size += n;
            } else {
                can_cache = 0;
            }
        }
    }

    if (can_cache) {
    cache_put(uri, object_data, object_size);
    printf("Cached: %s (%d bytes)\n", uri, object_size);
    }

    Close(serverfd);
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);

    free(vargp);

    /* This thread does not need to be joined later */
    Pthread_detach(Pthread_self());

    handle_client(connfd);

    Close(connfd);

    return NULL;
}


int main(int argc, char **argv)
{
    int listenfd;
    int *connfdp;
    pthread_t tid;

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    printf("Proxy listening on port %s\n", argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);

        connfdp = Malloc(sizeof(int));

        *connfdp = Accept(listenfd,
                          (SA *)&clientaddr,
                          &clientlen);

        printf("Accepted a connection\n");

        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}
