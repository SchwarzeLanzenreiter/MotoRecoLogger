// MIT License
// 
// Copyright (c) 2019 SchwarzeLanzenreiter
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_IF "can0"

int g_sock;
int g_read_can_port;

#ifdef DEBUG

#define LOG_FILE "/home/pi/canlogger/canlogger.log"  // debug log location
#define CAN_FILE "/home/pi/canlogger/canlogger.log"  // can log location

// debug output function
void LOG_PRINT(char log_txt[256], ...)
{
	time_t timer;
	struct tm *date;
	char str[256];
	FILE *log_file;        // log file
	char log_str[256];

	// get date time
	timer = time(NULL);
	date = localtime(&timer);
	strftime(str, sizeof(str), "[%Y%m%d %H%M%S] ", date);

	if ((log_file = fopen(LOG_FILE, "a")) != NULL) {
		// combine string
		strcat(str,log_txt);

		// write log file
		fputs(str, log_file);
		fclose(log_file); 
	}
	return;
}
#endif

// create and initialize can socket
int init_can_socket(const char *sock)
{
    struct ifreq ifr;
    struct sockaddr_can addr;

    /* open socket */
    g_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(g_sock < 0)
    {
#ifdef DEBUG
		sprintf(log_str,"socket create error\n", ptr);
		LOG_PRINT(log_str);
#endif
        return (-1);
    }

    addr.can_family = AF_CAN;
    strcpy(ifr.ifr_name, sock);

    if (ioctl(g_sock, SIOCGIFINDEX, &ifr) < 0)
    {
#ifdef DEBUG
		sprintf(log_str,"ioctl error\n", ptr);
		LOG_PRINT(log_str);
#endif
        return (-1);
    }

    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
#ifdef DEBUG
		sprintf(log_str,"bind error\n", ptr);
		LOG_PRINT(log_str);
#endif
        return (-1);
    }

    return 0;
}

// read can data
void read_can_socket()
{
    struct can_frame frame_rd;
    int recv = 0;

    g_read_can_port = 1;
    while(g_read_can_port)
    {
        struct timeval timeout = {1, 0};
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(g_sock, &readSet);

        if (select((g_sock + 1), &readSet, NULL, NULL, &timeout) >= 0)
        {
            if (!g_read_can_port)
            {
                break;
            }
            if (FD_ISSET(g_sock, &readSet))
            {
                recv = read(g_sock, &frame_rd, sizeof(struct can_frame));
                if(recv)
                {
                    printf("dlc = %d, data = %s\n", frame_rd.can_dlc,frame_rd.data);
                }
            }
        }

    }

}

// finalize can socket
int close_can_socket()
{
    close(g_sock);
    return 0;
}

// register sigterm
void sigterm(int signo)
{
	g_read_can_port = 0;
}

int main(void)
{
	// register sigterm event
	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);
	
	// initialize can interface
    init_can_socket(CAN_IF);
	
	// main can read logic
    read_can_socket();
	
	// close can socket
	close_can_socket();
	
    return 0;
}
