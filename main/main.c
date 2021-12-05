/*
  Autor: Prof° Fernando Simplicio de Sousa
  Hardware: NodeMCU ESP32
  Espressif SDK-IDF: v4.2
  Curso Online: Formação em Internet das Coisas (IoT) com ESP32
  Link: https://www.microgenios.com.br/formacao-iot-esp32/
*/
/**
 * Lib C
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/**
 * FreeRTOS
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
/**
 * WiFi
 */
#include "esp_wifi.h"
/**
 * WiFi Callback
 */
#include "esp_event.h"
/**
 * Log
 */
#include "esp_system.h"
#include "esp_log.h"
/**
 * NVS
 */
#include "nvs.h"
#include "nvs_flash.h"

/**
 * Lwip
 */
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"

/**
 * Lib MQTT
 */
#include "mqtt_client.h"
#include "esp_tls.h"

/**
 * GPIOs;
 */
#include "driver/gpio.h"


/**
 * Definições Gerais
 */
#define DEBUG 1
#define EXAMPLE_ESP_WIFI_SSID    "elkadri_2.4"
#define EXAMPLE_ESP_WIFI_PASS    "oculosdesol"

#define BUTTON  GPIO_NUM_0 
#define TOPICO_TEMPERATURA "temperatura"

/**
 * Variáveis
 */
esp_mqtt_client_handle_t client;
static const char *TAG = "main: ";
QueueHandle_t Queue_Button;
static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;
const static int WIFI_CONNECTED_BIT = BIT0;

extern const uint8_t cloudmqtt_com_crt_start[] asm("_binary_cloudmqtt_com_crt_start");
extern const uint8_t cloudmqtt_com_crt_end[]   asm("_binary_cloudmqtt_com_crt_end");

/**
 * Protótipos
 */
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
static void wifi_init_sta( void );
static void mqtt_app_start( void );
void task_button( void *pvParameter );
void app_main( void );
esp_mqtt_client_handle_t client;

/**
 * Função de callback do stack MQTT; 
 * Por meio deste callback é possível receber as notificações com os status
 * da conexão e dos tópicos assinados e publicados;
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    client = event->client;
    int msg_id;
    
    switch (event->event_id) 
    {
        case MQTT_EVENT_BEFORE_CONNECT:
			if( DEBUG )
				ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
			break;

        /**
         * Evento chamado quando o ESP32 se conecta ao broker MQTT, ou seja, 
         * caso a conexão socket TCP, SSL/TSL e autenticação com o broker MQTT
         * tenha ocorrido com sucesso, este evento será chamado informando que 
         * o ESP32 está conectado ao broker;
         */
        case MQTT_EVENT_CONNECTED:
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            /**
             * Assina o tópico umidade assim que o ESP32 se conectar ao broker MQTT;
             * onde, 
             * esp_mqtt_client_subscribe( Handle_client, TOPICO_STRING, QoS );
             */
            esp_mqtt_client_subscribe( client, TOPICO_TEMPERATURA, 0 );

            /**
             * Se chegou aqui é porque o ESP32 está conectado ao Broker MQTT; 
             * A notificação é feita setando em nível 1 o bit CONNECTED_BIT da 
             * variável mqtt_event_group;
             */
            xEventGroupSetBits( mqtt_event_group, WIFI_CONNECTED_BIT );
            break;
        /**
         * Evento chamado quando o ESP32 for desconectado do broker MQTT;
         */
        case MQTT_EVENT_DISCONNECTED:
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");   
            /**
             * Se chegou aqui é porque o ESP32 foi desconectado do broker MQTT;
             */
            xEventGroupClearBits(mqtt_event_group, WIFI_CONNECTED_BIT);
            break;

        /**
         * O eventos seguintes são utilizados para notificar quando um tópico é
         * assinado pelo ESP32;
         */
        case MQTT_EVENT_SUBSCRIBED:
            break;
        
        /**
         * Quando a assinatura de um tópico é cancelada pelo ESP32, 
         * este evento será chamado;
         */
        case MQTT_EVENT_UNSUBSCRIBED:
            break;
        
        /**
         * Este evento será chamado quando um tópico for publicado pelo ESP32;
         */
        case MQTT_EVENT_PUBLISHED:
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        
        /**
         * Este evento será chamado quando uma mensagem chegar em algum tópico 
         * assinado pelo ESP32;
         */
        case MQTT_EVENT_DATA:
            if( DEBUG )
            {
                ESP_LOGI(TAG, "MQTT_EVENT_DATA"); 

                /**
                 * Sabendo o nome do tópico que publicou a mensagem é possível
                 * saber a quem data pertence;
                 */
                ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
                ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);               
            }       
            break;
        
        /**
         * Evento chamado quando ocorrer algum erro na troca de informação
         * entre o ESP32 e o Broker MQTT;
         */
        case MQTT_EVENT_ERROR:
            if( DEBUG )
                ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
			
		case MQTT_EVENT_ANY:
			if( DEBUG )
				ESP_LOGI(TAG, "MQTT_EVENT_ANY");
			break;
        
        case MQTT_EVENT_DELETED:
            break;
    }
    return ESP_OK;
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
	if( DEBUG )
		ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

/**
 * Configura o stack MQTT carregando o certificado SSL/TLS;
 */
static void mqtt_app_start( void )
{
   /**
    * Conexão MQTT sem certificado SSL/TSL
    */
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

/**
 * Função de callback chamada pelo stack WiFi;
 */
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
	{
        esp_wifi_connect();
    } else 
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
	{
        esp_wifi_connect();
        xEventGroupClearBits( wifi_event_group, WIFI_CONNECTED_BIT );
    }else 
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
	{
		if( DEBUG )
		{
			ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        }
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Configura o WiFi do ESP32 em modo Station, ou seja, 
 * passa a operar como um cliente, porém com operando com o IP fixo;
 */

void wifi_init_sta( void )
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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

	if( DEBUG )
	{
		ESP_LOGI(TAG, "wifi_init_sta finished.");
		ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
				 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	}

}


/**
 * Task responsável pela varredura do botão; 
 * Ao pressionar button, o valor de counter é publicado no tópico
 * TOPICO_COUNTER;
 */
void task_button( void *pvParameter )
{
    char str[20]; 
    int aux = 0;
	int counter = 0;

    if( DEBUG )
        ESP_LOGI( TAG, "task_button run...\r\n" ); 

    /**
     * Configura a Button (GPIO17) como entrada;
     */
	gpio_pad_select_gpio( BUTTON );	
    gpio_set_direction( BUTTON, GPIO_MODE_INPUT );
	gpio_set_pull_mode( BUTTON, GPIO_PULLUP_ONLY );   
	
    for(;;) 
	{
	
        xEventGroupWaitBits(mqtt_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
            
        if( gpio_get_level( BUTTON ) == 0 && aux == 0 )
		{ 
            /**
            * Aguarda 80 ms devido o bounce;
            */
            vTaskDelay( 80/portTICK_PERIOD_MS );	

            if( gpio_get_level( BUTTON ) == 0 && aux == 0 ) 
            {		

                if( DEBUG )
                    ESP_LOGI( TAG, "Button %d Pressionado .\r\n", BUTTON ); 

                sprintf( str, "%d", counter );
                /*
                  link (dica de leitura)
                  https://www.hivemq.com/blog/mqtt-essentials-part-8-retained-messages
                  Sintaxe:
                  int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, 
                							  const char *topic, 
                							  const char *data, 
                							  int len, 
                							  int qos, 
                							  int retain)
                */
                if( esp_mqtt_client_publish( client, TOPICO_TEMPERATURA, str, strlen( str ), 0, 0 ) == 0  )
                {
                    if( DEBUG )
                        ESP_LOGI( TAG, "Counter=%d .Mensagem publicada com sucesso! .\r\n", counter );
                    
                   counter++; 
                }	
                

                aux = 1; 
            }
		}

		if( gpio_get_level( BUTTON ) == 1 && aux == 1 )
		{
		    vTaskDelay( 80/portTICK_PERIOD_MS );	

			if( gpio_get_level( BUTTON ) == 1 && aux == 1 )
			{
				aux = 0;
			}
		}	

		vTaskDelay( 10/portTICK_PERIOD_MS );	
    }
}

/**
 * Inicio da Aplicação;
 */
void app_main( void )
{
    /*
		Inicialização da memória não volátil para armazenamento de dados (Non-volatile storage (NVS)).
		**Necessário para realização da calibração do PHY. 
	*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	
	/*
	   Event Group do FreeRTOS. 
	   Só podemos enviar ou ler alguma informação TCP quando a rede WiFi estiver configurada, ou seja, 
	   somente após o aceite de conexão e a liberação do IP pelo roteador da rede.
	*/
	wifi_event_group = xEventGroupCreate();
	
	/*
	  O ESP32 está conectado ao broker MQTT? Para sabermos disso, precisamos dos event_group do FreeRTOS.
	*/
	mqtt_event_group = xEventGroupCreate();
	
    /*
	  Configura a rede WiFi em modo Station;
	*/
	wifi_init_sta();
	
    /**
     * Aguarda conexão do ESP32 a rede WiFi local;
     */
	xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
	
    /**
     * Inicializa o stack MQTT;
     */
    mqtt_app_start();

	/*
	   Task responsável em ler e enviar valores via Socket TCP Client. 
	*/
	if( xTaskCreate( task_button, "task_button", 102400, NULL, 2, NULL )  != pdTRUE )
    {
      if( DEBUG )
        ESP_LOGI( TAG, "error - Nao foi possivel alocar task_button.\r\n" );  
      return;   
    }
	
}
