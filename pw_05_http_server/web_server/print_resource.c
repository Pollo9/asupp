#include <err.h>
#include <errno.h>
#include <gmodule.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

char _response[] = "HTTP/1.1 200 OK\r\n\r\nHello World!\0";

int find_space(char *request)
{
    int i = 0;
    while (request[i] && request[i] != ' ')
        ++i;
    return i;
}

void print_request(char *buffer, ssize_t len, int client)
{
    GString *str = g_string_new(NULL);
    if (str == NULL)
        errx(EXIT_FAILURE, "Fail to build GString");
    ssize_t i = 0;
    while (i < len)
    {
        g_string_append_c(str, buffer[i]);
        if (g_str_has_suffix(str->str, "\r\n\r\n"))
        {
            int len = find_space(str->str + 5);
            printf("Resource = %.*s\n", len, str->str + 5);
            send(client, _response, strlen(_response), 0);
            g_string_free(str, 1);
            str = g_string_new(NULL);
            if (str == NULL)
                errx(EXIT_FAILURE, "Fail to build GString");
        }
        ++i;
    }
    if (str != NULL)
        g_string_free(str, 1);
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
            break;
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
        int client = accept(cnx, NULL, NULL);
        char buff[4096];
        ssize_t lg = recv(client, buff, 4096, 0);
        if (lg == 0)
        {
            close(client);
            continue;
        }
        print_request(buff, lg, client);
        close(client);
    }
}