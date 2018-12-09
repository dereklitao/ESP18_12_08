#include "csro_common.h"
#include "cJSON.h"
#include "../device/csro_device.h"

static EventGroupHandle_t wifi_event_group;
static SemaphoreHandle_t basic_msg_semaphore;
static SemaphoreHandle_t system_msg_semaphore;
static SemaphoreHandle_t timer_msg_semaphore;
static TimerHandle_t basic_msg_timer = NULL;

static const int CONNECTED_BIT = BIT0;
int udp_sock;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, sys_info.host_name);
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}


static bool create_udp_server(void)
{
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) {
        return false;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(udp_sock);
        return false;
    }
    return true;
}

static void udp_receive_task(void *pvParameters)
{
    static char udp_rx_buffer[512];
    while(true)
    {
        bool sock_status = false;
        while(sock_status == false)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);
            sock_status = create_udp_server();
        }
        while(true)
        {
            struct sockaddr_in source_addr;
            socklen_t socklen = sizeof(source_addr);
            bzero(udp_rx_buffer, 512);
            int len = recvfrom(udp_sock, udp_rx_buffer, sizeof(udp_rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
            if (len < 0) {
                break;
            }
            cJSON *serv_ip, *serv_mac, *time_info;
            cJSON *json = cJSON_Parse(udp_rx_buffer);
            if (json != NULL) {
                serv_ip = cJSON_GetObjectItem(json, "server");
                serv_mac = cJSON_GetObjectItem(json, "clientid");
                time_info = cJSON_GetObjectItem(json, "time");
                if ((serv_ip != NULL) && (serv_mac != NULL) && (serv_ip->type == cJSON_String) && (serv_mac->type == cJSON_String)) {
                    if (strlen(serv_ip->valuestring)>=4 && strlen(serv_mac->valuestring)>=10) {
                        if ((strcmp((char *)serv_ip->valuestring, (char *)mqtt.broker) != 0) || (strcmp((char *)serv_mac->valuestring, (char *)mqtt.prefix) != 0)) {
                            strcpy((char *)mqtt.broker, (char *)serv_ip->valuestring);
                            strcpy((char *)mqtt.prefix, (char *)serv_mac->valuestring);
                            mqtt.client.isconnected = 0;
                        }
                    }
                }
                if ((time_info != NULL) && (time_info->type == cJSON_String)) {
                    csro_datetime_set(time_info->valuestring);
                }
            }
            cJSON_Delete(json);
        }
    }
    vTaskDelete(NULL);
}

void handle_individual_message(MessageData* data)
{
    debug("handle_individual_message\r\n");
    csro_device_handle_message(data);
}

void handle_group_message(MessageData* data)
{
    debug("handle_group_message\r\n");
    csro_device_handle_message(data);
}


static void connect_wifi(void)
{
    csro_system_set_status(NORMAL_START_NOWIFI);
    wifi_event_group = xEventGroupCreate();
    tcpip_adapter_init();
    esp_event_loop_init(event_handler, NULL);

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&config);
    wifi_config_t wifi_config;
    strcpy((char *)wifi_config.sta.ssid, (char *)wifi_param.ssid);
    strcpy((char *)wifi_config.sta.password, (char *)wifi_param.pass);

    debug("%s, %s\r\n", wifi_config.sta.ssid, wifi_config.sta.password);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    xTaskCreate(udp_receive_task, "udp_receive_task", 2048, NULL, 5, NULL);
} 


static bool wifi_is_connected(void)
{
    static bool connect = false;
    tcpip_adapter_ip_info_t info;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &info);
    if (info.ip.addr != 0) {
        if (connect == false) {
            connect = true;
            csro_system_set_status(NORMAL_START_NOSERVER);
            sys_info.ip[0] = info.ip.addr&0xFF;
            sys_info.ip[1] = (info.ip.addr&0xFF00)>>8;
            sys_info.ip[2] = (info.ip.addr&0xFF0000)>>16;
            sys_info.ip[3] = (info.ip.addr&0xFF000000)>>24;
            sprintf(sys_info.ip_string, "%d.%d.%d.%d", sys_info.ip[0], sys_info.ip[1], sys_info.ip[2], sys_info.ip[3]);
            debug("IP = %s\r\n", sys_info.ip_string);
        } 
    }
    else {
        csro_system_set_status(NORMAL_START_NOWIFI);
        connect = false;
    }
    return connect;
}

static bool broker_is_connected(void)
{
    if (mqtt.client.isconnected == 1) {
        return true;
    }
    if (strlen((char *)mqtt.broker) == 0 || strlen((char *)mqtt.prefix) == 0) {
        return false;
    }
    close(mqtt.network.my_socket);
    NetworkInit(&mqtt.network);
    debug("id = %s, name = %s, password = %s\r\n", mqtt.id, mqtt.name, mqtt.password);
    if (NetworkConnect(&mqtt.network, mqtt.broker, 1883) != SUCCESS) {
        return false;
    }
    MQTTClientInit(&mqtt.client, &mqtt.network, 5000, mqtt.sendbuf, MQTT_BUFFER_LENGTH, mqtt.recvbuf, MQTT_BUFFER_LENGTH);
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID.cstring = mqtt.id;
    data.username.cstring = mqtt.name;
    data.password.cstring = mqtt.password;
    if (MQTTConnect(&mqtt.client, &data) != SUCCESS) {
        return false;
    }
    sprintf(mqtt.sub_topic, "%s/%s/%s/command", mqtt.prefix, sys_info.mac_string, sys_info.device_type);
    if (MQTTSubscribe(&mqtt.client, mqtt.sub_topic, QOS0, handle_individual_message) != SUCCESS) {
        mqtt.client.isconnected = 0;
        return false;
    }
    debug("sub ok with topic: %s\r\n", mqtt.sub_topic);
    sprintf(mqtt.sub_topic, "%s/group", mqtt.prefix);
    if (MQTTSubscribe(&mqtt.client, mqtt.sub_topic, QOS0, handle_group_message) != SUCCESS) {
        mqtt.client.isconnected = 0;
        return false;
    }
    debug("sub ok with topic: %s\r\n", mqtt.sub_topic);
    return true;
}

static void basic_msg_timer_callback( TimerHandle_t xTimer )
{
    xSemaphoreGive(basic_msg_semaphore);
}

void trigger_basic_message(void)
{
    xTimerReset(basic_msg_timer, 0);
    xSemaphoreGive(basic_msg_semaphore);
}

void trigger_system_message(void)
{
    xSemaphoreGive(system_msg_semaphore);
}

void trigger_timer_message(void)
{
    xSemaphoreGive(timer_msg_semaphore);
}


static bool mqtt_pub_basic_message(void)
{
    csro_device_prepare_basic_message();
	sprintf(mqtt.pub_topic, "%s/%s/%s/basic", mqtt.prefix, sys_info.mac_string, sys_info.device_type);
	mqtt.message.payload = mqtt.content;
	mqtt.message.payloadlen = strlen(mqtt.message.payload);
	mqtt.message.qos = QOS2;
	if (MQTTPublish(&mqtt.client, mqtt.pub_topic, &mqtt.message) != SUCCESS) {
        return false;
    }
	return true;
}

static bool mqtt_pub_system_message(void)
{
    return true;
}

static bool mqtt_pub_timer_message(void)
{
    return true;
}

void csro_mqtt_task(void *pvParameters)
{
    connect_wifi();
    vSemaphoreCreateBinary(basic_msg_semaphore);
    vSemaphoreCreateBinary(system_msg_semaphore);
    vSemaphoreCreateBinary(timer_msg_semaphore);
    basic_msg_timer = xTimerCreate("basic_msg_timer", 3000/portTICK_RATE_MS, pdTRUE, (void *)0, basic_msg_timer_callback);
    xTimerStart(basic_msg_timer, 0);

    while(true)
    {
        if (wifi_is_connected()) {
            if (broker_is_connected()) {
                csro_system_set_status(NORMAL_START_OK);
                if (xSemaphoreTake(basic_msg_semaphore, 0) == pdTRUE) {
                   mqtt_pub_basic_message();
               }
                if (xSemaphoreTake(system_msg_semaphore, 0) == pdTRUE) {
                   mqtt_pub_system_message();
               }
                if (xSemaphoreTake(timer_msg_semaphore, 0) == pdTRUE) {
                   mqtt_pub_timer_message();
               }
                if (MQTTYield(&mqtt.client, 100) != SUCCESS) {
                   mqtt.client.isconnected = 0;
               }
            }
            else {
                csro_system_set_status(NORMAL_START_NOSERVER);
                vTaskDelay(1000 / portTICK_RATE_MS);
            }
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}
