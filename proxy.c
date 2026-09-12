#include <stdio.h>
#include <string.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
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


int main(int argc, char **argv)
{
    int listenfd;
    int connfd;
    int serverfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    rio_t rio;
    char buf[MAXLINE];
    rio_t server_rio;
    char request[MAXLINE];
    ssize_t n;
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
    char hostname[MAXLINE];
    char port[16];
    char path[MAXLINE];




    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    printf("Proxy listening on port %s\n", argv[1]);

    while (1) {
    clientlen = sizeof(clientaddr);

    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    printf("Accepted a connection\n");

    Rio_readinitb(&rio, connfd);

    if (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        printf("Request line: %s", buf);

        sscanf(buf, "%s %s %s", method, uri, version);

        printf("Method: %s\n", method);
        printf("URI: %s\n", uri);
        printf("Version: %s\n", version);
        parse_uri(uri, hostname, port, path);

        printf("Hostname: %s\n", hostname);
        printf("Port: %s\n", port);
        printf("Path: %s\n", path);

    }


    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
    printf("Header: %s", buf);

    if (strcmp(buf, "\r\n") == 0) {
        break;
        }
    }

    serverfd = Open_clientfd(hostname, port);

    printf("Connected to server %s:%s\n", hostname, port);

    /* Build a new HTTP request for the real web server */
    snprintf(request, MAXLINE,
         "GET %s HTTP/1.0\r\n"
         "Host: %s:%s\r\n"
         "%s"
         "Connection: close\r\n"
         "Proxy-Connection: close\r\n"
         "\r\n",
         path, hostname, port, user_agent_hdr);

    printf("Sending request to server:\n%s", request);

    /* Send request to Tiny */
    Rio_writen(serverfd, request, strlen(request));

    /* Read Tiny's response and send it back to curl */
    Rio_readinitb(&server_rio, serverfd);

    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        Rio_writen(connfd, buf, n);
    }

    Close(serverfd);
    Close(connfd);

    }


    return 0;
}

