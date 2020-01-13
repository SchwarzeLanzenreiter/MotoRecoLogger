// MIT License
// 
// Copyright (c) 2019 Schwarze Lanzenreiter
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
#include <wiringPi.h>
#include <gps.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define DEBUG
#define CAN_IF "can0"
#define LOG_FILE "/home/pi/canlogger/canlogger.log"  		// debug log location
#define CAN_DIR "/home/pi/canlogger/"  						// can log location
#define SUP_BIKE 27									 		// SUP_BIKE is used to check whether motorcycle is awake
#define GPS_CAN_ID_NUM1 2047                    			// virtual CAN id for longitude and latitude of GPS data. 2047 = "7FF"
#define GPS_CAN_ID_NUM2 2046                    			// virtual CAN id for altitude and speed of GPS data. 2046 = "7FE"
#define CAN_FILE_NAME_LENGTH 19

struct CANData {
	unsigned int		second;
	unsigned short int 	mirisecond;
	unsigned short int 	id;
	char 				data[8];
};	

int g_sock;
int g_running;
char g_log_str[256];
struct timespec g_start_timestamp = { 0, 0 };
struct CANData g_candata;
FILE *g_logfile = NULL;
int g_flg_key_on[3];
int g_rc = -1;
struct gps_data_t g_gps_data;
char g_fname[sizeof(CAN_DIR) + CAN_FILE_NAME_LENGTH + 1]; // CAN_FILE_NAME_LENGTH is filename length like "20190501_120423.dat"
	
// proto
void debug_log(char log_txt[256], ...);	
int initialize(const char *sock);
void keep_reading();
int finalyze();
void sigterm(int signo);
struct timeval diff_time(); 
int is_keyon();

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

// create and initialize can socket
int initialize(const char *sock)
{
    struct ifreq ifr;
    struct sockaddr_can addr;

    /* open socket */
    g_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(g_sock < 0)
    {
#ifdef DEBUG
		sprintf(g_log_str,"socket create error\n");
		debug_log(g_log_str);
#endif
        return (-1);
    }

    addr.can_family = AF_CAN;
    strcpy(ifr.ifr_name, sock);

    if (ioctl(g_sock, SIOCGIFINDEX, &ifr) < 0)
    {
#ifdef DEBUG
		sprintf(g_log_str,"ioctl error\n");
		debug_log(g_log_str);
#endif
        return (-1);
    }

    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
#ifdef DEBUG
		sprintf(g_log_str,"bind error\n");
		debug_log(g_log_str);
#endif
        return (-1);
    }
	
	// initialize GPIO port
	if(wiringPiSetupGpio() == -1) {
#ifdef DEBUG
		sprintf(g_log_str,"Fail to initialize WiringPi\n");
		debug_log(g_log_str);
#endif
		return -1;
	}
	pinMode(SUP_BIKE, INPUT);

    return 0;
}

// calc diff time from program start
struct timespec elapsed_time(){
	struct timespec timestamp;
	struct timespec elapsed_timestamp;

	clock_gettime(CLOCK_MONOTONIC_RAW,&timestamp);
				
	// calc timestamp
	if (g_start_timestamp.tv_sec == 0) // first init
	{   
		g_start_timestamp = timestamp;
	}
	
	elapsed_timestamp.tv_sec  = timestamp.tv_sec  - g_start_timestamp.tv_sec;
	elapsed_timestamp.tv_nsec = timestamp.tv_nsec - g_start_timestamp.tv_nsec;
	
	if (elapsed_timestamp.tv_nsec < 0){
		elapsed_timestamp.tv_sec--, elapsed_timestamp.tv_nsec += 1000000000;
	}
	
	if (elapsed_timestamp.tv_sec < 0){
		elapsed_timestamp.tv_sec = elapsed_timestamp.tv_nsec = 0;
	}
	
	return elapsed_timestamp;
}

// check motorcycle is awake
// create can log file and connect to gpsd if bike is on
int is_keyon(){
	// check key on
	g_flg_key_on[2] = g_flg_key_on[1];
	g_flg_key_on[1] = g_flg_key_on[0];
	
	// GPIO 27 is connected to motorcycle power line via MotoReco hat
//#ifndef DEBUG
	g_flg_key_on[0] = digitalRead(SUP_BIKE);
//#endif
//#ifdef DEBUG
//	g_flg_key_on[0] = 1; // force recognize this program that bike is keyon
//#endif

	//if detect key on 3 times in a raw, bike is keyon
	if (g_flg_key_on[0] && g_flg_key_on[1] && g_flg_key_on[2]){
		// create can log file
		if (!g_logfile){
			time_t currtime;
			struct tm now;
			char fdir[] = CAN_DIR;

			if (time(&currtime) == (time_t)-1) {
#ifdef DEBUG
				sprintf(g_log_str,"fail to get timestamp of can log file\n");
				debug_log(g_log_str);
#endif
				return -1;
			}

			localtime_r(&currtime, &now);

			sprintf(g_fname, "%s%04d%02d%02d_%02d%02d%02d.dat",
				fdir,
				now.tm_year + 1900,
				now.tm_mon + 1,
				now.tm_mday,
				now.tm_hour,
				now.tm_min,
				now.tm_sec);
#ifdef DEBUG
			sprintf(g_log_str, "enabling g_logfile '%s'\n\n", g_fname);
			debug_log(g_log_str);
#endif
			g_logfile = fopen(g_fname, "wb");
			if (!g_logfile) {
#ifdef DEBUG
				sprintf(g_log_str,"fail to create log file\n");
				debug_log(g_log_str);
#endif
				return -1;
			}				
			
			// connect GPSD as same time as opening can log file
			if ((g_rc = gps_open("localhost", "2947", &g_gps_data)) == -1) {
#ifdef DEBUG
				sprintf(g_log_str,"fail to connect GPSD\n");
				debug_log(g_log_str);
#endif
				return -1;
			}

			gps_stream(&g_gps_data, WATCH_ENABLE | WATCH_JSON, NULL);
			
			//reset previous timestamp when can log file created
			g_start_timestamp.tv_sec  = 0;
			g_start_timestamp.tv_nsec = 0;
		}
	//if detect key on 3 times in a raw, bike is keyoff
	} else if (!g_flg_key_on[0] && !g_flg_key_on[1] && !g_flg_key_on[2]){
		//close can log file
		if (g_logfile != NULL){
			fclose(g_logfile);
			g_logfile = NULL;
			
			//change can log name using time when file closed
			char latest_fname[sizeof(CAN_DIR) + CAN_FILE_NAME_LENGTH + 1];
			time_t currtime;
			struct tm now;
			char fdir[] = CAN_DIR;

			if (time(&currtime) == (time_t)-1) {
#ifdef DEBUG
				sprintf(g_log_str,"fail to get timestamp of can log file\n");
				debug_log(g_log_str);
#endif
				return -1;
			}

			localtime_r(&currtime, &now);

			sprintf(latest_fname, "%s%04d%02d%02d_%02d%02d%02d.dat",
				fdir,
				now.tm_year + 1900,
				now.tm_mon + 1,
				now.tm_mday,
				now.tm_hour,
				now.tm_min,
				now.tm_sec);

			rename(g_fname,latest_fname);
			
#ifdef DEBUG
			sprintf(g_log_str, "renaming g_logfile '%s'\n", latest_fname);
			debug_log(g_log_str);
#endif			
			
			//disconnect gpsd 
			gps_stream(&g_gps_data, WATCH_DISABLE, NULL);
			gps_close (&g_gps_data);
			g_rc = -1;
		}
	}
	return 0;
}

// read can data
void keep_reading()
{
    struct can_frame frame_data;
    int recv = 0;
	struct timeval tv = {1, 0};
	struct timespec elapsed_timestamp;
	fd_set readfd;
	char timestring[16];
	int int_lon, int_lat,int_alt,int_spd;
	int int_lon_prev = 0;
	int int_lat_prev = 0;
	char buf[64];
		
    while(g_running)
    {
		// check if bike is keyon
		if (is_keyon()<0){
			g_running = 0;
			break;
		}

        FD_ZERO(&readfd);
        FD_SET(g_sock, &readfd);

		tv.tv_sec = 1;
		tv.tv_usec = 0;

        if (select((g_sock+1), &readfd, NULL, NULL, &tv) < 0){
			g_running = 0;
			break;
#ifdef DEBUG
			sprintf(g_log_str,"select error\n");
			debug_log(g_log_str);
#endif
		}

		if (FD_ISSET(g_sock, &readfd))
		{
			recv = read(g_sock, &frame_data, sizeof(struct can_frame));

			if(recv)
			{
				elapsed_timestamp = elapsed_time();

				g_candata.second = elapsed_timestamp.tv_sec;
				g_candata.mirisecond = elapsed_timestamp.tv_nsec/1000000;             //convert n sec to m sec, n sec is too high resolution
				g_candata.id = frame_data.can_id;
				memcpy(g_candata.data, frame_data.data, 8);
			}			
			// write to log file
			if (g_logfile){
				fwrite(&g_candata, sizeof(g_candata), 1, g_logfile);
			}
		}
		
		// read gps data
		if (g_rc != -1) {
			if (gps_waiting (&g_gps_data, 0)) {
				if ((g_rc = gps_read(&g_gps_data)) != -1) {
					// only continue if longitude and latitude are fixed
					if (
						(g_gps_data.status == STATUS_FIX || g_gps_data.status == STATUS_DGPS_FIX ) &&  //from raspbian buster, need to add STATUS_DGPS_FIX, or never log GPS data. 
						(g_gps_data.fix.mode == MODE_2D || g_gps_data.fix.mode == MODE_3D) &&
						!isnan(g_gps_data.fix.latitude) &&
						!isnan(g_gps_data.fix.longitude)) {
			
						elapsed_timestamp = elapsed_time();
						
#ifdef DEBUG
//						printf("%f\n",g_gps_data.fix.longitude);
//						printf("%f\n",g_gps_data.fix.latitude);
//						printf("%f\n",g_gps_data.fix.altitude);
//						printf("%f\n",g_gps_data.fix.speed);
#endif

						g_candata.second = elapsed_timestamp.tv_sec;	
						g_candata.mirisecond = elapsed_timestamp.tv_nsec/1000000;             //convert n sec to m sec, n sec is too high resolution
						
						//longitude	factor 1000000 offset 180
						//latitude	factor 1000000 offset 90
						//altitude	factor 1000000 offset 1000  // world lowest place is Dead Sea , -430m. 
						//speed		factor 1000000 offset 0 (speed should be bigger than zero)

						int_lon = g_gps_data.fix.longitude*1000000+180000000;
						int_lat = g_gps_data.fix.latitude*1000000+90000000;
						int_alt = g_gps_data.fix.altitude*1000000+1000000000;
						int_spd = g_gps_data.fix.speed*1000000;

						// avoid logging multiple GPS info
						if ((int_lon == int_lon_prev) && (int_lat == int_lat_prev)){
							continue;
						}

						int_lon_prev = int_lon;
						int_lat_prev = int_lat;
						
						// create can format data1(include longitude and latitude)
						g_candata.id = GPS_CAN_ID_NUM1;
						memset(g_candata.data,0,sizeof(g_candata.data));
						memcpy(g_candata.data, &int_lon, sizeof(int));
						memcpy(&g_candata.data[4], &int_lat, sizeof(int));
						
						// record GPS data as CAN packet
						if (g_logfile){
							fwrite(&g_candata, sizeof(g_candata), 1, g_logfile);
						}
						
						// create can format data2(altitude and speed)
						g_candata.id = GPS_CAN_ID_NUM2;
						memset(g_candata.data,0,sizeof(g_candata.data));
						memcpy(g_candata.data, &int_alt, sizeof(int));
						memcpy(&g_candata.data[4], &int_spd, sizeof(int));
						
						// record GPS data as CAN packet
						if (g_logfile){
							fwrite(&g_candata, sizeof(g_candata), 1, g_logfile);
						}
					} else {
#ifdef DEBUG
						sprintf(g_log_str,"gps not fixed gps_status:%d fix:%d\n",g_gps_data.status,g_gps_data.fix.mode);
						debug_log(g_log_str);
#endif
					}
				} else {
#ifdef DEBUG
					sprintf(g_log_str,"gps read error\n");
					debug_log(g_log_str);
#endif
				}
			} 
		} else {
			// try to connect GPSD again
			if ((g_rc = gps_open("localhost", "2947", &g_gps_data)) == -1) {
#ifdef DEBUG
				sprintf(g_log_str,"fail to connect GPSD\n");
				debug_log(g_log_str);
#endif
			}

			gps_stream(&g_gps_data, WATCH_ENABLE | WATCH_JSON, NULL);
#ifdef DEBUG
			sprintf(g_log_str,"reconnected to GPSD again\n");
			debug_log(g_log_str);
#endif
		}
    }
}

// finalize can socket
int finalize()
{
	// close can socket
    close(g_sock);
	
	// close can log file
	if (g_logfile != NULL){
		fclose(g_logfile);
		g_logfile = NULL;
#ifdef DEBUG
		sprintf(g_log_str,"close g_logfile in finalize function\n");
		debug_log(g_log_str);
#endif
	}
	
	//disconnect gpsd 
	if (g_rc != -1){
		gps_stream(&g_gps_data, WATCH_DISABLE, NULL);
		gps_close (&g_gps_data);
		g_rc = -1;
	}
    return 0;
}

// register sigterm
void sigterm(int signo)
{
	g_running = 0;
}

int main(void)
{
	// register sigterm event
	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);
	
	// initialize can interface
    if (initialize(CAN_IF)!=0) {
		return -1;
	}
	
	// set running flag
	g_running = 1;

	// main can read logic
    keep_reading();
	
	// close can socket
	finalize();
	
    return 0;
}
