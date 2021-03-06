/*
This code is provided under the BSD license.
Copyright (c) 2014, Emlid Limited. All rights reserved.
Written by Igor Vereninov and Mikhail Avkhimenia
twitter.com/emlidtech || www.emlid.com || info@emlid.com

Application: Mahory AHRS algorithm supplied with data from MPU9250.
Outputs roll, pitch and yaw in the console and sends quaternion
over the network - it can be used with 3D IMU visualizer located in
Navio/Applications/3D IMU visualizer.

To run this app navigate to the directory containing it and run following commands:
make
sudo ./AHRS

If you want to visualize IMU data on another machine pass it's address and port
sudo ./AHRS ipaddress portnumber

To achieve stable loop you need to run this application with a high priority
on a linux kernel with real-time patch. Raspbian distribution with real-time
kernel is available at emlid.com and priority can be set with chrt command:
chrt -f -p 99 PID
*/

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include "Navio/MPU9250.h"
#include "AHRS.hpp"
#include "Navio/Util.h"

#define G_SI 9.80665
#define PI   3.14159

// Objects

MPU9250 imu;    // MPU9250
AHRS    ahrs;   // Mahony AHRS

// Sensor data

float ax, ay, az;
float gx, gy, gz;
float mx, my, mz;

// Orientation data

float roll, pitch, yaw;

// Timing data

float offset[3];
struct timeval tv;
float dt, maxdt;
float mindt = 0.01;
unsigned long previoustime, currenttime;
float dtsumm = 0;
int isFirst = 1;

// Network data

int sockfd;
struct sockaddr_in servaddr = {0};
char sendline[80];

//============================= Initial setup =================================

void imuSetup()
{
    //----------------------- MPU initialization ------------------------------

    imu.initialize();

    //-------------------------------------------------------------------------

	printf("Beginning Gyro calibration...\n");
/*	for(int i = 0; i<100; i++)
	{
		imu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        gx *= 180 / PI;
        gy *= 180 / PI;
        gz *= 180 / PI;
		offset[0] += (-gx*0.0175);
		offset[1] += (-gy*0.0175);
		offset[2] += (-gz*0.0175);
		usleep(10000);
	}
	offset[0]/=100.0;
	offset[1]/=100.0;
	offset[2]/=100.0;*/
	offset[0]=0.017092;
	offset[1]=0.000103;
	offset[2]=0.00648999999999999;

	printf("Offsets are: %f %f %f\n", offset[0], offset[1], offset[2]);
	ahrs.setGyroOffset(offset[0], offset[1], offset[2]);
}

//============================== Main loop ====================================

void imuLoop()
{
    //----------------------- Calculate delta time ----------------------------

	gettimeofday(&tv,NULL);
	previoustime = currenttime;
	currenttime = 1000000 * tv.tv_sec + tv.tv_usec;
	dt = (currenttime - previoustime) / 1000000.0;
	if(dt < 1/1300.0) usleep((1/1300.0-dt)*1000000);
        gettimeofday(&tv,NULL);
        currenttime = 1000000 * tv.tv_sec + tv.tv_usec;
	dt = (currenttime - previoustime) / 1000000.0;

    //-------- Read raw measurements from the MPU and update AHRS --------------

    // Accel + gyro.
    imu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    ax /= G_SI;
    ay /= G_SI;
    az /= G_SI;
    gx *= 180 / PI;
    gy *= 180 / PI;
    gz *= 180 / PI;
    ahrs.updateIMU(ax, ay, az, gx*0.0175, gy*0.0175, gz*0.0175, dt);

    // Accel + gyro + mag. 
    // Soft and hard iron calibration required for proper function.
    /*
    imu.getMotion9(&ax, &ay, &az, &gx, &gy, &gz, &mx, &my, &mz);
    ahrs.update(ax, ay, az, gx*0.0175, gy*0.0175, gz*0.0175, my, mx, -mz, dt);
    */

    //------------------------ Read Euler angles ------------------------------

    ahrs.getEuler(&roll, &pitch, &yaw);

    //------------------- Discard the time of the first cycle -----------------

    if (!isFirst)
    {
    	if (dt > maxdt) maxdt = dt;
    	if (dt < mindt) mindt = dt;
    }
    isFirst = 0;

    //------------- Console and network output with a lowered rate ------------

    dtsumm += dt;
    if(dtsumm > 0.05)
    {
        // Console output
        printf("ROLL: %+05.2f PITCH: %+05.2f YAW: %+05.2f PERIOD %.4fs RATE %dHz \n", roll, pitch, yaw * -1, dt, int(1/dt));

        // Network output
        sprintf(sendline,"%10f %10f %10f %10f %dHz\n", ahrs.getW(), ahrs.getX(), ahrs.getY(), ahrs.getZ(), int(1/dt));
        sendto(sockfd, sendline, strlen(sendline), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

        dtsumm = 0;
    }
}

//=============================================================================

int main(int argc, char *argv[])
{   
    if (check_apm()) {
        return 1;
    }

    //--------------------------- Network setup -------------------------------

    sockfd = socket(AF_INET,SOCK_DGRAM,0);
    servaddr.sin_family = AF_INET;

    if (argc == 3)  {
        servaddr.sin_addr.s_addr = inet_addr(argv[1]);
        servaddr.sin_port = htons(atoi(argv[2]));
    } else {
        servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        servaddr.sin_port = htons(7000);
    }

    //-------------------- IMU setup and main loop ----------------------------

    imuSetup();

    while(1)
        imuLoop();
}
