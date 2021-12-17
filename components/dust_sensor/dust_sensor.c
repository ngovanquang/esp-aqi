#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "dust_sensor.h"

// == global defines ========================

int PM25_GPIO = 5;
int PM10_GPIO = 6;
float lowRatioPM25 = 0, lowRatioPM10 = 0;

// == set the DUST used pin =================

void setDUSTgpio (int gpio_25, int gpio_10)
{
    PM25_GPIO = gpio_25;
    PM10_GPIO = gpio_10;
}

void readDustData(float *pm25, float *pm10)
{
	unsigned int mSec25 = 0;
    unsigned int mSec10 = 0;
    for (int i = 0; i < 30000; ++i)
    {
        if ( gpio_get_level(PM25_GPIO)== 0 )
        {
		    ++mSec25;
	    }
        if ( gpio_get_level(PM10_GPIO)== 0 )
        {
            ++mSec10;
        }
        ets_delay_us(1000);		// mSec delay
        if (i % 3000 == 0) vTaskDelay(200 /portTICK_PERIOD_MS);
    }
	lowRatioPM25 = mSec25/30000.0*100;
    lowRatioPM10 = mSec10/30000.0*100;
    printf("ratio25: %f, ratio10: %f\n", lowRatioPM25, lowRatioPM10);
    if (lowRatioPM10 < 8) *pm10 = lowRatioPM10 * 10;
    else *pm10 = lowRatioPM10*15 - 40;

    if (lowRatioPM25 < 8) *pm10 = lowRatioPM25 * 10;
    else *pm25 = lowRatioPM25*15 - 40;
}


// z