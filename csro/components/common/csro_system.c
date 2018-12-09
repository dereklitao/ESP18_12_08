#include "csro_common.h"
#include "../device/csro_device.h"
#include "ssl/ssl_crypto.h"

csro_system_info    sys_info;
csro_wifi_param     wifi_param;
csro_date_time      date_time;
csro_mqtt           mqtt;

void csro_system_set_status(csro_system_status status)
{
    if((sys_info.status != status) && (sys_info.status != RESET_PENDING))
    {
        sys_info.status = status;
        debug("system status is %d.\r\n", sys_info.status);
    }
}


void csro_system_init(void)
{
    nvs_handle handle;
    debug("==========CSRO 2018.12.08 Derek Li==========\r\n");
    nvs_flash_init();
    nvs_open("system_info", NVS_READWRITE, &handle);

    nvs_get_i32(handle, "power_on_count", &sys_info.power_on_count);
    sys_info.power_on_count = sys_info.power_on_count + 1;
    nvs_set_i32(handle, "power_on_count", sys_info.power_on_count);
    
    debug("power count %d.\r\n", sys_info.power_on_count);
    esp_wifi_get_mac(WIFI_MODE_STA, sys_info.mac);
    sprintf(sys_info.mac_string, "%02x%02x%02x%02x%02x%02x", sys_info.mac[0] - 2, sys_info.mac[1],
            sys_info.mac[2], sys_info.mac[3], sys_info.mac[4], sys_info.mac[5]);
    sprintf(sys_info.host_name, "csro-%s", sys_info.mac_string);
   
    csro_datetime_init();
    csro_device_init();

 
    nvs_get_i8(handle, "wifi_flag", &wifi_param.flag);
    if (wifi_param.flag)
    {
        size_t ssid_len = 0;
        size_t pass_len = 0;
        nvs_get_str(handle, "ssid", NULL, &ssid_len);
        nvs_get_str(handle, "ssid", wifi_param.ssid, &ssid_len);
        nvs_get_str(handle, "pass", NULL, &pass_len);
        nvs_get_str(handle, "pass", wifi_param.pass, &pass_len);
        xTaskCreate(csro_mqtt_task, "csro_mqtt_task", 2048, NULL, 6, NULL);
    }
    else
    {
        xTaskCreate(csro_smartconfig_task, "csro_smartconfig_task", 1024, NULL, 6, NULL);
    }
    nvs_commit(handle);
    nvs_close(handle);
}


void csro_system_encrypt(void)
{
	uint8_t sha[30];
	SHA1_CTX *ctx=(SHA1_CTX *)malloc(sizeof(SHA1_CTX));
	SHA1_Init(ctx);
	SHA1_Update(ctx, (const uint8_t *)sys_info.mac_string, strlen(sys_info.mac_string));
	SHA1_Final(sha, ctx);
	free(ctx);
	sprintf(mqtt.id, "csro-%s", sys_info.mac_string);
	sprintf(mqtt.name, "csro-%s/%s", sys_info.mac_string, sys_info.device_type);
	sprintf(mqtt.password, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", 
    sha[0],  sha[2],  sha[4],  sha[6],  sha[8],  sha[1],  sha[3],  sha[5],  sha[7],  sha[9], 
    sha[10], sha[12], sha[14], sha[16], sha[18], sha[11], sha[13], sha[15], sha[17], sha[19]);
}