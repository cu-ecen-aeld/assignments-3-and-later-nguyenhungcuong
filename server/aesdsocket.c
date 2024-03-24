#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

int socket_fd;
int fd_rx;
const char* rx_file_path = "/var/tmp/aesdsocketdata";
#define MAX_RX_BUFFER_SIZE 1024
char rx_buff[MAX_RX_BUFFER_SIZE];

void signal_handler(int sig_num)
{
    syslog(LOG_INFO, "Caught signal, exiting\n");
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
    close(fd_rx);
    closelog();

    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    // set up log
    openlog(NULL, 0, LOG_USER);

    // register signal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

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

    struct sockaddr incoming_addr;
    socklen_t incoming_addr_len = 0;
    int accepted_socket = 0;

    bool packet_complete;
    ssize_t number_of_bytes_receive;
    ssize_t number_of_bytes_send;
    int number_of_bytes_write;
    size_t number_of_bytes_read;

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
            syslog(LOG_DEBUG, "Accepted connection\n");

            // create file for receiving data
            fd_rx = open(rx_file_path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU | S_IRGRP | S_IROTH);
            if (fd_rx == (-1))
            {
                syslog(LOG_ERR, "Error creating rx file: %s\n", strerror(errno));
                exit_failure();
            }
            syslog(LOG_DEBUG, "File created\n");

            // receive data over connection
            packet_complete = false;
            do
            {
                number_of_bytes_receive = recv(accepted_socket, rx_buff, MAX_RX_BUFFER_SIZE - 1, 0);
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
                
                number_of_bytes_write = write(fd_rx, rx_buff, number_of_bytes_receive);
                if (number_of_bytes_write == (-1))
                {
                    syslog(LOG_ERR, "Error writing rx file: %s\n", strerror(errno));
                    exit_failure();
                }
                syslog(LOG_DEBUG, "Write %i bytes success\n", number_of_bytes_write);
            }
            while (packet_complete == false);

            // send received data to client
            packet_complete = false;
            lseek(fd_rx, 0, SEEK_SET);
            do
            {
                number_of_bytes_read = read(fd_rx, rx_buff, MAX_RX_BUFFER_SIZE);
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
                    number_of_bytes_send = send(accepted_socket, rx_buff, number_of_bytes_read, 0);
                    if (number_of_bytes_send == (-1))
                    {
                        syslog(LOG_ERR, "Error sending: %s\n", strerror(errno));
                        exit_failure();
                    }
                    syslog(LOG_DEBUG, "Send %li bytes success\n", number_of_bytes_send);
                }
            }
            while (packet_complete == false);

            // close file
            close(fd_rx);
        }
    }
}
