#include <err.h>
#include <errno.h>
#include <gmodule.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

char notfound[] = "HTTP/1.1 404 Not Found\r\n\r\nError 404: Page Not Found\0";
char ok_msg[] = "HTTP/1.1 200 OK\r\n\r\n\0";
pthread_t thr;

int find_space(char *request)
{
    int i = 0;
    while (request[i] && request[i] != ' ')
        ++i;
    return i;
}

void send_response(GString *resource, int client)
{
    if (resource->len == 0)
    {
        g_string_append(resource, "index.html");
    }
    printf("%d: %s\n", client, resource->str);

    if (!strcmp(resource->str, "slow.html"))
    {
        sleep(10);
    }

    resource = g_string_prepend(resource, "www/");
    char *content;
    size_t lg;
    if (g_file_get_contents(resource->str, &content, &lg, NULL) == FALSE)
    {
        send(client, notfound, strlen(notfound), 0);
    }
    else
    {
        send(client, ok_msg, strlen(ok_msg), MSG_MORE);
        write(client, content, lg);
    }
}

GString *get_resource(GString *request)
{
    int len = find_space(request->str + 5);
    GString *res = g_string_new_len(request->str + 5, len);
    if (res == NULL)
        errx(EXIT_FAILURE, "Fail to build GString");
    return res;
}

void print_request(char *buffer, ssize_t len, int client)
{
    GString *str = g_string_new(NULL);
    if (str == NULL)
        errx(EXIT_FAILURE, "Fail to build GString");
    ssize_t i = 0;
    GString *resource;
    while (i < len)
    {
        g_string_append_c(str, buffer[i]);
        if (g_str_has_suffix(str->str, "\r\n\r\n"))
        {
            // + 5 to skip the 'GET /' from the beginning of the request
            resource = get_resource(str);
            send_response(resource, client);
            g_string_free(str, 1);
            g_string_free(resource, 1);
            str = g_string_new(NULL);
            if (str == NULL)
                errx(EXIT_FAILURE, "Fail to build GString");
        }
        ++i;
    }
    if (str != NULL)
        g_string_free(str, 1);
}

void *client_pthread(void *arg)
{
    long client = (long) arg;
    char buff[4096];
    ssize_t lg = recv(client, buff, 4096, 0);
    if (lg > 0)
    {
        print_request(buff, lg, client);
    }

    close(client);
    return NULL;
}

int binder(char *ip, char *port)
{
    struct addrinfo hints;
    struct addrinfo *result;
    int addrinfo_error;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo_error = getaddrinfo(ip, port, &hints, &result);

    if (addrinfo_error != 0)
    {
        errx(EXIT_FAILURE, "Fail getting address for %s on port %s: %s", ip,
             port, gai_strerror(addrinfo_error));
    }

    struct addrinfo *rp;
    int cnx;
    int val = 1;
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        cnx = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        setsockopt(cnx, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
        if (cnx == -1)
            continue;
        if (bind(cnx, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // SUCCESS
        close(cnx);
    }
    if (rp == NULL)
        errx(EXIT_FAILURE, "Couldn't bind");

    freeaddrinfo(result);
    return cnx;
}

int main(void)
{
    int cnx = binder("localhost", "2048");
    listen(cnx, SOMAXCONN);
    printf("%s\n", "Static Server\nListening to port 2048...");
    for (;;)
    {
        long client = accept(cnx, NULL, NULL);
        pthread_detach(pthread_create(&thr, NULL, client_pthread, (void *)client));
    }
}