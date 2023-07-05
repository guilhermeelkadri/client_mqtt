/**
 * @file main.c
 * @author Guilherme El Kadri Ribeiro (guilhermeelkadri@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2021-12-09
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"

#include "esp_event.h"

#include "esp_system.h"
#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"

#include "mqtt_client.h"
#include "esp_tls.h"

#include "driver/gpio.h"

#define DEBUG 1
#define EXAMPLE_ESP_WIFI_SSID "network-ssid"
#define EXAMPLE_ESP_WIFI_PASS "ssid-password"

#define QoS 2

#define BLINK_GPIO 2
#define BUTTON GPIO_NUM_0
#define TOPIC_MAC "MAC/ESP_MAC_WIFI_STA/"
#define TOPIC_IP "IP/"

typedef enum
{
   INTER_MQTT_INIT = 0,
   INTER_MQTT_WAIT_EVENT,
   INTER_MQTT_PROCESS_EVENT

} tks_intermqtt_state_t;

typedef enum
{
   INTER_EVT_BUTTON = 0,
   INTER_EVT_IP
} inter_evt_t;

static esp_mqtt_client_handle_t client;
static const char *TAG = "main: ";

static QueueHandle_t xQueue_inter_mqtt;
static SemaphoreHandle_t xButtonSemaphore;
static tks_intermqtt_state_t tks_intermqtt_state;
static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;
const static int WIFI_CONNECTED_BIT = BIT0;
const static int WIFI_NEW_IP = BIT1;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void wifi_init_sta(void);
static void mqtt_app_start(void);
void task_button(void *pvParameter);
void app_main(void);

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
   client = event->client;

   switch (event->event_id)
   {
   case MQTT_EVENT_BEFORE_CONNECT:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
      break;

   case MQTT_EVENT_CONNECTED:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

      esp_mqtt_client_subscribe(client, TOPIC_MAC, QoS);
      esp_mqtt_client_subscribe(client, TOPIC_IP, QoS);

      xEventGroupSetBits(mqtt_event_group, WIFI_CONNECTED_BIT);
      break;

   case MQTT_EVENT_DISCONNECTED:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");

      xEventGroupClearBits(mqtt_event_group, WIFI_CONNECTED_BIT);
      break;

   case MQTT_EVENT_SUBSCRIBED:
      break;

   case MQTT_EVENT_UNSUBSCRIBED:
      break;

   case MQTT_EVENT_PUBLISHED:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=0x%X", event->msg_id);
      break;

   case MQTT_EVENT_DATA:
      if (DEBUG)
      {
         ESP_LOGI(TAG, "MQTT_EVENT_DATA");

         ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
         ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);
      }
      break;

   case MQTT_EVENT_ERROR:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
      break;

   case MQTT_EVENT_ANY:
      if (DEBUG)
         ESP_LOGI(TAG, "MQTT_EVENT_ANY");
      break;

   case MQTT_EVENT_DELETED:
      break;
   }
   return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
   if (DEBUG)
   {
      ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
   }

   mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{

   const esp_mqtt_client_config_t mqtt_cfg =
       {
           .uri = "mqtt://m14.cloudmqtt.com:15269",
           .username = "ffcozmad",
           .password = "HbDavWMZhgCR",
       };

   esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
   esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
   esp_mqtt_client_start(client);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
   if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
   {
      esp_wifi_connect();
   }
   else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
   {
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
      ESP_LOGI(TAG, "Desconectouu \r\n");
   }
   else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
   {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

      xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
      xEventGroupSetBits(wifi_event_group, WIFI_NEW_IP);
   }
}

void wifi_init_sta(void)
{

   ESP_ERROR_CHECK(esp_netif_init());

   ESP_ERROR_CHECK(esp_event_loop_create_default());
   esp_netif_create_default_wifi_sta();
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   esp_event_handler_instance_t instance_any_id;
   esp_event_handler_instance_t instance_got_ip;
   ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &event_handler,
                                                       NULL,
                                                       &instance_any_id));
   ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &event_handler,
                                                       NULL,
                                                       &instance_got_ip));

   wifi_config_t wifi_config = {
       .sta = {
           .ssid = EXAMPLE_ESP_WIFI_SSID,
           .password = EXAMPLE_ESP_WIFI_PASS,
       },
   };
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());

   ESP_LOGI(TAG, "wifi_init_sta finished.");
   ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void print_mac(char *p_bufer)
{
   unsigned char mac[6] = {0};

   esp_efuse_mac_get_default(mac);
   esp_read_mac(mac, ESP_MAC_WIFI_STA);
   ESP_LOGI(TAG, "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X \r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

   sprintf(p_bufer, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void task_inter_mqtt(void *pvParameter)
{
   inter_evt_t inter_event;
   char buffer[1024] = {0};
   tks_intermqtt_state = INTER_MQTT_INIT;

   ESP_LOGI(TAG, "task_inter_mqtt run \r\n");

   for (;;)
   {
      switch (tks_intermqtt_state)
      {
      case INTER_MQTT_INIT:
         xEventGroupWaitBits(mqtt_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

         tks_intermqtt_state = INTER_MQTT_WAIT_EVENT;
         break;

      case INTER_MQTT_WAIT_EVENT:

         if (xQueueReceive(xQueue_inter_mqtt, &inter_event, 0) == pdPASS)
         {
            ESP_LOGI(TAG, "task_inter_mqtt got event:%d \r\n", inter_event);
            tks_intermqtt_state = INTER_MQTT_PROCESS_EVENT;
         }

         vTaskDelay(10 / portTICK_PERIOD_MS);

         break;

      case INTER_MQTT_PROCESS_EVENT:

         if (inter_event == INTER_EVT_BUTTON)
         {
            memset(buffer, 0, sizeof(buffer));
            print_mac(buffer);

            esp_mqtt_client_publish(client, TOPIC_MAC, buffer, strlen(buffer), QoS, false);
         }

         if (inter_event == INTER_EVT_IP)
         {
            esp_netif_t *netif = NULL;
            netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif, &ip_info);

            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));

            esp_mqtt_client_publish(client, TOPIC_IP, buffer, strlen(buffer), QoS, true);
         }

         tks_intermqtt_state = INTER_MQTT_WAIT_EVENT;

         break;

      default:
         break;
      }
   }
}

void vTimerCallback(TimerHandle_t xTimer)
{
   static bool led_state = false;

   led_state ^= true;

   gpio_set_level(BLINK_GPIO, led_state);
}

void task_gpio(void *pvParameter)
{
   static inter_evt_t inter_evt;

   TimerHandle_t xTimerLed;

   ESP_LOGI(TAG, "task_led run \r\n");

   gpio_pad_select_gpio(BLINK_GPIO);
   gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

   gpio_pad_select_gpio(BUTTON);
   gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
   gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

   xTimerLed = xTimerCreate("xTimerLed", (500 / portTICK_PERIOD_MS), pdTRUE, NULL, vTimerCallback);

   xTimerStart(xTimerLed, 0);

   for (;;)
   {
      if ((xEventGroupGetBits(wifi_event_group) & WIFI_NEW_IP) == WIFI_NEW_IP)
      {
         inter_evt = INTER_EVT_IP;

         if (xQueueSend(xQueue_inter_mqtt, &inter_evt, 0) == pdPASS)
         {
            ESP_LOGI(TAG, "task_gpio received ip event \r\n");
            xEventGroupClearBits(wifi_event_group, WIFI_NEW_IP);
         }
      }

      if(gpio_get_level(BUTTON) == 0)
      {
         for(uint8_t i = 0; i < 50; i++)
         {
            if(gpio_get_level(BUTTON))
               return;
         }

         vTaskDelay(50 / portTICK_PERIOD_MS);

         inter_evt = INTER_EVT_BUTTON;

         if (xQueueSend(xQueue_inter_mqtt, &inter_evt, 0) == pdPASS)
         {
            ESP_LOGI(TAG, "task_gpio MAC address event created \r\n");
         }
      }



      vTaskDelay(10 / portTICK_PERIOD_MS);
   }
}

void app_main(void)
{

   esp_err_t ret = nvs_flash_init();
   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
   {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
   }

   ESP_ERROR_CHECK(ret);

   wifi_event_group = xEventGroupCreate();

   mqtt_event_group = xEventGroupCreate();

   wifi_init_sta();

   xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

   mqtt_app_start();

   if (xTaskCreate(task_inter_mqtt, "task_inter_mqtt", 10240, NULL, 2, NULL) != pdTRUE)
   {
      ESP_LOGI(TAG, "error - Nao foi possivel alocar task_inter_mqtt.\r\n");

      return;
   }

   if (xTaskCreate(task_gpio, "task_gpio", 4048, NULL, 1, NULL) != pdTRUE)
   {
      ESP_LOGI(TAG, "error - nao foi possivel alocar task_gpio.\n");
      return;
   }

   if ((xQueue_inter_mqtt = xQueueCreate(3, sizeof(inter_evt_t))) == NULL)
   {
      ESP_LOGI(TAG, "error - nao foi possivel alocar xQueue_inter_mqtt.\n");
      return;
   }
}
