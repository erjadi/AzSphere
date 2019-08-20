#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <applibs/rtc.h>
#include <applibs/log.h>
#include <applibs/gpio.h>
#include <applibs/networking.h>
#include "mt3620_rdb.h"
#include "epoll_timerfd_utilities.h"
#include "leds.h"
#include <applibs/i2c.h>
#include "lcd.h"

int trigger, echo;
struct timespec trig, start, end;

void InitDistance() {
	trigger = GPIO_OpenAsOutput(MT3620_RDB_HEADER3_PIN7_GPIO, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	echo = GPIO_OpenAsInput(MT3620_RDB_HEADER3_PIN5_GPIO);
	struct timespec trig, start, end;
}

unsigned int measureDistance() {
	GPIO_SetValue(trigger, GPIO_Value_High);
	nanosleep(0, 100000);
	GPIO_SetValue(trigger, GPIO_Value_Low);
	int i = 0;
	int time = 0;
	clock_gettime(CLOCK_REALTIME, &trig);
	while (i == 0) {
		GPIO_GetValue(echo, &i);
		time++;
		if (time > 100000)
			return -1;
	}
	clock_gettime(CLOCK_REALTIME, &start);
	return (start.tv_nsec - trig.tv_nsec);
}