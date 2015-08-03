/*

  Copyright (c) 2015 Martin Sustrik

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

*/

#include "../libmill.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CONN_ESTABLISHED 1
#define CONN_SUCCEEDED 2
#define CONN_FAILED 3

struct child_stats {
    pid_t pid;
    int connections;
    int active;
    int failed;
};

void statistics(chan ch, ipaddr addr) {
    struct child_stats stats = {0};
    stats.pid = getpid();
    udpsock sock = udplisten(iplocal(NULL, 0, 0));

    while(1) {
        int op = chr(ch, int);

        if(op == CONN_ESTABLISHED)
            ++stats.connections, ++stats.active;
        else
            --stats.active;
        if(op == CONN_FAILED)
            ++stats.failed;

        if (stats.connections % 100 == 0)
            udpsend(sock, addr, &stats, sizeof(stats));
    }
}

void dialogue(tcpsock as, chan ch) {
    chs(ch, int, CONN_ESTABLISHED);

    int64_t deadline = now() + 10000;

    tcpsend(as, "What's your name?\r\n", 19, deadline);
    if(errno != 0)
        goto cleanup;
    tcpflush(as, deadline);
    if(errno != 0)
        goto cleanup;

    char inbuf[256];
    size_t sz = tcprecvuntil(as, inbuf, sizeof(inbuf), "\r", 1, deadline);
    if(errno != 0)
        goto cleanup;

    inbuf[sz - 1] = 0;
    char outbuf[256];
    int rc = snprintf(outbuf, sizeof(outbuf), "Hello, %s!\r\n", inbuf);

    sz = tcpsend(as, outbuf, rc, deadline);
    if(errno != 0)
        goto cleanup;
    tcpflush(as, deadline);
    if(errno != 0)
        goto cleanup;

    cleanup:
    if(errno == 0)
        chs(ch, int, CONN_SUCCEEDED);
    else
        chs(ch, int, CONN_FAILED);
    tcpclose(as);
}

int main(int argc, char *argv[]) {

    int port = 5555;
    if(argc > 1)
        port = atoi(argv[1]);

    ipaddr serv_addr = iplocal(NULL, port, 0);
    tcpsock ls = tcplisten(serv_addr, 10);
    if(!ls) {
        perror("Can't open listening socket");
        return 1;
    }

    ipaddr stats_addr = iplocal(NULL, port + 1, 0);
    udpsock ss = udplisten(stats_addr);
    if(!ss) {
        perror("Can't open stats socket");
        return 1;
    }

    for (int i = 0; i < 4; ++i) {
        pid_t pid = fork();
        assert(pid >= 0);

        if (pid == 0) {
            /* Child */
            udpclose(ss);
            chan ch = chmake(int, 100);
            go(statistics(ch, stats_addr));

            while(1) {
                tcpsock as = tcpaccept(ls, -1);
                if(!as)
                    continue;
                go(dialogue(as, ch));
            }

            _exit(1);
        }
    }

    while (1) {
        struct child_stats stats;
        ipaddr addr;

        ssize_t sz = udprecv(ss, &addr, &stats, sizeof(stats), -1);
        assert(sz == sizeof(stats));

        printf("process: %d connections: %d active: %d failed: %d\n",
               stats.pid, stats.connections, stats.active, stats.failed);
    }

    return 0;
}

