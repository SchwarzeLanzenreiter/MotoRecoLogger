all:mrlogger mrserver mrgpio

mrlogger:mrlogger.o
	gcc -o mrlogger mrlogger.o -lm -lgps -lwiringPi
	
mrlogger.o:	mrlogger.c
	gcc -c mrlogger.c

mrserver:mrserver.o
	gcc -o mrserver mrserver.o
	
mrserver.o:	mrserver.c
	gcc -c mrserver.c

mrgpio:mrgpio.o
	gcc -o mrgpio mrgpio.o -lwiringPi
	
mrgpio.o:	mrgpio.c
	gcc -c mrgpio.c

clean:
	rm -f mrserver mrlogger mrgpio *.o