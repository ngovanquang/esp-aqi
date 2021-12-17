#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "DHT22.h"
#include "time_sync.h"
#include "MQ135.h"
#include "dust_sensor.h"

/*

json format
{
    "deviceId": "afldakjdfklroiqjkfa",
    "deviceType": "ESP AQI",
    "data": {
        "temperature": "30.5",
        "humidity": "60.5",
        "co2": "1200",
        "co": "200",
        "pm25": "40",
        "pm10": "20",
    },
}

*/

static const char *TAG = "MQTT";
static const char *APP_TAG = "ESP_AQI";
static const char *TIME_TAG = "TIME";
static const char *deviceId = "3e31d3bd-e0a6-4497-8b48-cef5c6f3547b";
static const char *deviceType = "ESP AQI";
static const char *latitude = "21.027763";
static const char *longitude = "105.834160";
static char strftime_buf[64];
static const int DelayMS = 3000;
static int msg_id;

esp_mqtt_client_handle_t mqtt_client = NULL;
TaskHandle_t publishMessageHandle = NULL;
TaskHandle_t dhtTaskHandle = NULL;
TaskHandle_t mq135TaskHandle = NULL;
TaskHandle_t syncTimeHandle = NULL;
TaskHandle_t dustTaskHandle = NULL;
QueueHandle_t queue1; // store dht22 data
QueueHandle_t queue2; // store mq135 data
QueueHandle_t queue3; // store dust sensor data

/**
 * @brief sync time using Network time server
 * 
 * @param args 
 */
static void sync_time(void *args)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
        localtime_r(&now, &timeinfo);
        // Is time set? If not, tm_year will be (1970 -1900)
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TIME_TAG, "Time is not set yet. Connecting to Wifi and getting time over NTP");
            obtain_time();
            // update 'now' variable with current time
            time(&now);
        }
    while (1)
    {
        // set timezone to Easten standrd time
        setenv("TZ", "CST-7", 1);
        tzset();
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%d-%mT%H:%M:%S", &timeinfo);
        //strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TIME_TAG, "The current date/time in Hanoi is: %s", strftime_buf);
        vTaskDelay(DelayMS / portTICK_PERIOD_MS);
    }
    
}

static void read_dustsensor_data(void* args)
{
    char txbuff[50];
    queue3 = xQueueCreate(5, sizeof(txbuff));
    if (queue3 == 0)
    {
        ESP_LOGW("QUEUE", "failed to create queue3 = %p", queue3);
    }

    setDUSTgpio(18, 19);

    float pm25data = -1, pm10data = -1;
    sprintf(txbuff, "\"pm2_5\":%.f,\"pm10\":%.f", pm25data, pm10data);
    if (xQueueSend(queue3, (void*)txbuff, (TickType_t)0) != 1)
    {
        printf("could not sended this message = %s \n", txbuff);
    }

    while (1)
    {
        readDustData(&pm25data, &pm10data);
        sprintf(txbuff, "\"pm2_5\":%.f,\"pm10\":%.f", pm25data, pm10data);
        if (xQueueSend(queue3, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(DelayMS/portTICK_PERIOD_MS);
    }
    
}

/**
 * @brief Read data from MQ135 sensor
 * 
 * @param args 
 */
static void read_mq135_data(void* args)
{
    char txbuff[50];
    queue2 = xQueueCreate(5, sizeof(txbuff));
    if (queue2 == 0)
    {
        ESP_LOGW("QUEUE", "failed to create queue2 = %p", queue2);
    }
    config_mq135_sensor();
    while (1)
    {
        read_mq135_data_callback();
        sprintf(txbuff, "\"co2\": %.f,\"co\": %.f", get_ppm_co2(), get_ppm_co());
        
        if (xQueueSend(queue2, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(pdMS_TO_TICKS(DelayMS));
    }
    
}

static void recv_dht22_data(void* arg)
{
    char txbuff[50];
    queue1 = xQueueCreate(5, sizeof(txbuff));
    if (queue1 == 0)
    {
        ESP_LOGW("QUEUE", "failed to create queue1 = %p", queue1);
    }

    setDHTgpio(4);
    int ret = 0;
    while (1)
    {
        ret = readDHT();
        errorHandler(ret);
        sprintf(txbuff, "\"temperature\": %.1f,\"humidity\": %.1f", getTemperature(), getHumidity());
        
        if (xQueueSend(queue1, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(pdMS_TO_TICKS(DelayMS));
    }
    
}

/**
 * @brief Subscribe message
 * 
 *
*/
static void process_msg_from_subscribe (char *msg) {
    
    if (strcmp(msg,"off")) {
        gpio_set_level(2, 0);
    } else if (strcmp(msg, "on")) {
        gpio_set_level(2, 1);
    }
}

/**
 * @brief Publish message to broker
 * 
 * @param args 
 */
static void publish_message_task(char *args)
{
    char rxbuff1[50];
    char rxbuff2[50];
    char rxbuff3[50];
    char buff[1024];
    while (1)
    {
        
        if (xQueueReceive(queue1, &(rxbuff1), (TickType_t)5))
        {
            printf("got a data from queue1 === %s \n", rxbuff1);
        }
        if (xQueueReceive(queue2, &(rxbuff2), (TickType_t)5))
        {
            printf("got a data from queue2 === %s \n", rxbuff2);
        }
        if (xQueueReceive(queue3, &(rxbuff3), (TickType_t)5))
        {
            printf("got a data from queue3 === %s \n", rxbuff3);
        }
        sprintf(buff, "{\"deviceId\":\"%s\",\"deviceType\":\"%s\",\"data\":{%s,\"location\":{\"latitude\":\"%s\",\"longitude\":\"%s\"},\"time\":\"%s\",%s,%s}}", deviceId, deviceType, rxbuff1, latitude, longitude, strftime_buf, rxbuff2, rxbuff3);
        msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/qos1", buff, 0, 1, 0);
        vTaskDelay(DelayMS / portTICK_PERIOD_MS);
    }
}

/*
* log function from mqtt error
*/
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
/* handler event from mqtt
*
*
*/
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(mqtt_client, "/topic/led/qos2", 2);
        vTaskResume(publishMessageHandle);
        vTaskResume(dhtTaskHandle);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        vTaskSuspend(publishMessageHandle);
        vTaskSuspend(dhtTaskHandle);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        process_msg_from_subscribe(event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if(event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event_id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // The last argument may be used to pass data to the event handler, in this example mqtt_event_handler
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void)
{
    ESP_LOGI(APP_TAG, "[APP] Startup...");
    ESP_LOGI(APP_TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(APP_TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_pad_select_gpio(2);
    gpio_set_direction(2, GPIO_MODE_OUTPUT);

    // Connect to wifi
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();

    xTaskCreate(sync_time, "sync time", 4096, NULL, 10, &syncTimeHandle);
    xTaskCreate(recv_dht22_data, "dht data task", 4096, NULL, 10, &dhtTaskHandle);
    xTaskCreate(publish_message_task, "publish message", 4096, NULL, 10, &publishMessageHandle);
    xTaskCreate(read_mq135_data, "read mq135 data", 2048, NULL, 10, &mq135TaskHandle);
    xTaskCreate(read_dustsensor_data, "read dust sensor data", 4096, NULL, 10, &dustTaskHandle);
}
