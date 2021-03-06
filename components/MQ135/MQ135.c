#include <stdio.h>
#include <math.h>
#include "MQ135.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
//#include "esp_adc_cal.h"
#include "esp_log.h"

#define ADC1_TEST_CHANNEL (0)

//static esp_adc_cal_characteristics_t adc1_chars;

static const int R0 = 80;

static float ratio = 0;
static float RS = 0;
static int sensorValue = 0;
static float volts = 0;

static float ppm_co = 0;
static float ppm_co2 = 0;
/*
    The library reference from https://www.teachmemicro.com/mq-135-air-quality-sensor-tutorial/
    and Datasheet
    
    Rs = RL * (Vc - VRL)/VRL
    RS = RL * (Vc/VRL - 1)
    RL = 20k
    Vc = 3.8v
    VRL = Vout
    -> RS

    ppm = a*(RS/R0)^b

    |------------|   a      |        b        |
    |    CO      |  605.18  |   -3.937        |
    |    C02     |  110.47  |   -2.862        |

    // cách 2: ko chính xác.
    CO2 (10, 1.39) (100, 1.01) => PPMco2 = (RS/R0 - 1.39)*(90/-0.38) + 10 = 339.21 -236.84*(RS/R0)

    CO (10 , 1.58) (100, 1.181) => PPMco = (y - y1)*(x2 - x1)/(y2 - y1) + x1
                                    => PPMco = (RS/R0 - 1.58) * (90/-0.399) + 10 = 366.39 -225.56*RS/R0

*/

void config_mq135_sensor (void)
{
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_TEST_CHANNEL, ADC_ATTEN_11db);
}

void read_mq135_data_callback (void)
{
    sensorValue = adc1_get_raw(ADC1_TEST_CHANNEL);
    volts = sensorValue * 3.3;
    volts = volts / 4095;
    RS = 20*(3.8/volts - 1);
    ratio = RS / R0;
    printf("volts: %f, RS: %f, ratio: %f\n", volts, RS, ratio);
}

float get_ppm_co2 (void)
{
    ppm_co2 = 110.47*pow(ratio, -2.862);
    ESP_LOGI("MQ SENSOR", "C02: %f ppm", ppm_co2);
    return ppm_co2;
}

float get_ppm_co (void)
{
    ppm_co = 605.18*pow(ratio, -3.937)/100;
    ESP_LOGI("MQ SENSOR", "C0: %f ppm", ppm_co);
    return ppm_co;
}