//
// Created by 尾崎耀一 on 2019-01-13.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "mydhcp.h"

#define BUFSIZE     256
#define IPADDRLEN   128

#define ALARM_INTERVAL  1

struct client client_list_head;             // client list head
struct address_pool address_pool_head;      // address pool head
struct address_pool address_using_head;     // address in-use head
uint16_t ttl;                               // TTL
struct client *recv_timeout_client;         // pointer to the client whose REQUEST has timed out
struct client *ttl_timeout_client;          // pointer to the clinet whose IP address's ttl has timed out

void init_address_pool(char *);
void insert_address(struct address_pool *, struct address_pool *);
struct address_pool *search_address_entry(struct address_pool *, struct in_addr);
void delete_address_entry(struct address_pool *);
struct address_pool *allocate_address();
void recall_address(struct client *);
int wait_event(int, struct sockaddr_in *, struct message *, int);
struct client *create_new_client(struct sockaddr_in *);

void alarm_proc();  // SIGALRM handler

void dump_message(struct message *);
void print_clients_list();
void print_address_pool();
void print_address_using();

void insert_client(struct client *);
struct client *search_client_entry(struct in_addr, in_port_t);
void delete_client_entry(struct client *);

/* ---------------------- proc functions ---------------------- */
void send_offer_proc(int, struct client *, struct message *, struct sockaddr *);
void recv_alloc_request_proc(int, struct client *, struct message *, struct sockaddr *);
void request_timed_out_proc(int, struct client *, struct message *, struct sockaddr *);
void delete_client_proc(int, struct client *, struct message *, struct sockaddr *);
void recv_ext_request_proc(int, struct client *, struct message *, struct sockaddr *);
void request_timed_out_error_proc(int, struct client *, struct message *, struct sockaddr *);
void recv_release_proc(int, struct client *, struct message *, struct sockaddr *);
void ttl_timed_out_proc(int, struct client *, struct message *, struct sockaddr *);

/* ---------------------- helper functions ---------------------- */
void send_offer(int, uint8_t, struct client *, struct sockaddr *);
void send_ack(int, uint8_t, struct client *, struct sockaddr *);
void send_message(int, uint8_t, uint8_t, struct client *, struct sockaddr *);
int request_parameter_check(struct message *, struct client *);

struct proc_table {
    int state;
    int event;
    void (*func)(int, struct client *, struct message *, struct sockaddr *);
} table[] = {
        {S_STATE_INIT,              S_EVENT_RECV_DISCOVER,          send_offer_proc             }, // 1
        {S_STATE_WAIT_REQUEST,      S_EVENT_RECV_ALLOC_REQUEST,     recv_alloc_request_proc     }, // 2, 8
        {S_STATE_WAIT_REQUEST,      S_EVENT_REQUEST_TIMEOUT,        request_timed_out_proc      }, // 5
        {S_STATE_WAIT_REQUEST,      S_EVENT_RECV_UNKNOWN,           delete_client_proc          }, // 9
        {S_STATE_INUSE,             S_EVENT_RECV_EXT_REQUEST,       recv_ext_request_proc       }, // 3, 11
        {S_STATE_INUSE,             S_EVENT_RECV_RELEASE,           recv_release_proc           }, // 4, 7
        {S_STATE_INUSE,             S_EVENT_TTL_TIMEOUT,            ttl_timed_out_proc          }, // 4
        {S_STATE_INUSE,             S_EVENT_RECV_UNKNOWN,           delete_client_proc          }, // 12
        {S_STATE_REQUEST_TIMEOUT,   S_EVENT_RECV_ALLOC_REQUEST,     recv_alloc_request_proc     }, // 6, 8
        {S_STATE_REQUEST_TIMEOUT,   S_EVENT_REQUEST_TIMEOUT,        request_timed_out_error_proc}, // 15
        {S_STATE_REQUEST_TIMEOUT,   S_EVENT_RECV_UNKNOWN,           delete_client_proc          }, // 16
        {S_STATE_WAIT_REQUEST,      S_EVENT_DEFAULT,                delete_client_proc          }, // 10
        {S_STATE_INUSE,             S_EVENT_DEFAULT,                delete_client_proc          }, // 13
        {S_STATE_REQUEST_TIMEOUT,   S_EVENT_DEFAULT,                delete_client_proc          }, // 17
        {0,                         0,                              NULL                        }
};

struct message received_message;

int
main(int argc, char *argv[])
{
    int s, event;
    struct sockaddr_in server_socket_address, client_socket_address;
    struct client *client_pointer;
    struct proc_table *pointer_to_table;
    struct sigaction act;
    struct itimerval tm;

    if (argc != 2) {
        fprintf(stderr, "Usage: mydhcpd <configuration file>\n");
        exit(EXIT_FAILURE);
    }

    // prepare clients list
    client_list_head.fp = &client_list_head;
    client_list_head.bp = &client_list_head;

    // prepare address pool
    address_pool_head.fp = &address_pool_head;
    address_pool_head.bp = &address_pool_head;

    // prepare in-use address list
    address_using_head.fp = &address_using_head;
    address_using_head.bp = &address_using_head;

    // read configuration file and set
    init_address_pool(argv[1]);

    // create server's socket
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket @ creating server's socket");
        exit(EXIT_FAILURE);
    }

    // prepare server's socket address
    memset(&server_socket_address, 0, sizeof(server_socket_address));
    server_socket_address.sin_family = AF_INET;
    server_socket_address.sin_port = htons(MYDHCP_PORT);
    server_socket_address.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind server's socket and its address
    if (bind(s, (struct sockaddr *)&server_socket_address, sizeof(server_socket_address)) < 0) {
        perror("bind @ binding server's socket and its address");
        exit(EXIT_FAILURE);
    }

    // set SIGALRM handler
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarm_proc;
    if (sigaction(SIGALRM, &act, NULL) < 0) {
        perror("sigaction @ setting SIGALRM handler");
        exit(EXIT_FAILURE);
    }

    // set SIGALRM interval
    recv_timeout_client = NULL;
    ttl_timeout_client = NULL;
    tm.it_interval.tv_sec = ALARM_INTERVAL;
    tm.it_value.tv_sec = ALARM_INTERVAL;
    tm.it_interval.tv_usec = 0;
    tm.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &tm, NULL) < 0) {
        perror("setitimer @ setting SIGALRM interval");
        exit(EXIT_FAILURE);
    }

    // main loop
    for (;;) {

        // wait event
        fprintf(stderr, "\nwaiting for any event...\n");
        if ((event = wait_event(s, &client_socket_address, &received_message, sizeof(received_message))) < 0) {
            fprintf(stderr, "ERROR: illegal event happened\n");
            continue;
        }
        // fprintf(stderr, "EVENT: %s\n", SERVER_EVENT_NAME[event]);

        // search for corresponding client
        if (recv_timeout_client) {
            client_pointer = recv_timeout_client;
            recv_timeout_client = NULL;
        } else if (ttl_timeout_client) {
            client_pointer = ttl_timeout_client;
            ttl_timeout_client = NULL;
        } else if ((client_pointer =
                search_client_entry(client_socket_address.sin_addr, client_socket_address.sin_port))
                == NULL) {
            client_pointer = create_new_client(&client_socket_address);
        }

        // select appropriate state transition function
        for (pointer_to_table = table; pointer_to_table->state; pointer_to_table++) {
            if (pointer_to_table->state == client_pointer->state && pointer_to_table->event == event) {
                (*pointer_to_table->func)(s, client_pointer, &received_message, (struct sockaddr *)&client_socket_address);
                break;
            }
            if (pointer_to_table->state == client_pointer->state && pointer_to_table->event == S_EVENT_DEFAULT) {
                fprintf(stderr, "ERROR: unexpected event happened: state = %s, event = %s\n",
                        SERVER_STATES_NAME[client_pointer->state], SERVER_EVENT_NAME[event]);
                break;
            }
        }
    }
}

void
send_offer_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    struct address_pool *ap;
    if ((ap = allocate_address()) != NULL) {
        client->ipaddr.s_addr = ap->ipaddr.s_addr;
        client->netmask.s_addr = ap->netmask.s_addr;
        client->ttl = htons(ttl);
        client->timeout_counter = RECV_TIMEOUT;
        send_offer(socket, CODE_SUCCUESS, client, client_socket_address);   // send OFFER(SUCCESS)
        client->state = S_STATE_WAIT_REQUEST;   // state transition
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> %s #\n", SERVER_STATES_NAME[before], SERVER_STATES_NAME[client->state]);
        fprintf(stderr, "#####################################################################################################\n");
    } else {
        fprintf(stderr, "ERROR: no IP address available in address pool\n");
        send_offer(socket, CODE_NOIPADDR, client, client_socket_address);   // send OFFER(NOIPADDR)
        delete_client_entry(client);
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
        fprintf(stderr, "#####################################################################################################\n");
        free(client);
    }
}

void
recv_alloc_request_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    if (request_parameter_check(message, client) == 0) {
        send_ack(socket, CODE_SUCCUESS, client, client_socket_address); // send ACK(OK)
        client->ttl_counter = ntohs(client->ttl);
        client->state = S_STATE_INUSE;  // state transition
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> %s #\n", SERVER_STATES_NAME[before], SERVER_STATES_NAME[client->state]);
        fprintf(stderr, "#####################################################################################################\n");
    } else {
        send_ack(socket, CODE_PARMERR, client, client_socket_address);  // send ACK(NG)
        recall_address(client);
        delete_client_entry(client);
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
        fprintf(stderr, "#####################################################################################################\n");
        free(client);
    }
}

void request_timed_out_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    send_offer(socket, CODE_SUCCUESS, client, client_socket_address);   // resend OFFER(SUCCESS)
    client->state = S_STATE_REQUEST_TIMEOUT;    // state transition
    fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\t%s --> %s #\n", SERVER_STATES_NAME[before], SERVER_STATES_NAME[client->state]);
    fprintf(stderr, "#####################################################################################################\n");
}

void delete_client_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t     #\n", SERVER_STATES_NAME[before]);
    fprintf(stderr, "#####################################################################################################\n");
    recall_address(client);
    delete_client_entry(client);
    free(client);
}

void recv_ext_request_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    if (request_parameter_check(message, client) == 0) {
        send_ack(socket, CODE_SUCCUESS, client, client_socket_address); // send ACK(OK)
        client->ttl_counter = ntohs(client->ttl);
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> %s #\n", SERVER_STATES_NAME[before], SERVER_STATES_NAME[client->state]);
        fprintf(stderr, "#####################################################################################################\n");
    } else {
        send_ack(socket, CODE_PARMERR, client, client_socket_address);  // semd ACK(PARAMERR)
        fprintf(stderr, "## client[%s] ##########################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
        fprintf(stderr, "#####################################################################################################\n");
        recall_address(client);
        delete_client_entry(client);
        free(client);
    }
}

void request_timed_out_error_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before = client->state;
    fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
    fprintf(stderr, "#####################################################################################################\n");
    recall_address(client);
    delete_client_entry(client);
    free(client);
}

void recv_release_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before =client->state;
    if (message->ipaddr.s_addr != client->ipaddr.s_addr) {
        fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
        fprintf(stderr, "#\tIP mismatch: using = %s, message: %s #\n", inet_ntoa(client->ipaddr), inet_ntoa(message->ipaddr));
        fprintf(stderr, "#\t%s --> %s #\n", SERVER_STATES_NAME[before], SERVER_STATES_NAME[before]);
        fprintf(stderr, "#####################################################################################################\n");
        return;
    }
    fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
    fprintf(stderr, "#####################################################################################################\n");
    recall_address(client);
    delete_client_entry(client);
    free(client);
}

void ttl_timed_out_proc(int socket, struct client *client, struct message *message, struct sockaddr *client_socket_address)
{
    int before =client->state;
    fprintf(stderr, "## client[%s] ################################################################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\t%s --> TERMINATE\t\t\t\t    #\n", SERVER_STATES_NAME[before]);
    fprintf(stderr, "#####################################################################################################\n");
    recall_address(client);
    delete_client_entry(client);
    free(client);
}

int request_parameter_check(struct message *message, struct client *client)
{
    int err = 0;
    if (message->ipaddr.s_addr != client->ipaddr.s_addr) {
        fprintf(stderr, "ERROR: IP mismatch: using = %s, message: %s\n", inet_ntoa(client->ipaddr), inet_ntoa(message->ipaddr));
        err++;
    }
    if (message->netmask.s_addr != client->netmask.s_addr) {
        fprintf(stderr, "ERROR: netmask mismatch: using = %s, message: %s\n", inet_ntoa(client->netmask), inet_ntoa(message->netmask));
        err++;
    }
    if (message->ttl > client->ttl) {
        fprintf(stderr, "ERROR: illegal TTL in REQUEST: offered = %d, request = %d\n", ntohs(client->ttl), ntohs(message->ttl));
        err++;
    }
    return err;
}

void
send_offer(int socket, uint8_t code, struct client *client, struct sockaddr *client_socket_address)
{
    send_message(socket, TYPE_OFFER, code, client, client_socket_address);
}

void
send_ack(int socket, uint8_t code, struct client *client, struct sockaddr *client_socket_address)
{
    send_message(socket, TYPE_ACK, code, client, client_socket_address);
}

void
send_message(int socket, uint8_t type, uint8_t code, struct client *client, struct sockaddr *client_socket_address)
{
    struct message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.code = code;
    message.ttl = htons(ttl);
    message.ipaddr.s_addr = client->ipaddr.s_addr;
    message.netmask.s_addr = client->netmask.s_addr;
    if (sendto(
            socket,
            &message,
            sizeof(message),
            0,
            client_socket_address,
            sizeof(struct sockaddr)
                    ) < 0) {
        perror("sendto @ send_message");
        exit(EXIT_FAILURE);
    }
    dump_message(&message);
}

void dump_message(struct message *message)
{
    switch (message->type) {
        case TYPE_DISCOVER: // server <- client
            // fprintf(stderr, "receive %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-- [ (<-) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tType:    %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-----------------------------------------------------------------------+\n\n");
            break;
        case TYPE_OFFER:    // server -> client
            fprintf(stderr, "send %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-- [ (->) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tType:      %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tCode:      %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "\t|\tIP:        %s\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "\t|\tNetmask:   %d\t\t\t\t\t\t|\n", message->netmask.s_addr);
            fprintf(stderr, "\t|\tTTL:       %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "\t+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_REQUEST:  // server <- client
            fprintf(stderr, "receive %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-- [ (<-) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tType:    %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tCode:    %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "|\tIP:      %s\t\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "|\tNetmask: %d\t\t\t\t\t\t|\n", message->netmask.s_addr);
            fprintf(stderr, "|\tTTL:     %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_ACK:      // server -> client
            fprintf(stderr, "send %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t+-- [ (->) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tType:      %s\t\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "\t|\tCode:      %s\t\t|\n", MESSAGE_CODE_NAME[message->code]);
            fprintf(stderr, "\t|\tIP:        %s\t\t\t\t\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "\t|\tNetmask:   %d\t\t\t\t\t\t|\n", message->netmask.s_addr);
            fprintf(stderr, "\t|\tTTL:       %d\t\t\t\t\t\t\t|\n", ntohs(message->ttl));
            fprintf(stderr, "\t+-----------------------------------------------------------------------+\n");
            break;
        case TYPE_RELEASE:  // server <- client
            fprintf(stderr, "receive %s\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "+-- [ (<-) %s ] ---------------+\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tType:    %s\t|\n", MESSAGE_TYPE_NAME[message->type]);
            fprintf(stderr, "|\tIP:      %s\t|\n", inet_ntoa(message->ipaddr));
            fprintf(stderr, "+-----------------------------------------------------------------------+\n");
            break;
        default:
            return;
    }
}

void
init_address_pool(char *config)
{
    FILE *fp;
    struct address_pool *ap;
    char buf[BUFSIZE], string_IP[IPADDRLEN], string_netmask[IPADDRLEN];

    if ((fp = fopen(config, "r")) == NULL) {
        perror("fopen @ init_address_pool");
        exit(EXIT_FAILURE);
    }

    if (fgets(buf, sizeof(buf), fp) == NULL) {
        perror("fgets @ init_address_pool");
        exit(EXIT_FAILURE);
    }

    if (sscanf(buf, "%hu", &ttl) != 1) {
        fprintf(stderr, "ERROR: config file: format error");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "\n## config file ##########################\n");
    fprintf(stderr, "# TTL: %d\t\t\t\t#\n", ttl);
    while(fgets(buf, sizeof(buf), fp) != NULL) {
        if ((ap = (struct address_pool *)malloc(sizeof(struct address_pool))) == NULL) {
            perror("malloc @ init_address_pool");
            exit(EXIT_FAILURE);
        }
        if (sscanf(buf, "%s %s", string_IP, string_netmask) != 2) {
            fprintf(stderr, "ERROR: config file: format error: \"%s\"\n", buf);
            exit(EXIT_FAILURE);
        }
        if (inet_aton(string_IP, &(ap->ipaddr)) == 0) {
            fprintf(stderr, "ERROR: config file: address format error: \"%s\"\n", string_IP);
            exit(EXIT_FAILURE);
        }
        if (inet_aton(string_netmask, &(ap->netmask)) == 0) {
            fprintf(stderr, "ERROR: config file: netmask format error: \"%s\"\n", string_netmask);
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "# IP: %s, netmask: %s\t\t#\n", inet_ntoa(ap->ipaddr), inet_ntoa(ap->netmask));
        insert_address(&address_pool_head, ap);
    }
    fprintf(stderr, "#########################################\n");
    if (ferror(fp)) {
        perror("fgets @ init_address_pool");
        exit(EXIT_FAILURE);
    }
    fclose(fp);
}

void
insert_address(struct address_pool *head, struct address_pool *ap)
{
    ap->bp = head->bp;
    ap->fp = head;
    head->bp->fp = ap;
    head->bp = ap;
}

struct address_pool *
search_address_entry(struct address_pool *head, struct in_addr ipaddr)
{
    struct address_pool *ap;
    for (ap = head->fp; ap != head; ap = ap->fp) {
        if (ap->ipaddr.s_addr == ipaddr.s_addr) {
            return ap;
        }
    }
    return NULL;
}

void
delete_address_entry(struct address_pool *ap)
{
    ap->bp->fp = ap->fp;
    ap->fp->bp = ap->bp;
    ap->fp = NULL;
    ap->bp = NULL;
}

struct address_pool *
allocate_address()
{
    struct address_pool *ap;
    if ((ap = address_pool_head.fp) == &address_pool_head) {
        return NULL;
    }
    ap->fp->bp = &address_pool_head;
    address_pool_head.fp = ap->fp;
    ap->fp = NULL;
    ap->bp = NULL;
    insert_address(&address_using_head, ap);
    fprintf(stderr, "IP address allocated: %s, netmask: %s\n", inet_ntoa(ap->ipaddr), inet_ntoa(ap->netmask));
    print_address_pool();
    print_address_using();
    return ap;
}

void
recall_address(struct client *client)
{
    struct address_pool *ap;
    if ((ap = search_address_entry(&address_using_head, client->ipaddr)) == NULL) {
        fprintf(stderr, "ERROR: no such ip address in using list: %s\n", inet_ntoa(client->ipaddr));
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "recall address: %s\n", inet_ntoa(client->ipaddr));
    delete_address_entry(ap);
    insert_address(&address_pool_head, ap);
    print_address_pool();
    print_address_using();
}

int
wait_event(int socket, struct sockaddr_in *client_socket_address, struct message *message, int size)
{
    socklen_t client_socket_len;
    ssize_t receive_counter;
    struct message *_received_message;
    if (recv_timeout_client) {
        recv_timeout_client = NULL;
        return S_EVENT_REQUEST_TIMEOUT;
    }
    if (ttl_timeout_client) {
        ttl_timeout_client = NULL;
        return S_EVENT_TTL_TIMEOUT;
    }
  RECV_AGAIN:
    client_socket_len = sizeof(struct sockaddr_in);
    if ((receive_counter = recvfrom(
            socket,
            &received_message,
            sizeof(received_message),
            0,
            (struct sockaddr *)client_socket_address,
            &client_socket_len))
                < 0) {
        if (errno != EINTR) {
            perror("recvfrom @ wait_event");
            exit(EXIT_FAILURE);
        }
        if (recv_timeout_client) {
            client_socket_address->sin_addr.s_addr = recv_timeout_client->id.s_addr;
            return S_EVENT_REQUEST_TIMEOUT;
        }
        if (ttl_timeout_client) {
            client_socket_address->sin_addr.s_addr = ttl_timeout_client->id.s_addr;
            return S_EVENT_TTL_TIMEOUT;
        }
        goto RECV_AGAIN;
    }
    if (receive_counter != sizeof(struct message)) {
        fprintf(stderr, "ERROR: illegal message size: want = %ld byte, received = %ld bytes\n",
                sizeof(struct message), receive_counter);
        return S_EVENT_DEFAULT;
    }
    fprintf(stderr, "received message from %s(%d)\n",
            inet_ntoa(client_socket_address->sin_addr),
            ntohs(client_socket_address->sin_port));
    dump_message(&received_message);
    _received_message = &received_message;
    switch (_received_message->type) {
        case TYPE_DISCOVER:
            return S_EVENT_RECV_DISCOVER;
        case TYPE_REQUEST:
            if (_received_message->code == CODE_ALLOCREQ) {
                return S_EVENT_RECV_ALLOC_REQUEST;
            }
            if (_received_message->code == CODE_EXTREQ) {
                return S_EVENT_RECV_EXT_REQUEST;
            }
            fprintf(stderr, "ERROR: unknown REQUEST code: %d\n", _received_message->code);
            return S_EVENT_DEFAULT;
        case TYPE_RELEASE:
            return S_EVENT_RECV_RELEASE;
        case TYPE_ACK:
            fprintf(stderr, "ERROR: received unexpected message (ACK)\n");
            return S_EVENT_RECV_UNEXPECTED;
        default:
            fprintf(stderr, "ERROR: unknown message type: %d\n", _received_message->type);
            return S_EVENT_RECV_UNKNOWN;
    }
}

struct client *
create_new_client(struct sockaddr_in *client_socket_address)
{
    struct client *client;
    if ((client = (struct client *)malloc(sizeof(struct client))) == NULL) {
        perror("malloc @ create_new_client");
        exit(EXIT_FAILURE);
    }
    memset(client, 0, sizeof(struct client));
    client->id.s_addr = client_socket_address->sin_addr.s_addr;
    client->port = client_socket_address->sin_port;
    insert_client(client);
    fprintf(stderr, "create new client (ID: %s)\n", inet_ntoa(client_socket_address->sin_addr));
    client->state = S_STATE_INIT;   // state transition
    fprintf(stderr, "## client[%s] #########################################\n", inet_ntoa(client->id));
    fprintf(stderr, "#\tNULL --> %s #\n", SERVER_STATES_NAME[client->state]);
    fprintf(stderr, "##############################################################\n\n");
    return client;
}

void
insert_client(struct client *client)
{
    client->bp = client_list_head.bp;
    client->fp = &client_list_head;
    client_list_head.bp->fp = client;
    client_list_head.bp = client;
    print_clients_list();
}

struct client *
search_client_entry(struct in_addr addr, in_port_t port)
{
    struct client *client;
    for (client = client_list_head.fp; client != &client_list_head; client = client->fp) {
        if (client->id.s_addr == addr.s_addr && client->port == port) {
            return client;
        }
    }
    return NULL;
}

void
delete_client_entry(struct client *client)
{
    client->bp->fp = client->fp;
    client->fp->bp = client->bp;
    client->fp = NULL;
    client->bp = NULL;
    print_clients_list();
}

void
alarm_proc()
{
    struct client *client;
    for (client = client_list_head.fp; client != &client_list_head; client = client->fp) {
        if (client->state == S_STATE_WAIT_REQUEST || client->state == S_STATE_REQUEST_TIMEOUT) {
            client->timeout_counter--;
            if (client->timeout_counter <= 0 && recv_timeout_client == NULL) {
                recv_timeout_client = client;
            }
        }
        if (client->state == S_STATE_INUSE) {
            client->ttl_counter--;
            fprintf(stderr, "client IP: %s, TTL: %d\n", inet_ntoa(client->ipaddr), client->ttl_counter);
            if (client->ttl_counter <= 0 && ttl_timeout_client == NULL) {
                ttl_timeout_client = client;
                fprintf(stderr, "client IP: %s, TTL timed out\n", inet_ntoa(client->ipaddr));
            }
        }
    }
}

void
print_clients_list()
{
    struct client *client;
    int c = 0;
    fprintf(stderr, "## clients ##############################################\n");
    for (client = client_list_head.fp; client != &client_list_head; client = client->fp) {
        fprintf(stderr, "#    [%d] ID: %s, allocated IP: %s\t\t#\n",
                c,
                inet_ntoa(client->id),
                inet_ntoa(client->ipaddr));
        c++;
    }
    fprintf(stderr, "#########################################################\n\n");
}

void
print_address_pool()
{
    struct address_pool *ap;
    int c = 0;
    fprintf(stderr, "## address pool #########################\n");
    for (ap = address_pool_head.fp; ap != &address_pool_head; ap = ap->fp) {
        fprintf(stderr, "##    [%d] IP: %s\t\t#\n",
                c,
                inet_ntoa(ap->ipaddr));
        c++;
    }
    fprintf(stderr, "#########################################\n\n");
}

void
print_address_using()
{
    struct address_pool *ap;
    int c = 0;
    fprintf(stderr, "## using address ########################\n");
    for (ap = address_using_head.fp; ap != &address_using_head; ap = ap->fp) {
        fprintf(stderr, "##    [%d] IP: %s\t\t#\n",
                c,
                inet_ntoa(ap->ipaddr));
        c++;
    }
    fprintf(stderr, "#########################################\n\n");
}