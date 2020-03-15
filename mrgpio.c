#include <wiringPi.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
 
 #define GPIO17 17  //rpi_wake
 #define GPIO27 27  //sup_bike
 #define DEBUG
 
 #define LOG_FILE "/home/pi/GPIO/gpio.log"  		// debug log location
 
 char g_log_str[256];
 
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
 
void no_sup_bike(void){
	
	printf("Power Off Detected!!\n");
	
	//wait 3sec
	sleep(3);

#ifdef DEBUG
	sprintf(g_log_str,"detect sup_bike off\n");
	debug_log(g_log_str);
#endif

	//check if still no_sup_bike
	if (!digitalRead(GPIO27))
	{
		printf("Now shutting down!!\n");
	
#ifdef DEBUG
		sprintf(g_log_str,"now going to shutdown\n");
		debug_log(g_log_str);
#endif
	
		// shutdown
		system("sudo shutdown -h now");
	}
}
 
int main(void){
        int setup = 0;
		
		//initialize WiringPi
        setup = wiringPiSetupGpio();
		
		// set GPIO17 pin to output mode
		pinMode(GPIO17, OUTPUT);
		
		// set GPIO17 1 to tell PIC raspberryPi is running.
		digitalWrite(GPIO17, 1);
		
		// set GPIO27 pin to input mode
		pinMode(GPIO27, INPUT);
		
        while(setup != -1){
                wiringPiISR( GPIO27, INT_EDGE_FALLING, no_sup_bike );
				
                sleep(10000);
        }
        return 0;
}