#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lib/tdns/tdns-c.h"

/* A few macros that might be useful */
/* Feel free to add macros you want */
#define DNS_PORT 53
#define BUFFER_SIZE 2048



int main() {
    /* A few variable declarations that might be useful */
    /* You can add anything you want */
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];


    /* PART 1 TODO: Implement a DNS nameserver for the utexas.edu zone */

    /* 1. Create a UDP socket. */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    /* 2. Initialize the server address (INADDR_ANY, DNS_PORT) */
    /*    and bind the socket to this address. */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind socket");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 3. Initialize a TDNS server context using TDNSInit(). */
    /*    This context will be used for all subsequent TDNS library calls. */
    struct TDNSServerContext *ctx = TDNSInit();
    if (!ctx) {
        perror("Failed to initialize TDNS context");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* 4. Create the utexas.edu zone with TDNSCreateZone(). */
    TDNSCreateZone(ctx, "utexas.edu");
    /*    - Add an A record for www.utexas.edu using TDNSAddRecord(). */
    TDNSAddRecord(ctx, "utexas.edu", "www", "128.83.120.1", NULL);
    /*    - Add an NS record for the UTCS nameserver ns.cs.utexas.edu. */
    TDNSAddRecord(ctx, "utexas.edu", "cs", NULL, "ns.cs.utexas.edu");
    /*    - Add an A record for ns.cs.utexas.edu. */
    TDNSAddRecord(ctx, "cs.utexas.edu", "ns", "128.83.144.1", NULL);
   


    /* 5. Enter a loop to receive incoming DNS messages */
    /*    and parse each message using TDNSParseMsg(). */
    while (1) {
        ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("Failed to receive data");
            continue;
        }

        struct TDNSParseResult response;
        uint8_t msg_type = TDNSParseMsg(buffer, recv_len, &response);
        if (msg_type == TDNS_QUERY) {
            /* This is a query message. */
            /* You can use the information in the 'response' struct to handle the query. */
            /* For example, you can check response.qname, response.qtype, etc. */

             /* 6. If the message is a query for A, AAAA, or NS records: */
            if (response.qtype == A || response.qtype == AAAA || response.qtype == NS) {
            /*      - Look up the record with TDNSFind() and send a response. */
            /*    Otherwise, ignore the message. */

                struct TDNSFindResult result;
                if (TDNSFind(ctx, &response, &result)) {
                    /* Send the response back to the client */
                    ssize_t sent_len = sendto(sockfd, result.serialized, result.len, 0, (struct sockaddr *)&client_addr, client_len);
                    if (sent_len < 0) {
                        perror("Failed to send response");
                    }
                } else {
                    perror("Record not found");
                }
            }
        }
    }
    close(sockfd);
    return 0;
}
