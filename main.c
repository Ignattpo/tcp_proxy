#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>

volatile unsigned char terminated = 0;

struct connect_t {
    char* source_addr;
    char *c_addr;
    unsigned short int port;
    struct sockaddr_in sock_addr;
};

struct tunnel_t {
    int  client;
    int  server;
};

void sigint_cb ( int sig ) {
    if ( !terminated ) {
        terminated = 1;
    }
}

static void signals_init ( void ) {
    /* Обработчик сигнала SIGINT */
    struct sigaction sa_hup;
    memset ( &sa_hup, 0, sizeof ( sa_hup ) );
    sa_hup.sa_handler = sigint_cb;
    sa_hup.sa_flags = SA_RESTART;
    sigaction ( SIGINT, &sa_hup, 0 );
}

static int main_parse_arguments ( int argc, char *argv[],struct connect_t *proxy, struct connect_t *web ) {
    static struct option long_options[] = {
        // аргументы
        {"proxy_addr", required_argument, 0, 'l'}, // адрес proxy
        {"web_addr", required_argument, 0, 'd'}, // адрес web
        {0, 0, 0, 0} // конец аргументов
    };

    int c = -1;
    do {
        int option_index = 0;
        /* Получаем следующую опцию... */
        c = getopt_long ( argc, argv, "l:d:", long_options, &option_index );
        if ( c == -1 ) {
            break;
        }

        switch ( c ) {
        case 'l':
            if ( optarg ) {
                proxy->source_addr = strdup ( optarg );
                if ( proxy->source_addr == NULL ) {
                    if ( web->source_addr ) {
                        free ( web->source_addr );
                    }
                    goto aborting_strdup;
                }
            }
            break;
        case 'd':
            if ( optarg ) {
                web->source_addr = strdup ( optarg );
                if ( web->source_addr == NULL ) {
                    if ( proxy->source_addr ) {
                        free ( proxy->source_addr );
                    }
                    goto aborting_strdup;
                }
            }
            break;
        }
    } while ( c != -1 );

    if ( !proxy->source_addr && !web->source_addr ) {
        fprintf ( stderr,"missing arguments \n" );
        return -1;
    }

    if ( !proxy->source_addr ) {
        fprintf ( stderr,"missing argument -l \n" );
        free ( web->source_addr );
        return -1;
    }
    if ( !web->source_addr ) {
        fprintf ( stderr,"missing argument -d \n" );
        free ( proxy->source_addr );
        return -1;
    }

    return 0;

aborting_strdup:
    perror ( "strdup" );
    return -1;
}

static int parse_addr ( struct connect_t *connect ) {
    connect->c_addr = strtok ( connect->source_addr,":" );
    if ( !connect->c_addr ) {
        fprintf ( stderr,"missing source_addr\n" );
        return -1;
    }
    char* c_port = strtok ( NULL,":" );
    if ( !c_port ) {
        fprintf ( stderr,"missing port %s\n",connect->source_addr );
        return -1;
    }
    connect->port=atoi ( c_port );

    return 0;
}

static int init_sock_addr ( struct connect_t *connect ) {
    struct hostent* hosten = gethostbyname ( connect->c_addr );
    if ( !hosten ) {
        perror ( "gethostbyname" );
        return -1;
    }

    struct in_addr **  addr_list= ( struct in_addr ** ) hosten->h_addr_list;
    if ( addr_list[0]==NULL ) {
        fprintf ( stderr,"incorrect addres %s\n",connect->c_addr );
        return -1;
    }
    connect->sock_addr.sin_family = AF_INET;
    connect->sock_addr.sin_port = htons ( connect->port );
    connect->sock_addr.sin_addr=*addr_list[0];

    return 0;
}

void* communication ( void* thread_data ) {
    char buf[1024];
    struct tunnel_t *tunnel = thread_data;
    while ( !terminated ) {
        int  bytes_read = recv ( tunnel->client, buf,  sizeof ( buf ), 0 );
        if ( bytes_read <= 0 ) {
            break;
        }

        send ( tunnel->server, buf, bytes_read, 0 );
        bytes_read = recv ( tunnel->server, buf,  sizeof ( buf ), 0 );
        if ( bytes_read <= 0 ) {
            break;
        }

        send ( tunnel->client, buf, bytes_read, 0 );
    }

    close ( tunnel->client );
    close ( tunnel->server );
    free ( tunnel );

    return 0;
}


int main ( int argc, char *argv[] ) {
    signals_init();

    struct connect_t proxy = {.c_addr = NULL,.port = 0,.source_addr = NULL};
    struct connect_t web = {.c_addr = NULL,.port = 0,.source_addr = NULL};
    int res = main_parse_arguments ( argc, argv, &proxy, &web );
    if ( res == -1 ) {
        goto aborting;
    }

    res = parse_addr ( &proxy );
    if ( res == -1 ) {
        goto  aborting_addr;
    }

    res = parse_addr ( &web );
    if ( res == -1 ) {
        goto  aborting_addr;
    }

    res = init_sock_addr ( &proxy );
    if ( res == -1 ) {
        goto  aborting_addr;
    }

    res = init_sock_addr ( &web );
    if ( res == -1 ) {
        goto  aborting_addr;
    }

    int  main_socket = socket ( AF_INET, SOCK_STREAM, 0 );
    if ( main_socket < 0 ) {
        goto  aborting_socket;
    }

    if ( bind ( main_socket, ( struct sockaddr * ) &proxy.sock_addr, sizeof ( proxy.sock_addr ) ) < 0 ) {
        goto  aborting_socket;
    }

    const  struct timespec time = {.tv_sec = 1,.tv_nsec = 0};
    const sigset_t  sigmask = {SIGINT};

    listen ( main_socket, 1 );
    while ( !terminated ) {
        fd_set rfds;
        FD_ZERO ( &rfds );
        FD_SET ( main_socket, &rfds );
        res = pselect ( main_socket+1, &rfds,  NULL,  NULL, &time, &sigmask );
        if ( res == -1 ) {
            goto  aborting_socket;
        }
        if ( res == 0 ) {
            continue;
        }
        int  sock = accept ( main_socket, NULL, NULL );
        if ( sock < 0 ) {
            goto  aborting_socket;
        }

        int  web_socket = socket ( AF_INET, SOCK_STREAM, 0 );
        if ( web_socket < 0 ) {
            goto  aborting_socket;
        }
        if ( connect ( web_socket, ( struct sockaddr * ) &web.sock_addr, sizeof ( web.sock_addr ) ) < 0 ) {
            goto  aborting_socket;
        }
        pthread_t thread;
        struct tunnel_t *tunnel = malloc ( sizeof ( *tunnel ) );
        tunnel->client = sock;
        tunnel->server = web_socket;
        res =  pthread_create ( &thread, NULL, communication, tunnel );
        if ( res != 0 ) {
            fprintf ( stderr,"Error  pthread create.\n" );
        }

    }

    close ( main_socket );
    free ( proxy.source_addr );
    free ( web.source_addr );

    return 0;

aborting_socket:
    close ( main_socket );
aborting_addr:
    free ( proxy.source_addr );
    free ( web.source_addr );
aborting:
    return -1;
}
