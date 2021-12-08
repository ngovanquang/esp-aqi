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
#include "DHT22.h"

#define CONFIG_BROKER_URL "mqtt://broker.hivemq.com"

static const char *TAG = "MQTT";
static const char *APP_TAG = "ESP_AQI";
int msg_id;

esp_mqtt_client_handle_t mqtt_client = NULL;
TaskHandle_t publishMessageHandle = NULL;
TaskHandle_t dhtTaskHandle = NULL;

QueueHandle_t queue1; // store dht22 data

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void recv_dht22_data(void* arg)
{
    char txbuff[50];
    queue1 = xQueueCreate(5, sizeof(txbuff));
    if (queue1 == 0)
    {
        printf("failed to create queue1 = %p \n", queue1);
    }

    setDHTgpio(4);
    int ret = 0;
    while (1)
    {
        ret = readDHT();
        errorHandler(ret);
        sprintf(txbuff, "templature: %.1f,\nhumitidy: %.1f,", getTemperature(), getHumidity());
        
        if (xQueueSend(queue1, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
}


/* Publish message to broker
*
*/
static void publish_message_task(void *args)
{
    char rxbuff[50];
    char buff[1024];
    while (1)
    {
        
         if (xQueueReceive(queue1, &(rxbuff), (TickType_t)5))
        {
            printf("got a data from queue1 === %s \n", rxbuff);
        }
        sprintf(buff, "{\n%s\n}", rxbuff);
        msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/qos1", buff, 0, 1, 0);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
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

    // Connect to wifi
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();

    xTaskCreate(recv_dht22_data, "dht data task", 4096, NULL, 10, &dhtTaskHandle);
    xTaskCreate(publish_message_task, "publish message", 4096, NULL, 10, &publishMessageHandle);

}
