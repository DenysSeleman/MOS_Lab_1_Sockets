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

#define UNIX_SOCKET_PATH "/tmp/socket"
#define PORT 8080

// Функція для вимірювання часу
double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1E9;
}

int main() {
    char socket_type[5], blocking_type[15], sync_type[10];
    int server_fd, client_fd;
    struct sockaddr_in inet_addr;
    struct sockaddr_un unix_addr;
    int opt = 1;
    int addrlen, packet_size;
    struct timespec start, end;

    // Отримуємо від користувача параметри
    printf("Enter socket type (UNIX/INET): ");
    scanf("%s", socket_type);
    printf("Blocking or non-blocking (blocking/non-blocking): ");
    scanf("%s", blocking_type);
    printf("Sync or async (sync/async): ");
    scanf("%s", sync_type);
    printf("Enter packet size (in bytes): ");
    scanf("%d", &packet_size);

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
    else if (strcmp(socket_type, "UNIX") == 0) {
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
    else {
        printf("Invalid socket type!\n");
        exit(EXIT_FAILURE);
    }

    // Встановлюємо блокуючий або неблокуючий режим
    if (strcmp(blocking_type, "non-blocking") == 0) {
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    }

    // Початок прослуховування
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening...\n");


    /*
    if ((client_fd = accept(server_fd, (struct sockaddr*)&inet_addr, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    */

    if (strcmp(socket_type, "INET") == 0)
        client_fd = accept(server_fd, (struct sockaddr*)&inet_addr, &addrlen);
    else if (strcmp(socket_type, "UNIX") == 0)
        client_fd = accept(server_fd, (struct sockaddr*)&unix_addr, &addrlen);

    if (client_fd < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }



    // Отримуємо пакети та вимірюємо швидкодію
    int total_bytes = 0, packets = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (read(client_fd, buffer, packet_size) > 0) {
        total_bytes += packet_size;
        packets++;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = get_time_diff(start, end);
    printf("Total data received: %d bytes\n", total_bytes);
    printf("Total packets received: %d\n", packets);
    printf("Time taken: %f seconds\n", elapsed);
    printf("Throughput: %f packets/second, %f bytes/second\n", packets / elapsed, total_bytes / elapsed);

    close(client_fd);
    close(server_fd);
    unlink(UNIX_SOCKET_PATH);

    return 0;
}
