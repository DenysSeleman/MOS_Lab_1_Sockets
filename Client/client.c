#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define UNIX_SOCKET_PATH "/tmp/socket"
#define PORT 8080

// Функція для вимірювання часу
double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1E9;
}

// Функція для неблокуючого підключення
int non_blocking_connect(int sock_fd, struct sockaddr* addr, socklen_t addrlen) {
    if (connect(sock_fd, addr, addrlen) < 0) {
        if (errno == EINPROGRESS) {
            // Очікування на завершення з'єднання
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock_fd, &write_fds);

            struct timeval timeout;
            timeout.tv_sec = 5; // Макс. час очікування
            timeout.tv_usec = 0;

            int result = select(sock_fd + 1, NULL, &write_fds, NULL, &timeout);
            if (result > 0 && FD_ISSET(sock_fd, &write_fds)) {
                int optval;
                socklen_t optlen = sizeof(optval);
                if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1 || optval != 0) {
                    errno = optval;  // Присвоюємо помилку для обробки
                    return -1;
                }
                return 0;
            }
            else {
                errno = ETIMEDOUT;  // Час очікування закінчився
                return -1;
            }
        }
        return -1;
    }
    return 0;
}

// Функція для неблокуючої відправки даних
ssize_t non_blocking_send(int sock_fd, const void* buffer, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t bytes = send(sock_fd, (char*)buffer + sent, len - sent, 0);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Очікуємо, поки сокет буде готовий для запису
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(sock_fd, &write_fds);

                struct timeval timeout;
                timeout.tv_sec = 5;  // Макс. час очікування
                timeout.tv_usec = 0;

                int result = select(sock_fd + 1, NULL, &write_fds, NULL, &timeout);
                if (result <= 0) {
                    errno = (result == 0) ? ETIMEDOUT : errno;
                    return -1;
                }
                continue;
            }
            return -1;
        }
        sent += bytes;
    }
    return sent;
}

// Структура для параметрів окремого клієнта
typedef struct {
    char socket_type[5];
    char blocking_type[15];
    int packet_size;
    int num_packets;
    double connection_time;
} client_params;

// Функція для запуску окремого клієнта
void* run_client(void* args) {
    client_params* params = (client_params*)args;
    int sock_fd;
    struct sockaddr_in inet_addr;
    struct sockaddr_un unix_addr;
    char* buffer = (char*)malloc(params->packet_size);
    memset(buffer, 'A', params->packet_size); // Заповнюємо буфер символами

    struct timespec start_time, end_time;

    if (strcmp(params->socket_type, "INET") == 0) {
        if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            free(buffer);
            pthread_exit(NULL);
        }

        inet_addr.sin_family = AF_INET;
        inet_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, "127.0.0.1", &inet_addr.sin_addr) <= 0) {
            printf("Invalid address\n");
            free(buffer);
            pthread_exit(NULL);
        }
    }
    else {
        if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            free(buffer);
            pthread_exit(NULL);
        }

        unix_addr.sun_family = AF_UNIX;
        strcpy(unix_addr.sun_path, UNIX_SOCKET_PATH);
    }

    // Встановлюємо неблокуючий режим, якщо він був вказаний
    if (strcmp(params->blocking_type, "non-blocking") == 0) {
        int flags = fcntl(sock_fd, F_GETFL, 0);
        fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
    }

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Підключення клієнта до сервера
    int connect_result;
    if (strcmp(params->socket_type, "INET") == 0) {
        if (strcmp(params->blocking_type, "blocking") == 0)
            connect_result = connect(sock_fd, (struct sockaddr*)&inet_addr, sizeof(inet_addr));
        else
            connect_result = non_blocking_connect(sock_fd, (struct sockaddr*)&inet_addr, sizeof(inet_addr));
    }
    else {
        if (strcmp(params->blocking_type, "blocking") == 0)
            connect_result = connect(sock_fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr));
        else
            connect_result = non_blocking_connect(sock_fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr));
    }
    if (connect_result < 0) {
        perror("Connection failed");
        free(buffer);
        close(sock_fd);
        pthread_exit(NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    params->connection_time = get_time_diff(start_time, end_time);
    //printf("Connection time: %f seconds\n", params->connection_time);

    // Відправка пакетів
    for (int i = 0; i < params->num_packets; i++) {
        if (strcmp(params->blocking_type, "blocking") == 0) {
            if (send(sock_fd, buffer, params->packet_size, 0) < 0) {
                perror("Send failed");
                free(buffer);
                close(sock_fd);
                pthread_exit(NULL);
            }
        }
        else {
            if (non_blocking_send(sock_fd, buffer, params->packet_size) < 0) {
                perror("Send failed");
                free(buffer);
                close(sock_fd);
                pthread_exit(NULL);
            }
        }
    }

    free(buffer);
    close(sock_fd);
    pthread_exit((void*)params);
}

int main() {
    char socket_type[5], blocking_type[15];
    int packet_size, num_packets, client_number = 10;

    // Отримуємо від користувача параметри
    while (1) {
        printf("Enter socket type (UNIX/INET): ");
        scanf("%s", socket_type);
        if (strcmp(socket_type, "INET") == 0 || strcmp(socket_type, "UNIX") == 0)
            break;
        printf("Invalid socket type!\n");
    }
    while (1) {
        printf("Enter blocking type (blocking/non-blocking): ");
        scanf("%s", blocking_type);
        if (strcmp(blocking_type, "blocking") == 0 || strcmp(blocking_type, "non-blocking") == 0)
            break;
        printf("Invalid blocking type!\n");
    }
    while (1) {
        printf("Enter packet size (in bytes): ");
        scanf("%d", &packet_size);
        if (packet_size > 0)
            break;
        printf("Invalid packet size!\n");
    }
    while (1) {
        printf("Enter number of packets: ");
        scanf("%d", &num_packets);
        if (num_packets > 0)
            break;
        printf("Invalid number of packets!\n");
    }

    // Ініціалізуємо параметри клієнта
    client_params params;
    strcpy(params.socket_type, socket_type);
    strcpy(params.blocking_type, blocking_type);
    params.packet_size = packet_size;
    params.num_packets = num_packets;

    // Масив потоків
    pthread_t threads[client_number];
    client_params* results[client_number]; // для зберігання результатів кожного клієнта

    double total_connection_time = 0;

    // Створюємо потік для кожного клієнта
    for (int i = 0; i < client_number; i++) {
        client_params* client_data = (client_params*)malloc(sizeof(client_params));
        *client_data = params;
        if (pthread_create(&threads[i], NULL, run_client, client_data) != 0) {
            perror("Thread creation failed");
            free(client_data);
        }
    }

    // Очікуємо завершення всіх потоків
    for (int i = 0; i < client_number; i++) {
        pthread_join(threads[i], (void**)&results[i]);
        total_connection_time += results[i]->connection_time;
        free(results[i]);
    }

    double avg_connection_time = total_connection_time / client_number;
    printf("Average connection time: %f seconds\n", avg_connection_time);

    printf("All clients have finished.\n");
    return 0;
}