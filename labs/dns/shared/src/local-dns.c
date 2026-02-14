#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lib/tdns/tdns-c.h"

/* DNS header structure */
struct dnsheader
{
        uint16_t id; /* query identification number */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        /* fields in third byte */
        unsigned qr : 1;     /* response flag */
        unsigned opcode : 4; /* purpose of message */
        unsigned aa : 1;     /* authoritative answer */
        unsigned tc : 1;     /* truncated message */
        unsigned rd : 1;     /* recursion desired */
                             /* fields in fourth byte */
        unsigned ra : 1;     /* recursion available */
        unsigned unused : 1; /* unused bits (MBZ as of 4.9.3a3) */
        unsigned ad : 1;     /* authentic data from named */
        unsigned cd : 1;     /* checking disabled by resolver */
        unsigned rcode : 4;  /* response code */
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        /* fields in third byte */
        unsigned rd : 1;     /* recursion desired */
        unsigned tc : 1;     /* truncated message */
        unsigned aa : 1;     /* authoritative answer */
        unsigned opcode : 4; /* purpose of message */
        unsigned qr : 1;     /* response flag */
                             /* fields in fourth byte */
        unsigned rcode : 4;  /* response code */
        unsigned cd : 1;     /* checking disabled by resolver */
        unsigned ad : 1;     /* authentic data from named */
        unsigned unused : 1; /* unused bits (MBZ as of 4.9.3a3) */
        unsigned ra : 1;     /* recursion available */
#endif
        /* remaining bytes */
        uint16_t qdcount; /* number of question records */
        uint16_t ancount; /* number of answer records */
        uint16_t nscount; /* number of authority records */
        uint16_t arcount; /* number of resource records */
};

/* A few macros that might be useful */
/* Feel free to add macros you want */
#define DNS_PORT 53
#define BUFFER_SIZE 2048

int main() {
        /* A few variable declarations that might be useful */
        /* You can add anything you want */
        int sockfd;
        struct sockaddr_in server_addr, client_addr, ns_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[BUFFER_SIZE];
        char query_buffer[BUFFER_SIZE];

        /* PART 2 TODO: Implement a local iterative DNS server */

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
        /*    This context will be used for all TDNS library calls. */
        struct TDNSServerContext *ctx = TDNSInit();
        if (!ctx) {
                perror("Failed to initialize TDNS context");
                close(sockfd);
                exit(EXIT_FAILURE);
        }

        /* 4. Create the edu zone with TDNSCreateZone(). */
        TDNSCreateZone(ctx, "edu");
        /*    - Add the UT nameserver ns.utexas.edu using TDNSAddRecord(). */
        TDNSAddRecord(ctx, "edu", "utexas", NULL, "ns.utexas.edu");
        /*    - Add an A record for ns.utexas.edu using TDNSAddRecord(). */
        TDNSAddRecord(ctx, "utexas.edu", "ns", "40.0.0.20", NULL);

        /* 5. Continuously receive incoming DNS messages */
        /*    and parse them using TDNSParseMsg(). */
        while (1) {
                ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
                if (recv_len < 0) {
                        perror("Failed to receive data");
                        continue;
                }

                struct TDNSParseResult response;
                uint8_t msg = TDNSParseMsg(buffer, recv_len, &response);

                /* 6. If the message is a query for A, AAAA, or NS records: */
                /*      - Look up the record using TDNSFind(). */
                /*      - Ignore all other query types. */
                if (msg == TDNS_QUERY) {
                        if (response.qtype == A || response.qtype == AAAA || response.qtype == NS) {
                                struct TDNSFindResult result;
                                uint8_t found = TDNSFind(ctx, &response, &result);

                                /*      a. If the record exists and indicates delegation: */
                                if (found && response.nsIP != NULL) {
                                        /*         - Send an iterative query to the delegated nameserver. */
                                        putAddrQID(ctx, response.dh->id, &client_addr);
                                        /*         - Store per-query context using putAddrQID() and putNSQID() */
                                        /*           for future response handling. */
                                        putNSQID(ctx, response.dh->id, strdup(response.nsIP), strdup(response.nsDomain));

                                        // nameserver set up
                                        memset(&ns_addr, 0, sizeof(ns_addr));
                                        ns_addr.sin_family = AF_INET;
                                        ns_addr.sin_port = htons(DNS_PORT);
                                        inet_pton(AF_INET, response.nsIP, &ns_addr.sin_addr);

                                        ssize_t sent_len = sendto(sockfd, buffer, recv_len, 0, (struct sockaddr *)&ns_addr, sizeof(ns_addr));
                                        if (sent_len < 0) {
                                                perror("Failed to send query to delegated nameserver");
                                        }
                                } else if (found) {
                                        /*      b. If the record exists and does NOT indicate delegation: */
                                        /*         - Send a direct response back to the client. */
                                        ssize_t sent_len = sendto(sockfd, result.serialized, result.len, 0, (struct sockaddr *)&client_addr, client_len);
                                        if (sent_len < 0) {
                                                perror("Failed to send response to client");
                                        }
                                } else {
                                        /*      c. If the record does NOT exist: */
                                        /*         - Send an empty response back to the client. */
                                        sendto(sockfd, result.serialized, result.len, 0, (struct sockaddr *)&client_addr, client_len);
                                }
                        }
                }
                else if (msg == TDNS_RESPONSE) {

                        /* 7-1. If the message is an authoritative response (i.e., contains an answer): */
                        /*      - Retrieve the original client address and NS info using */
                        if(response.dh->aa) {
                                const char *nsIP;
                                const char *nsDomain;
                                getNSbyQID(ctx, response.dh->id, &nsIP, &nsDomain);

                                /*      - Add the NS information to the response using TDNSPutNStoMessage(). */
                                uint64_t new_len = TDNSPutNStoMessage(buffer, recv_len, &response, nsIP, nsDomain);

                                /*      - Retrieve the original client address and NS info using */
                                /*        getAddrbyQID() and getNSbyQID(). */
                                struct sockaddr_in original_client_addr;
                                getAddrbyQID(ctx, response.dh->id, &original_client_addr);
                                /*      - Send the completed response to the client. */
                                ssize_t sent_len = sendto(sockfd, buffer, new_len, 0, (struct sockaddr *)&original_client_addr, sizeof(original_client_addr));
                                if (sent_len < 0) {
                                        perror("Failed to send response to client");
                                }
                                /*      - Delete the per-query context using delAddrQID() and delNSQID(). */
                                delAddrQID(ctx, response.dh->id);
                                delNSQID(ctx, response.dh->id);
                        }  else {
                                /* 7-2. If the message is a referral response (i.e., does NOT contain an answer but contains NS info): */
                                /*      - Extract the iterative query from the original query using TDNSGetIterQuery(). */
                                printf("DEBUG: Non-authoritative response, nsIP=%s, nsDomain=%s\n", 
                                response.nsIP ? response.nsIP : "NULL",
                                response.nsDomain ? response.nsDomain : "NULL");
        
                                
                                ssize_t iter_query_len = TDNSGetIterQuery(&response, query_buffer);
                                printf("DEBUG: Extracted query, length=%ld\n", iter_query_len);

                                if (iter_query_len < 0) {
                                        perror("Failed to extract iterative query");
                                }
                                
                                memset(&ns_addr, 0, sizeof(ns_addr));
                                ns_addr.sin_family = AF_INET;
                                ns_addr.sin_port = htons(DNS_PORT);
                                inet_pton(AF_INET, response.nsIP, &ns_addr.sin_addr);

                                printf("DEBUG: Forwarding to %s:%d\n", response.nsIP, DNS_PORT);


                                /*         - Forward the iterative query to the referred nameserver. */
                                ssize_t sent_len = sendto(sockfd, query_buffer, iter_query_len, 0, (struct sockaddr *)&ns_addr, sizeof(ns_addr));
                                printf("DEBUG: sendto returned %ld\n", sent_len);

                                if (sent_len < 0) {
                                        perror("Failed to send iterative query to referred nameserver");
                                }
                                /*         - Update the per-query context using putNSQID(). */
                                putNSQID(ctx, response.dh->id, strdup(response.nsIP), strdup(response.nsDomain));
                                
                        }
                }
        }

        close(sockfd);
        return 0;
}
