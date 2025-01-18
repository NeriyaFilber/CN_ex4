#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip_icmp.h>
#include <netinet/in.h>
#include <time.h>


#define MAX_HOPS 30
#define TIMEOUT 1
#define PACKET_SIZE 64

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }

    if (len == 1) {
        sum += *(unsigned char *)buf;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

void traceroute(const char *target_ip) {
    int sockfd;
    struct sockaddr_in addr;
    struct icmphdr icmp_hdr;
    char packet[PACKET_SIZE];
    struct timeval timeout = {TIMEOUT, 0};
    int found =0;

    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, target_ip, &addr.sin_addr);

    printf("Traceroute to %s:\n", target_ip);

    for (int ttl = 1; ttl <= MAX_HOPS; ttl++) {
        if (found) {
            break;
        }
            if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
                perror("setsockopt");
                close(sockfd);
                exit(EXIT_FAILURE);
            }


        memset(&icmp_hdr, 0, sizeof(icmp_hdr));
        icmp_hdr.type = ICMP_ECHO;
        icmp_hdr.un.echo.id = getpid();
        icmp_hdr.un.echo.sequence = ttl;
        icmp_hdr.checksum = checksum(&icmp_hdr, sizeof(icmp_hdr));

        memset(packet, 0, PACKET_SIZE);
        memcpy(packet, &icmp_hdr, sizeof(icmp_hdr));

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int num =0 ; num<3; num ++) {
            if (sendto(sockfd, packet, sizeof(icmp_hdr), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                perror("sendto");
                close(sockfd);
                exit(EXIT_FAILURE);
            }


            char recv_buffer[PACKET_SIZE];
            struct sockaddr_in recv_addr;
            socklen_t addr_len = sizeof(recv_addr);
            int bytes_received = recvfrom(sockfd, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&recv_addr, &addr_len);
            clock_gettime(CLOCK_MONOTONIC, &end);

            if (bytes_received > 0) {
                double elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
                char recv_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &recv_addr.sin_addr, recv_ip, sizeof(recv_ip));
                if (num ==0) {
                    printf("%2d  %s  %.3f ms  ", ttl, recv_ip, elapsed_time);
                }
                else {
                    printf("%.3fms  ",elapsed_time);
                    if (num ==2) {
                        printf("\n");
                    }
                }
                if (strcmp(recv_ip, target_ip) == 0) {
                    found =1;

                }
            } else {
                if (num==0){
                    printf("%2d  *\t  ", ttl);
                }
                else if (num == 1){
                    printf("*\t  ");
                }
                else {
                    printf("*\n");
                }
            }
        }
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-a") != 0) {
        fprintf(stderr, "Usage: %s -a <target_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (geteuid() != 0) {
        fprintf(stderr, "This program must be run as root.\n");
        exit(EXIT_FAILURE);
    }

    traceroute(argv[2]);
    return 0;
}