#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

int socket_fd;
int fd_rx;
const char* rx_file_path = "/var/tmp/aesdsocketdata";
#define MAX_RX_BUFFER_SIZE 1024
char rx_buff[MAX_RX_BUFFER_SIZE];
pthread_mutex_t file_mutex;

typedef struct list_data_s list_data_t;
struct list_data_s {
    pthread_t thread;
    LIST_ENTRY(list_data_s) entries;
};

LIST_HEAD(listhead, list_data_s) head;
list_data_t *datap = NULL;

void signal_handler(int sig_num)
{
    syslog(LOG_INFO, "Caught signal, exiting\n");
    
    // join thread
    LIST_FOREACH(datap, &head, entries)
    {
        pthread_cancel(datap->thread);
        pthread_join(datap->thread, NULL);
    }

    while (!LIST_EMPTY(&head)) {
        datap = LIST_FIRST(&head);
        LIST_REMOVE(datap, entries);
        free(datap);
    }

    int ret_shutdown = shutdown(socket_fd, SHUT_RDWR);
    if (ret_shutdown == (-1))
    {
        syslog(LOG_ERR, "Failed to close socket\n");
    }
    remove(rx_file_path);
    closelog();
    
    exit(EXIT_SUCCESS);
}

static void exit_failure(void)
{
    syslog(LOG_INFO, "exit_failure\n");

    close(fd_rx);
    closelog();

    exit(EXIT_FAILURE); 
}

struct thread_connection_param {
    int socket_fd;
    int file_fd;
    pthread_mutex_t *mutex;
};

void *thread_timestamp(void *input_param)
{
    syslog(LOG_DEBUG, "thread_timestamp enter\n");

    char outstr[200];
    time_t t;
    struct tm *tmp;
    size_t size;

    while (true)
    {
        sleep(10);

        t = time(NULL);
        tmp = localtime(&t);

        size = strftime(outstr, sizeof(outstr), "timestamp:%a, %d %b %Y %T %z\n", tmp);

        pthread_mutex_lock(&file_mutex);
        write(fd_rx, outstr, size);
        pthread_mutex_unlock(&file_mutex);
    }

    syslog(LOG_DEBUG, "thread_timestamp exit\n");

    return NULL;
}

void thread_cleanup_handler(void *arg)
{
    syslog(LOG_DEBUG, "thread_cleanup_handler\n");

    // cast param
    struct thread_connection_param* thread_param = (struct thread_connection_param *)arg;
    
    // remove from the LIST
    LIST_FOREACH(datap, &head, entries)
    {
        if(pthread_equal(pthread_self(), datap->thread))
        {
            break;
        }
    }

    if(datap != NULL)
    {
        LIST_REMOVE(datap, entries);
        free(datap);
    }

    // close socket
    close(thread_param->socket_fd);
    
    free(thread_param);
}

void *thread_connection(void *input_param)
{
    // cast param
    struct thread_connection_param* thread_param = (struct thread_connection_param *) input_param;

    syslog(LOG_DEBUG, "thread_connection enter: %i\n", thread_param->socket_fd);

    pthread_cleanup_push(thread_cleanup_handler, &thread_param);

    bool packet_complete;
    ssize_t number_of_bytes_receive;
    ssize_t number_of_bytes_send;
    int number_of_bytes_write;
    size_t number_of_bytes_read;

    // receive data over connection
    pthread_mutex_lock(thread_param->mutex);
    packet_complete = false;
    do
    {
        number_of_bytes_receive = recv(thread_param->socket_fd, rx_buff, MAX_RX_BUFFER_SIZE - 1, 0);
        if (number_of_bytes_receive == (-1))
        {
            syslog(LOG_ERR, "Error receiving: %s\n", strerror(errno));
            exit_failure();
        }
        syslog(LOG_DEBUG, "Received %li bytes\n", number_of_bytes_receive);
        
        if (strchr(rx_buff, '\n') != NULL)
        {
            packet_complete = true;
            syslog(LOG_DEBUG, "Received completed\n");
        }
        
        // write with mutex protect
        number_of_bytes_write = write(thread_param->file_fd, rx_buff, number_of_bytes_receive);

        if (number_of_bytes_write == (-1))
        {
            syslog(LOG_ERR, "Error writing rx file: %s\n", strerror(errno));
            exit_failure();
        }
        syslog(LOG_DEBUG, "Write %i bytes success\n", number_of_bytes_write);
    }
    while (packet_complete == false);
    pthread_mutex_unlock(thread_param->mutex);  

    // send received data to client
    packet_complete = false;
    lseek(thread_param->file_fd, 0, SEEK_SET);
    do
    {
        number_of_bytes_read = read(thread_param->file_fd, rx_buff, MAX_RX_BUFFER_SIZE);
        if (number_of_bytes_read == (-1))
        {
            syslog(LOG_ERR, "Error reading rx file: %s\n", strerror(errno));
            exit_failure();
        }
        syslog(LOG_DEBUG, "Read %li bytes success\n", number_of_bytes_read);

        if (number_of_bytes_read == 0)
        {
            packet_complete = true;
            syslog(LOG_DEBUG, "Read completed\n");
        }
        else
        {
            number_of_bytes_send = send(thread_param->socket_fd, rx_buff, number_of_bytes_read, 0);
            if (number_of_bytes_send == (-1))
            {
                syslog(LOG_ERR, "Error sending: %s\n", strerror(errno));
                exit_failure();
            }
            syslog(LOG_DEBUG, "Send %li bytes success\n", number_of_bytes_send);
        }
    }
    while (packet_complete == false);

    pthread_cleanup_pop(0);

    // remove from the LIST
    LIST_FOREACH(datap, &head, entries)
    {
        if(pthread_equal(pthread_self(), datap->thread))
        {
            break;
        }
    }

    if(datap != NULL)
    {
        LIST_REMOVE(datap, entries);
        free(datap);
    }

    syslog(LOG_DEBUG, "thread_connection exit %i\n", thread_param->socket_fd);
    
    // close socket
    close(thread_param->socket_fd);
    
    // free param
    free(thread_param);

    return NULL;
}

int main(int argc, char* argv[])
{
    syslog(LOG_DEBUG, "MAIN\n");

    // set up log
    openlog(NULL, 0, LOG_USER);

    // register signal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // init mutex
    pthread_mutexattr_t mutex_att;
    pthread_mutexattr_init(&mutex_att);
    pthread_mutexattr_settype(&mutex_att, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutexattr_setrobust(&mutex_att, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&file_mutex, &mutex_att);

    // create file for receiving data
    fd_rx = open(rx_file_path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU | S_IRGRP | S_IROTH);
    if (fd_rx == (-1))
    {
        syslog(LOG_ERR, "Error creating rx file: %s\n", strerror(errno));
        exit_failure();
    }
    syslog(LOG_DEBUG, "File created: %i\n", fd_rx);

    struct addrinfo hints;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    struct addrinfo* res;
    int ret_getaddr = getaddrinfo(NULL, "9000", &hints, &res);
    if (ret_getaddr != 0)
    {
        syslog(LOG_ERR, "Error get address info: %i\n", ret_getaddr);
        exit_failure();
    }

    // create socket
    socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == (-1))
    {
        syslog(LOG_ERR, "Error open socket: %s\n", strerror(errno));
        exit_failure();
    }
    syslog(LOG_DEBUG, "Create socket success: %i\n", socket_fd);
    int optval = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    // bind address
    int ret_bind = bind(socket_fd, res->ai_addr, res->ai_addrlen);
    if (ret_bind == (-1))
    {
        syslog(LOG_ERR, "Error bind address: %s\n", strerror(errno));
        exit_failure();
    }
    syslog(LOG_DEBUG, "Bind address success: %i\n", ret_bind);
    freeaddrinfo(res);

    if (argc > 1)
    {
        if (!strcmp(argv[1], "-d"))
        {
            // run application as daemon
            pid_t child_pid = fork();
            if (child_pid == (-1))
            {
                syslog(LOG_ERR, "Error creating Daemon\n");
                exit_failure();
            }
            else if (child_pid > 0)
            {
                exit(EXIT_SUCCESS);
            }
            else
            {
                // This is the child. Convert this to a daemon.
                if (setsid() == -1)
                {
                    syslog(LOG_ERR, "Error creating new session");
                    exit_failure();
                }
                chdir("/");
                open("/dev/null", O_RDWR);
            }
        }
        else
        {
            syslog(LOG_ERR, "Argument not supported");
            exit_failure();
        }
    }

    // listen
    int ret_listen = listen(socket_fd, 10);
    if (ret_listen == (-1))
    {
        syslog(LOG_ERR, "Error listen: %s\n", strerror(errno));
        exit_failure();
    }
    syslog(LOG_DEBUG, "Listen success\n");

    // init LIST
    LIST_INIT(&head);

    // create timestamp thread
    datap = malloc(sizeof(list_data_t));
    LIST_INSERT_HEAD(&head, datap, entries);
    pthread_create(&datap->thread, NULL, thread_timestamp, NULL);

    struct sockaddr incoming_addr;
    socklen_t incoming_addr_len = 0;
    int accepted_socket = 0;
    while (true)
    {
        // accept
        accepted_socket = accept(socket_fd, &incoming_addr, &incoming_addr_len);
        if (accepted_socket == (-1))
        {
            syslog(LOG_ERR, "Error accept: %s\n", strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG, "Accepted connection %i\n", accepted_socket);

            // add to LIST
            datap = malloc(sizeof(list_data_t));
            LIST_INSERT_HEAD(&head, datap, entries);

            // create new thread
            struct thread_connection_param* param = malloc(sizeof(struct thread_connection_param));
            param->socket_fd = accepted_socket;
            param->file_fd = fd_rx;
            param->mutex = &file_mutex;

            pthread_create(&datap->thread, NULL, thread_connection, param);
        }
    }
}
