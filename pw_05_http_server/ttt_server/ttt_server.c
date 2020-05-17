#include <err.h>
#include <errno.h>
#include <glib/gprintf.h>
#include <gmodule.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

char _msgOk[] = "HTTP/1.1 200 OK\r\n\r\n\0";
pthread_t thr;
int _player_counter = 0;
int _restart_counter = 0;
char grid[] = "_________";

int find_space(char *request)
{
    int i = 0;
    while (request[i] && request[i] != ' ')
        ++i;
    return i;
}

void grid_command(long client, char *playerName)
{
    char *content = NULL;
    size_t lg;
    if (_player_counter == 2)
    {
        if (g_file_get_contents("www/busy.html", &content, &lg, NULL))
        {
            send(client, _msgOk, strlen(_msgOk), MSG_MORE);
            write(client, content, lg);
        }
    }
    else
    {
        char symbol = (_player_counter == 0) ? 'x' : 'o';
        _player_counter++;
        if (g_file_get_contents("www/grid.html", &content, &lg, NULL))
        {
            char *to_send = g_strdup_printf(content, symbol, playerName);
            if (to_send == NULL)
                errx(EXIT_FAILURE, "Fail to g strdup printf");
            send(client, _msgOk, strlen(_msgOk), MSG_MORE);
            write(client, to_send, strlen(to_send));
            g_free(to_send);
        }
    }
    g_free(content);
}

void restart_command(long client)
{
    if (_restart_counter == 0)
    {
        _player_counter--;
        int i = 0;
        while(i < 9)
        {
            grid[i] = '_';
            i++;
        }   
        _restart_counter++;
    }
    else
    {
        _player_counter++;
        _restart_counter--;
    }
    send(client, grid, 9, 0);
}

void default_command(long client)
{
    char *content = NULL;
    size_t lg;
    gboolean res;
    if (_player_counter < 2)
        res = g_file_get_contents("www/new_player.html", &content, &lg, NULL);
    else
        res = g_file_get_contents("www/busy.html", &content, &lg, NULL);
    if (res)
    {
        send(client, _msgOk, strlen(_msgOk), MSG_MORE);
        write(client, content, lg);
        g_free(content);
    }
}

void apply_command(long client, GString *resource)
{
    char c[2];
    int index;
    char playerName[1024];
    if (!strcmp(resource->str, "update"))
        send(client, grid, 9, 0);
    else if (sscanf(resource->str, "set_%[xo]%d", c, &index) == 2)
    {
        grid[index] = c[0];
        send(client, grid, 9, 0);
    }
    else if (sscanf(resource->str, "grid?nickname=%s", playerName) == 1)
        grid_command(client, playerName);
    else if (!strcmp(resource->str, "restart"))
        restart_command(client);
    else
        default_command(client);
}

void send_response(GString *resource, long client)
{
    printf("%ld: %s\n", client, resource->str);
    GString *path = g_string_new(NULL);
    g_string_printf(path, "%s%s", "www/", resource->str);
    char *content = NULL;
    size_t lg;
    if (g_file_get_contents(path->str, &content, &lg, NULL))
    {
        send(client, _msgOk, strlen(_msgOk), MSG_MORE);
        write(client, content, lg);
        g_free(content);
    }
    else
        apply_command(client, resource);
    g_string_free(path, 1);
}

GString *get_resource(GString *request)
{
    int len = find_space(request->str + 5);
    GString *res = g_string_new_len(request->str + 5, len);
    if (res == NULL)
        errx(EXIT_FAILURE, "Fail to build GString");
    return res;
}

void print_request(char *buffer, ssize_t len, long client)
{
    GString *str = g_string_new(NULL);
    if (str == NULL)
        errx(EXIT_FAILURE, "Fail to build GString");

    ssize_t i = 0;
    GString *resource;
    for(ssize_t i = 0; i < len; i++)
    {
        g_string_append_c(str, buffer[i]);
        if (g_str_has_suffix(str->str, "\r\n\r\n"))
        {
            resource = get_resource(str);
            send_response(resource, client);
            g_string_free(str, 1);
            g_string_free(resource, 1);
            str = g_string_new(NULL);
            if (str == NULL)
                errx(EXIT_FAILURE, "Fail to build GString");
        }
    }
    if (str != NULL)
        g_string_free(str, 1);
}

void *client_pthread(void *arg)
{
    long client = (long)arg;
    char buff[4096];
    ssize_t lg = recv(client, buff, 4096, 0);
    if (lg > 0)
        print_request(buff, lg, client);

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
    rp = result;
    while(rp != NULL)
    {
        cnx = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        setsockopt(cnx, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
        if (cnx == -1)
            continue;
        if (bind(cnx, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // SUCCESS
        close(cnx);
        rp = rp->ai_next
    }
    if (rp == NULL)
        errx(EXIT_FAILURE,
             "Couldn't bind, probably the ip is already used. If not, retry in "
             "few seconds");

    freeaddrinfo(result);
    return cnx;
}

int main(void)
{
    printf("%s\n", "Static Server\nListening to port 2048...");
    int cnx = binder("localhost", "2048");
    listen(cnx, SOMAXCONN);
    for (;;)
    {
        long client = accept(cnx, NULL, NULL);
        pthread_create(&thr, NULL, client_pthread, (void *)client);
        pthread_detach(thr);
    }
}