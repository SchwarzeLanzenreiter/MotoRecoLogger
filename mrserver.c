// MIT License
// 
// Copyright (c) 2020 Schwarze Lanzenreiter
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
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "./motoreco.h"

#define DEBUG
#define LOG_FILE "/home/pi/motoreco/server.log"  		    // debug log location

const int port = 55283;
const char *ipaddr = "192.168.100.255";                     // only send broad cast to 192.168.100.***

int g_running;
char* g_shared_memory;
int g_seg_id;
char g_log_str[256];
FILE *g_logfile = NULL;
struct CANData g_data_arry[SHM_SIZE / sizeof(struct CANData)];

// debug output function
void debug_log(char log_txt[256], ...)
{
	time_t timer;
	struct tm *date;
	char str[256];
	FILE *log_file;        // log file

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

// create and initialize shared memory
int initializeIPC(){
	//key  Johann Zarco, Bradley Smith, Pol Espargaro and Jonas Folger
	// combination of 5 38 44 94 and smallest number is ... 3444589 
	int key = 3444589;

    // getting shared memory ID
	g_seg_id = shmget(key, SHM_SIZE, IPC_CREAT | 0666);
    if(g_seg_id == -1){
#ifdef DEBUG
		sprintf(g_log_str,"fail to get segment id\n");
		debug_log(g_log_str);
#endif
        return -1;
    }

    // attach shared memory to process
    g_shared_memory = (char *)shmat(g_seg_id, (void *)0, 0);
	
	if (g_shared_memory == (char *)-1){
#ifdef DEBUG
		sprintf(g_log_str,"fail to attach shared memory\n");
		debug_log(g_log_str);
#endif
		return -1;
	}
	
	return 0;
}

// register sigterm
void sigterm(int signo)
{
	g_running = 0;
}

int main(int argc, char** argv)
{
    // register sigterm event
	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

    struct sockaddr_in addr;
    int sock;
    socklen_t from_addr_size;    
    
    //create socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    //fail to create
    if(sock < 0)
    {
#ifdef DEBUG
		sprintf(g_log_str,"fail to make a socket\n");
		debug_log(g_log_str);
#endif
        return -1;
    }

    //set up options
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ipaddr);

    int broadcast  = 1;
    setsockopt(sock,SOL_SOCKET, SO_BROADCAST, (char *)&broadcast, sizeof(broadcast));

    //initialize sheared memory
    initializeIPC();

  	//set running flag
	g_running = 1;

    while(g_running)
    {
        //shared memory size and data_array is same so just copy
        memcpy(g_data_arry, g_shared_memory, SHM_SIZE );

        // search how many valid CAN data
        int i=0;
        while (g_data_arry[i].second && g_data_arry[i].mirisecond){
            i++;
        }

        if (i>0){
            ssize_t send_status;

            // only sent valid CAN data
            sendto(sock, g_data_arry , sizeof(struct CANData)*i , 0,
                    (struct sockaddr *)&addr, sizeof(addr) );
            // fail to send data
            if(send_status < 0)
            {
                printf("fail tp sent UDP data\n", i);
                return -1;
            }
        }

        //wait 0.1sec
        usleep(100000);
    }
    //close socket
    close(sock);

	//detach shared memory
	shmdt(g_shared_memory);

    return 0;
}