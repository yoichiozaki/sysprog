//
// Created by 尾崎耀一 on 2019-01-09.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

// status
#define S_INIT                  1
#define S_WAIT_REQUEST          2
#define S_REQUEST_TIMEOUT       3
#define S_IN_USE                4
#define S_TERMINATE             5

// event
#define E_RECV_DISCOVER                 1
#define E_RECV_ALLOC_REQUEST_THEN_OK    2
#define E_RECV_EXTEND_REQUEST_THEN_OK   3
#define E_RECV_ALLOC_REQUEST_THEN_NG    4
#define E_RECV_EXTEND_REQUEST_THEN_NG   5
#define E_TTL_TIMEOUT                   6
#define E_RECV_RELEASE_THEN_OK          7
#define E_RECV_RELEASE_THEN_NG          8
#define E_RECV_TIMEOUT                  9
#define E_RECV_UNKNOWN_MSG              10
#define E_RECV_UNEXPECTED_MSG           11

// message
struct message {
    uint8_t         Type;
    uint8_t         Code;
    uint16_t        TTL;
    in_addr_t       IP;
    struct in_addr  Netmask;
};

// port
#define CLIENT_PORT 51230
#define SERVER_PORT 51230

// message type
#define T_DISCOVER  0
#define T_OFFER     1
#define T_REQUEST   2
#define T_ACK       3
#define T_RELEASE   4

// message code
#define C_OK                0
#define C_OFFER_ERROR       1
#define C_REQUEST_ALLOC     2
#define C_REQUEST_EXTEND    3
#define C_ACK_ERROR         4

// client
struct client {
    struct client *fp;
    struct client *bp;
    int status;
    int TTL_counter;
    int timeout_counter;
    struct in_addr ID;
    struct in_addr IP;
    struct in_addr Netmask;
    in_port_t Port;
    uint16_t TTL;
};
struct client client_list_head;
void add_tail(in_addr_t addr, in_addr_t netmask);
void remove_head();
struct client *search(struct in_addr ID);

#define READ_LINE_MAX_LEN 1024

uint16_t ipaddr_use_limit;
uint16_t extend_ttl;

int wait_event();

void create_client_and_alloc_IP_and_send_offer(int, int);
void send_OK_ack(int, int);
void reset_TTL_and_send_OK_ack(int, int);
void send_NG_ack_and_recall_IP_and_delete_client(int, int);
void recall_IP_and_delete_client(int, int);
void re_send_offer(int, int);
void goto_in_use(int, int);

struct proctable {
    int status;
    int event;
    void (*func)(int, int); // void func(int, int)へのポインタ
} ptab[] = {
        {S_INIT, E_RECV_DISCOVER, create_client_and_alloc_IP_and_send_offer},
        {S_WAIT_REQUEST, E_RECV_ALLOC_REQUEST_THEN_OK, send_OK_ack},
        {S_WAIT_REQUEST, E_RECV_TIMEOUT, re_send_offer},
        {S_WAIT_REQUEST, E_RECV_ALLOC_REQUEST_THEN_NG, send_NG_ack_and_recall_IP_and_delete_client},
        {S_WAIT_REQUEST, E_RECV_UNKNOWN_MSG, recall_IP_and_delete_client},
        {S_WAIT_REQUEST, E_RECV_UNEXPECTED_MSG, recall_IP_and_delete_client},
        {S_REQUEST_TIMEOUT, E_RECV_ALLOC_REQUEST_THEN_OK, send_OK_ack},
        {S_REQUEST_TIMEOUT, E_RECV_ALLOC_REQUEST_THEN_NG, send_NG_ack_and_recall_IP_and_delete_client},
        {S_REQUEST_TIMEOUT, E_RECV_TIMEOUT, recall_IP_and_delete_client},
        {S_REQUEST_TIMEOUT, E_RECV_UNKNOWN_MSG, recall_IP_and_delete_client},
        {S_REQUEST_TIMEOUT, E_RECV_UNEXPECTED_MSG, recall_IP_and_delete_client},
        {S_IN_USE, E_RECV_EXTEND_REQUEST_THEN_OK, reset_TTL_and_send_OK_ack},
        {S_IN_USE, E_RECV_RELEASE_THEN_NG, goto_in_use},
        {S_IN_USE, E_RECV_RELEASE_THEN_OK, recall_IP_and_delete_client},
        {S_IN_USE, E_TTL_TIMEOUT, recall_IP_and_delete_client},
        {S_IN_USE, E_RECV_EXTEND_REQUEST_THEN_NG, send_NG_ack_and_recall_IP_and_delete_client},
        {S_IN_USE, E_RECV_UNKNOWN_MSG, recall_IP_and_delete_client},
        {S_IN_USE, E_RECV_UNEXPECTED_MSG, recall_IP_and_delete_client},
        {0, 0, NULL}
};

int server_socket;
struct sockaddr_in client_addr;
struct sockaddr_in server_addr;
socklen_t client_addr_len = sizeof(client_addr);

struct client *target;
int
main(int argc, char *argv[])
{
    // ./mydhcpd config
    if (argc != 2) {
        fprintf(stderr, "%s: Usage: %s <CONFIG>\n", argv[0], argv[0]);
        return -1;
    }
    client_list_head.fp = &client_list_head;
    client_list_head.bp = &client_list_head;
    FILE *config;
    char readline[READ_LINE_MAX_LEN];
    memset(readline, '\0', sizeof(readline));
    if ((config = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "%s: failed to open config file.\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int line = 0;
    while (fgets(readline, READ_LINE_MAX_LEN, config) != NULL) {
        if (line == 0) {
            ipaddr_use_limit = (uint16_t)strtol(readline, NULL, 10);
            // fprintf(stderr, "%d:\t%s", line, readline);
            line++;
        } else {
            char *char_address = strtok(readline, " ");
            char *char_netmask = strtok(NULL, " ");
            fprintf(stderr, "REGISTER CLIENT[%d]: address: %s, netmask: %s", line-1, char_address, char_netmask);
            add_tail(inet_addr(char_address), htons((in_addr_t)strtol(char_netmask, NULL, 10)));
            line++;
        }
    }
    fprintf(stderr, "\n");
    fclose(config);

    // for debug
    // for (struct client *tmp = client_list_head.fp; tmp != &client_list_head; tmp = tmp->fp) {
    //     fprintf(stderr, "CLIENT\n");
    //     fprintf(stderr, "\tIP:\t\t%s\n", inet_ntoa(tmp->IP));
    //     fprintf(stderr, "\tNETMASK:\t%d\n", ntohs(tmp->Netmask.s_addr));
    //     fprintf(stderr, "\tTTL:\t\t%d\n", ntohs(tmp->TTL));
    //     fprintf(stderr, "\tSTATUS:\t\t%d\n", tmp->status);
    // }

    // create socket
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // set server ip address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // bind
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    struct proctable *pt;
    int event = -1;
    for(;;) {
        event = wait_event();

        // debug
        fprintf(stderr, "[DEBUG] EVENT: %d\n\n", event);

        target = search(client_addr.sin_addr);

        // debug
        fprintf(stderr, "[DEBUG] TARGET_CLIENT\n");
        fprintf(stderr, "\tIP:\t\t%s\n", inet_ntoa(target->IP));
        fprintf(stderr, "\tNETMASK:\t%d\n", ntohs(target->Netmask.s_addr));
        fprintf(stderr, "\tTTL:\t\t%d\n", ntohs(target->TTL));
        fprintf(stderr, "\tSTATUS:\t\t%d\n\n", target->status);

        for (pt = ptab; pt->status; pt++) {
            if (pt -> status == target->status && pt-> event == event) {
                (*pt->func)(target->status, event);
                break;
            }
        }
        if (pt->status == 0) {
            fprintf(stderr, "ERROR @ pt->status == 0");
            exit(EXIT_FAILURE);
        }
    }
}

int
wait_event()
{
    struct message buf;
    struct client *_target;
    memset(&buf, '\0', sizeof(buf));
    RECVFROM:
    if (recvfrom(
            server_socket,
            &buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&client_addr,
            &client_addr_len
            ) < 0) {
        perror("recvfrom @ wait_event");
        exit(EXIT_FAILURE);
    }

    struct in_addr tmp;
    tmp.s_addr = buf.IP;
    fprintf(stderr, "--[(<-)RECV MESSAGE]----------------------------------\n");
    fprintf(stderr, "|\tType: %d\n", buf.Type);
    fprintf(stderr, "|\t\tT_DISCOVER          0\n"
                    "|\t\tT_OFFER             1\n"
                    "|\t\tT_REQUEST           2\n"
                    "|\t\tT_ACK               3\n"
                    "|\t\tT_RELEASE           4\n");
    fprintf(stderr, "|\tCode: %d\n", buf.Code);
    fprintf(stderr, "|\t\tC_OK                0\n"
                    "|\t\tC_OFFER_ERROR       1\n"
                    "|\t\tC_REQUEST_ALLOC     2\n"
                    "|\t\tC_REQUEST_EXTEND    3\n"
                    "|\t\tC_ACK_ERROR         4\n");
    fprintf(stderr, "|\tTTL: %d\n", ntohs(buf.TTL));
    fprintf(stderr, "|\tIP: %s\n", inet_ntoa(tmp));
    fprintf(stderr, "|\tNetmask: %d\n", ntohs(buf.TTL));
    fprintf(stderr, "--------------------------------------------------\n\n");

    _target = search(client_addr.sin_addr);
    if (_target->TTL_counter < 0 || _target->timeout_counter < 0) {
        if (_target->TTL_counter < 0) {
            return E_TTL_TIMEOUT;
        } else if ( _target->timeout_counter < 0) {
            return E_RECV_TIMEOUT;
        } else {
            goto RECVFROM;
        }
    }
    switch (buf.Type) {
        case T_DISCOVER:
            return E_RECV_DISCOVER;
        case T_REQUEST:
            if (buf.Code == C_REQUEST_ALLOC) {
                // TODO: 割り当てられるIPより多くのクライアントからリクエストが来た時
                if (ntohs(buf.TTL) <= ipaddr_use_limit) {
                    _target->TTL = ntohs(buf.TTL);
                    return E_RECV_ALLOC_REQUEST_THEN_OK;
                } else {
                    return E_RECV_ALLOC_REQUEST_THEN_NG;
                }
            } else if (buf.Code == C_REQUEST_EXTEND) {
                if (buf.TTL <= ipaddr_use_limit) {
                    extend_ttl = ntohs(buf.TTL);
                    return E_RECV_EXTEND_REQUEST_THEN_OK;
                } else {
                    return E_RECV_EXTEND_REQUEST_THEN_NG;
                }
            } else {
                return E_RECV_UNEXPECTED_MSG;
            }
        case T_RELEASE:
            return E_RECV_RELEASE_THEN_OK;
        default:
            fprintf(stderr, "ERROR: recv unknown type message.\n");
            return E_RECV_UNKNOWN_MSG;
    }
}
void
create_client_and_alloc_IP_and_send_offer(int st, int ev)
{
    struct message offer_message;
    offer_message.Type = T_OFFER;
    if (target->IP.s_addr) {
        offer_message.Code = C_OK;
        offer_message.IP = target->IP.s_addr;
        offer_message.Netmask = target->Netmask;
        offer_message.TTL = htons(ipaddr_use_limit);
    } else {
        offer_message.Code = C_OFFER_ERROR;
    }
    if (sendto(
            server_socket,
            &offer_message,
            sizeof(offer_message),
            0,
            (struct sockaddr *)&client_addr,
            sizeof(client_addr)
            ) < 0) {
        perror("send @ create_client_and_alloc_IP_and_send_offer");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_OFFER\n");
    fprintf(stderr, "\t|\tCode: C_OK\n");
    fprintf(stderr, "\t|\tTTL: %d\n", offer_message.TTL);
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(target->IP));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(target->Netmask.s_addr));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    target->status = S_WAIT_REQUEST;
    fprintf(stderr, "STATUS: S_INIT(%d) --[E_RECV_DISCOVER(%d) + create_client_and_alloc_IP_and_send_offer()]--> S_WAIT_REQUEST(%d)\n", st, ev, S_WAIT_REQUEST);
}

void
send_OK_ack(int st, int ev)
{
    struct message OK_ack_message;
    OK_ack_message.Type = T_ACK;
    OK_ack_message.Code = C_OK;
    OK_ack_message.IP = target->IP.s_addr;
    OK_ack_message.Netmask = target->Netmask;
    OK_ack_message.TTL = ipaddr_use_limit;
    if (sendto(
            server_socket,
            &OK_ack_message,
            sizeof(OK_ack_message),
            0,
            (struct sockaddr *)&client_addr,
            sizeof(client_addr)
    ) < 0) {
        perror("send @ send_OK_ack");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_ACK\n");
    fprintf(stderr, "\t|\tCode: C_OK\n");
    fprintf(stderr, "\t|\tTTL: %d\n", OK_ack_message.TTL);
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(target->IP));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(target->Netmask.s_addr));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    target->status = S_IN_USE;
    if (st == S_WAIT_REQUEST) {
        fprintf(stderr, "STATUS: S_WAIT_REQUEST(%d) --[E_RECV_ALLOC_REQUEST_THEN_OK(%d) + send_OK_ack()]--> S_IN_USE(%d)\n", st, ev, S_IN_USE);
    } else if (st == S_REQUEST_TIMEOUT){
        fprintf(stderr, "STATUS: S_REQUEST_TIMEOUT(%d) --[E_RECV_ALLOC_REQUEST_THEN_OK(%d) + send_OK_ack()]--> S_IN_USE(%d)\n", st, ev, S_IN_USE);
    }
}

void
reset_TTL_and_send_OK_ack(int st, int ev)
{
    target->TTL = extend_ttl;
    struct message OK_ack_message;
    OK_ack_message.Type = T_ACK;
    OK_ack_message.Code = C_OK;
    OK_ack_message.IP = target->IP.s_addr;
    OK_ack_message.Netmask = target->Netmask;
    OK_ack_message.TTL = ipaddr_use_limit;
    if (sendto(
            server_socket,
            &OK_ack_message,
            sizeof(OK_ack_message),
            0,
            (struct sockaddr *)&client_addr,
            sizeof(client_addr)
    ) < 0) {
        perror("send @ reset_TTL_and_send_OK_ack");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_ACK\n");
    fprintf(stderr, "\t|\tCode: C_OK\n");
    fprintf(stderr, "\t|\tTTL: %d\n", OK_ack_message.TTL);
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(target->IP));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(target->Netmask.s_addr));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    target->status = S_IN_USE;
    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_RECV_EXTEND_REQUEST_THEN_OK(%d) + reset_TTL_and_send_OK_ack()]--> S_IN_USE(%d)\n", st, ev, S_IN_USE);
}

void
send_NG_ack_and_recall_IP_and_delete_client(int st, int ev)
{
    struct message NG_ack_message;
    NG_ack_message.Type = T_ACK;
    NG_ack_message.Code = C_ACK_ERROR;
    if (sendto(
            server_socket,
            &NG_ack_message,
            sizeof(NG_ack_message),
            0,
            (struct sockaddr *)&client_addr,
            sizeof(client_addr)
    ) < 0) {
        perror("send @ send_NG_ack_and_recall_IP_and_delete_client");
        exit(EXIT_FAILURE);
    }
    add_tail(target->IP.s_addr, target->Netmask.s_addr);
    target->bp->fp = target->fp;
    target->fp->bp = target->bp;
    target->fp = NULL;
    target->bp = NULL;
    free(target);

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_ACK\n");
    fprintf(stderr, "\t|\tCode: C_ACK_ERROR\n");
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    target->status = S_TERMINATE;
    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_RECV_EXTEND_REQUEST_THEN_NG(%d) + send_NG_ack_and_recall_IP_and_delete_client()]--> S_TERMINATE(%d)\n", st, ev, S_TERMINATE);
}

void
recall_IP_and_delete_client(int st, int ev)
{
    add_tail(target->IP.s_addr, target->Netmask.s_addr);
    target->bp->fp = target->fp;
    target->fp->bp = target->bp;
    target->fp = NULL;
    target->bp = NULL;
    free(target);
    target->status = S_TERMINATE;
    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_RECV_RELEASE_THEN_OK or E_TTL_TIMEOUT(%d) + recall_IP_and_delete_client()]--> S_TERMINATE(%d)\n", st, ev, S_TERMINATE);
}

void
re_send_offer(int st, int ev)
{
    struct message re_offer_message;
    re_offer_message.Type = T_OFFER;
    if (target->IP.s_addr) {
        re_offer_message.Code = C_OK;
        re_offer_message.IP = target->IP.s_addr;
        re_offer_message.Netmask = target->Netmask;
        re_offer_message.TTL = ipaddr_use_limit;
    } else {
        re_offer_message.Code = C_OFFER_ERROR;
    }
    if (sendto(
            server_socket,
            &re_offer_message,
            sizeof(re_offer_message),
            0,
            (struct sockaddr *)&client_addr,
            sizeof(client_addr)
    ) < 0) {
        perror("send @ re_send_offer");
        exit(EXIT_FAILURE);
    }
    target->status = S_REQUEST_TIMEOUT;

    fprintf(stderr, "\t--[(->)SEND MESSAGE]----------------------------------\n");
    fprintf(stderr, "\t|\tType: T_OFFER\n");
    fprintf(stderr, "\t|\tCode: C_OK\n");
    fprintf(stderr, "\t|\tTTL: %d\n", re_offer_message.TTL);
    fprintf(stderr, "\t|\tIP: %s\n", inet_ntoa(target->IP));
    fprintf(stderr, "\t|\tNetmask: %d\n", ntohs(target->Netmask.s_addr));
    fprintf(stderr, "\t--------------------------------------------------\n\n");

    fprintf(stderr, "STATUS: S_WAIT_REQUEST(%d) --[E_RECV_TIMEOUT(%d) + re_send_offer()]--> S_REQUEST_TIMEOUT(%d)\n", st, ev, S_REQUEST_TIMEOUT);
}

void
goto_in_use(int st, int ev)
{
    fprintf(stderr, "STATUS: S_IN_USE(%d) --[E_RECV_RELEASE_THEN_NG(%d) + goto_in_use()]--> S_IN_USE(%d)\n", st, ev, S_IN_USE);
}

void
add_tail(in_addr_t addr, in_addr_t netmask)
{
    struct client *p;
    struct client *fp;
    struct client *bp;
    p = (struct client *)malloc(sizeof(struct client));
    memset(p, '\0', sizeof(*p));
    if (p == NULL) {
        fprintf(stderr, "ERROR: malloc @ add_tail\n");
        exit(EXIT_FAILURE);
    }
    p->status = S_INIT;
    p->timeout_counter = 10;
    p->IP.s_addr = addr;
    p->Netmask.s_addr = netmask;
    p->fp = &client_list_head;
    bp = &client_list_head;
    for (fp = client_list_head.fp; fp != &client_list_head; fp = fp->fp) {
        bp = fp;
    }
    bp->fp = p;
    client_list_head.bp = p;
    p->bp = bp;
}

void remove_head()
{
    client_list_head.fp->fp->bp = &client_list_head;
    client_list_head.fp = client_list_head.fp->fp;
    client_list_head.fp->fp = NULL;
    client_list_head.fp->bp = NULL;
    free(client_list_head.fp);
}

struct client *
search(struct in_addr ID)
{
    // TODO: 割り当てられていないIPアドレスを持つクライアントが存在すればそいつを返す
    struct client *tmp;
    for (tmp = client_list_head.fp; tmp != &client_list_head; tmp = tmp->fp) {
        if (tmp->ID.s_addr == 0) {
            return tmp;
        }
        if (tmp->ID.s_addr == ID.s_addr) {
            return tmp;
        }
    }

    struct client *p;
    struct client *fp;
    struct client *bp;
    p = (struct client *)malloc(sizeof(struct client));
    if (p == NULL) {
        fprintf(stderr, "ERROR: malloc @ search\n");
        exit(EXIT_FAILURE);
    }
    p->status = S_INIT;
    p->timeout_counter = 10;
    p->ID = ID;
    p->fp = &client_list_head;
    bp = &client_list_head;
    for (fp = client_list_head.fp; fp != &client_list_head; fp = fp->fp) {
        bp = fp;
    }
    bp->fp = p;
    client_list_head.bp = p;
    p->bp = bp;
    return p;
}