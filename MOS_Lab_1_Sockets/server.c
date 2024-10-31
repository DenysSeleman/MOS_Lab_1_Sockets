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

#define UNIX_SOCKET_PATH "/tmp/socket"
#define PORT 8080

// Функція для вимірювання часу
double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1E9;
}

typedef struct {
    int packets_received;
    int bytes_received;
} client_result_t;

typedef struct {
    int client_fd;
    int packet_size;
    char* buffer;
} client_data_t;

// Функція для читання даних із клієнта
void client_handler(client_data_t* client_data, int write_fd) {
    int client_fd = client_data->client_fd;
    char* buffer = client_data->buffer;
    int packet_size = client_data->packet_size;

    client_result_t result = { 0, 0 };
    ssize_t bytes_read;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Таймаут для завершального сигналу
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);

        int activity = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            perror("Select error on client socket");
            break;
        }
        else if (activity == 0) {
            // Завершення прийому даних
            break;
        }

        // Читаємо дані від клієнта
        bytes_read = read(client_fd, buffer, packet_size);
        if (bytes_read > 0) {
            result.bytes_received += bytes_read;
            result.packets_received++;
        }
        else if (bytes_read == 0) {
            // Клієнт закрив з'єднання
            break;
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Read error");
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_time = get_time_diff(start, end);
    //printf("Client throughput: %f packets/second, %f bytes/second\n",
    //    result.packets_received / elapsed_time, result.bytes_received / elapsed_time);

    close(client_fd);
    free(client_data->buffer);

    // Передаємо результат у головний процес через канал
    write(write_fd, &result, sizeof(client_result_t));
    close(write_fd); // Закриваємо дескриптор каналу для запису
}

int main() {
    char socket_type[5], blocking_type[15], sync_type[10];
    int server_fd, client_fd;
    struct sockaddr_in inet_addr;
    struct sockaddr_un unix_addr;
    int addrlen, packet_size, client_limit = 10;
    struct timespec start, end, start_time, end_time;
    int is_first_client = 1; // Позначає, що перший клієнт ще не оброблений

    int total_packets = 0; // Загальна кількість пакетів від усіх клієнтів
    int total_bytes = 0; // Загальна кількість байтів від усіх клієнтів

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
        printf("Enter sync type (sync/async): ");
        scanf("%s", sync_type);
        if (strcmp(sync_type, "sync") == 0 || strcmp(sync_type, "async") == 0)
            break;
        printf("Invalid sync type!\n");
    }
    while (1) {
        printf("Enter packet size (in bytes): ");
        scanf("%d", &packet_size);
        if (packet_size > 0)
            break;
        printf("Invalid packet size!\n");
    }

    char buffer[packet_size];

    // Вибір типу сокета
    if (strcmp(socket_type, "INET") == 0) {
        addrlen = sizeof(inet_addr);
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        inet_addr.sin_family = AF_INET;
        inet_addr.sin_addr.s_addr = INADDR_ANY;
        inet_addr.sin_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr*)&inet_addr, sizeof(inet_addr)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }
    }
    else {
        addrlen = sizeof(unix_addr);
        if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        unix_addr.sun_family = AF_UNIX;
        strcpy(unix_addr.sun_path, UNIX_SOCKET_PATH);
        unlink(UNIX_SOCKET_PATH);

        if (bind(server_fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }
    }

    // Встановлюємо неблокуючий режим, якщо він був вказаний
    if (strcmp(blocking_type, "non-blocking") == 0) {
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    }

    // Початок прослуховування
    if (listen(server_fd, client_limit * 2) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening...\n");

    int clients_handled = 0;

    // Створюємо канал
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Pipe failed");
        exit(EXIT_FAILURE);
    }

    if (strcmp(sync_type, "async") == 0) {
        // Асинхронний режим
        fd_set read_fds;
        struct timeval timeout;

        while (clients_handled < client_limit) {
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);

            timeout.tv_sec = 5; // Час очікування на підключення
            timeout.tv_usec = 0;

            int activity = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
            if (activity < 0 && errno != EINTR) {
                perror("Select error");
                exit(EXIT_FAILURE);
            }

            if (FD_ISSET(server_fd, &read_fds)) {
                if (strcmp(socket_type, "INET") == 0)
                    client_fd = accept(server_fd, (struct sockaddr*)&inet_addr, &addrlen);
                else
                    client_fd = accept(server_fd, (struct sockaddr*)&unix_addr, &addrlen);

                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // Продовжуємо, якщо ресурс тимчасово недоступний
                    perror("Accept failed");
                    continue;
                }

                // Встановлюємо для клієнтського сокета неблокуючий режим, якщо він був вказаний
                if (strcmp(blocking_type, "non-blocking") == 0) {
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                }

                if (is_first_client) {
                    clock_gettime(CLOCK_MONOTONIC, &start_time);
                    is_first_client = 0;
                }

                pid_t pid = fork();
                if (pid == 0) {
                    // Дочірній процес
                    close(server_fd);
                    close(pipe_fd[0]); // Закриваємо читання каналу в дочірньому процесі

                    client_data_t* client_data = (client_data_t*)malloc(sizeof(client_data_t));
                    client_data->client_fd = client_fd;
                    client_data->packet_size = packet_size;
                    client_data->buffer = (char*)malloc(packet_size);

                    client_handler(client_data, pipe_fd[1]);

                    exit(0); // Завершуємо процес після обробки клієнта
                }
                else if (pid > 0) {
                    // Батьківський процес
                    close(client_fd);  // Закриваємо дескриптор клієнта в головному процесі
                    close(pipe_fd[1]);  // Закриваємо запис каналу у головному процесі
                    clients_handled++;

                    client_result_t result;
                    read(pipe_fd[0], &result, sizeof(client_result_t));
                    total_packets += result.packets_received;
                    total_bytes += result.bytes_received;
                }
                else {
                    perror("Fork failed");
                }
            }
        }

        // Очікуємо завершення всіх дочірніх процесів
        while (wait(NULL) > 0);

        clock_gettime(CLOCK_MONOTONIC, &end_time);
    }
    else {
        // Синхронний режим
        fd_set read_fds;

        while (clients_handled < client_limit) {
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);

            int activity = select(server_fd + 1, &read_fds, NULL, NULL, NULL); // Очікуємо без таймауту
            if (activity < 0 && errno != EINTR) {
                perror("Select error");
                exit(EXIT_FAILURE);
            }

            if (FD_ISSET(server_fd, &read_fds)) {
                // Приймаємо нового клієнта
                if (strcmp(socket_type, "INET") == 0)
                    client_fd = accept(server_fd, (struct sockaddr*)&inet_addr, &addrlen);
                else
                    client_fd = accept(server_fd, (struct sockaddr*)&unix_addr, &addrlen);

                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // Продовжуємо, якщо ресурс тимчасово недоступний
                    perror("Accept failed");
                    continue;
                }

                // Встановлюємо для клієнтського сокета неблокуючий режим, якщо він був вказаний
                if (strcmp(blocking_type, "non-blocking") == 0) {
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                }

                if (is_first_client) {
                    clock_gettime(CLOCK_MONOTONIC, &start_time);
                    is_first_client = 0;
                }

                client_data_t* client_data = (client_data_t*)malloc(sizeof(client_data_t));
                client_data->client_fd = client_fd;
                client_data->packet_size = packet_size;
                client_data->buffer = (char*)malloc(packet_size);

                // Викликаємо client_handler без створення процесу
                client_handler(client_data, pipe_fd[1]);

                client_result_t result;
                read(pipe_fd[0], &result, sizeof(client_result_t));
                total_packets += result.packets_received;
                total_bytes += result.bytes_received;

                clock_gettime(CLOCK_MONOTONIC, &end_time);

                clients_handled++;
            }
        }
    }

    double total_time = get_time_diff(start_time, end_time);
    printf("Total time: %f seconds\n", total_time);
    printf("Total packets: %d\n", total_packets);
    printf("Total bytes: %d\n", total_bytes);
    printf("Throughput: %f packets/second, %f bytes/second\n", total_packets / total_time, total_bytes / total_time);

    close(server_fd);
    unlink(UNIX_SOCKET_PATH);
    printf("Server handled %d clients and is now closing.\n", client_limit);
    return 0;
}