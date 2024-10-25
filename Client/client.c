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
    int sockfd;
    struct sockaddr_in inet_addr;
    struct sockaddr_un unix_addr;
    char* buffer;
    int packet_size, num_packets;
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
    printf("Enter number of packets: ");
    scanf("%d", &num_packets);

    // Виділяємо буфер для пакету
    buffer = (char*)malloc(packet_size);
    memset(buffer, 'A', packet_size); // Заповнюємо буфер символами

    // Вибір типу сокета
    if (strcmp(socket_type, "INET") == 0) {
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        inet_addr.sin_family = AF_INET;
        inet_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, "127.0.0.1", &inet_addr.sin_addr) <= 0) {
            printf("Invalid address\n");
            return -1;
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
        if (connect(sockfd, (struct sockaddr*)&inet_addr, sizeof(inet_addr)) < 0) {
            perror("Connection failed");
            exit(EXIT_FAILURE);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);

    }
    else if (strcmp(socket_type, "UNIX") == 0) {
        if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        unix_addr.sun_family = AF_UNIX;
        strcpy(unix_addr.sun_path, UNIX_SOCKET_PATH);

        clock_gettime(CLOCK_MONOTONIC, &start);
        if (connect(sockfd, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) < 0) {
            perror("Connection failed");
            exit(EXIT_FAILURE);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
    }
    else {
        printf("Invalid socket type!\n");
        exit(EXIT_FAILURE);
    }

    printf("Connection time: %f seconds\n", get_time_diff(start, end));

    // Відправка пакетів
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < num_packets; i++) {
        send(sockfd, buffer, packet_size, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = get_time_diff(start, end);
    printf("Time taken: %f seconds\n", elapsed);
    printf("Throughput: %f packets/second, %f bytes/second\n", num_packets / elapsed, (num_packets * packet_size) / elapsed);

    free(buffer);
    close(sockfd);

    return 0;
}
