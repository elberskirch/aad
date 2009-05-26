#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include <event.h>
/*******************************
 * read_ec:
 * wait_write_ec -> wait_write(0x66)
 * 
 * wait_write(addr):
 * while(inb(addr) & 0x02)
 *    sleep(0.01)
 *
 *
 *
 */

/* REGISTERS */
#define TEMPERATURE 0x58
#define FAN 0x55

#define _READ   0x01
#define _WRITE  0x02

/* TEMPERATURE SETTINGS */
#define START_TEMP  59
#define STOP_TEMP   49

#define FAN_OFF     0x00
#define FAN_ON      0x01

#define true    1
#define false   0

/*******************************/
int lasttime;
int port;
int logfile;

unsigned char fan_state = 0;
unsigned char speed = 1;
struct timeval interval;
unsigned char demon = false;
unsigned char logger = false; 



unsigned char _inb(unsigned char addr) {
    off_t r;
    unsigned char buf = 0;
    r = lseek(port, addr, SEEK_SET);
    read(port,&buf,1);
    /*printf("_%02x_\n", buf);*/
    return buf;   
}

void _outb(unsigned char addr, unsigned char val) {
    off_t r;
    r = lseek(port, addr, SEEK_SET);
    write(port,&val,1);
}

unsigned char wait_ec(unsigned char rw) {
    unsigned int l = 0;
   
    if( rw == _READ ) {  
        while( !( _inb(0x66) & _READ) && ( l < 10000 ) ) {
            usleep(10000);
            l++;
        }
    } else {
        while( ( _inb(0x66) & _WRITE) && ( l < 10000 ) ) {
            usleep(10000);
            l++;
        }
    }
    return -(l == 10000);
}

unsigned char read_temperature() {
    if( !wait_ec(_WRITE) )
        _outb(0x66, 0x80);
    if( !wait_ec(_WRITE) )
        _outb(0x62, TEMPERATURE);
    if( !wait_ec(_READ) )
        return(_inb(0x62));
}

void set_fan(unsigned char speed) {
    if( !wait_ec(_WRITE) )
        _outb(0x66, 0x81);
    if( !wait_ec(_WRITE) )
        _outb(0x62, FAN);
    if( !wait_ec(_WRITE) )
        _outb(0x62, speed);
}

void start_fan(void) {
    set_fan(speed);
    fan_state = FAN_ON;
}

void stop_fan(void) {
    set_fan(0x1f);
    fan_state = FAN_OFF;
}

static void worker(int fd, short event, void *arg) {
    struct timeval tv;
    struct event *timeout = arg;
    int newtime = time(NULL);
    unsigned char buf = 0;
    char wbuf[80];
    unsigned char len = 0;
    off_t r;
    time_t now;
    char timebuf[40];
     
    buf = read_temperature();    
    now = time(NULL);
    strftime(timebuf, 40, "%c", localtime(&now));
    switch(fan_state) {
        case FAN_OFF:
           if( buf >= START_TEMP )
              start_fan();
           break;
        case FAN_ON:
           if( buf <= STOP_TEMP )
              stop_fan();
           break;
    }      
    
    if( logger ) {
        len = sprintf(wbuf, "[%s] fan_state: %d temp: %d deg c\n", 
                      timebuf, fan_state, buf); 
        if( !demon ) 
            write(1, wbuf, len);
        write(logfile, wbuf, len);
    }

    lasttime = newtime;
        
    evutil_timerclear(&tv);
    tv.tv_sec = interval.tv_sec;
    event_add(timeout, &tv);
}

void daemonize(void) {
    /* Our process ID and Session ID */
    pid_t pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Open any logs here */        

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }
}

void start_timer(struct timeval freq) {
	struct event timeout;
	struct timeval tv;
    /* Initalize the event library */
    event_init();
	/* Initalize one event */
	evtimer_set(&timeout, worker, &timeout);
    evutil_timerclear(&tv);
    tv.tv_sec = freq.tv_sec;
	event_add(&timeout, &tv);
	lasttime = time(NULL);
	event_dispatch();
} 

void signal_handler(int signal) {
    if( !demon ) 
        printf("shutting down on signal %02d ...", signal);
    close(port);
    if( logger )
        close(logfile); 
    printf("done\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    char dev[] = "/dev/port";
    char lfile[] = "/var/log/aad.log";
    char errorbuf[80];
    interval.tv_sec = 2;
    // -d : daemonize
    // -i 2 : interval 
    // -l : log
    // -s : speed

    int c;

    if( (port = open(dev, O_RDWR)) == -1 ) {
        /* ERROR */
        strerror(errno,errorbuf,sizeof(errorbuf));
        perror(errorbuf);
        return -1;
    }
    
    if( (logfile = open(lfile, O_RDWR|O_CREAT,0600)) == -1 ) {
        /* ERROR */
        strerror(errno,errorbuf,sizeof(errorbuf));
        perror(errorbuf);
        return -1;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while((c = getopt(argc,argv,"di:ls:")) != -1) {
        switch(c) {
            case 'd': 
                demon = true;
                printf("daemonize\n");
                break;
            case 'i':
                interval.tv_sec = atoi(optarg);
                printf("interval %02d\n", interval.tv_sec);
                break;
            case 'l':
                logger = true;
                break;
            case 's':
                speed = atoi(optarg);
                printf("speed %02d\n", speed);
                break;
            default:
                printf("unknown option\n");
                exit(-1);
        }

    }
    // defined start condition, fan off
    stop_fan();

    if( demon )
        daemonize();

    start_timer(interval);
    return 0;
}
