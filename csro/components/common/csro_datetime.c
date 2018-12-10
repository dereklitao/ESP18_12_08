#include "csro_common.h"

#define ALARM_NUM 20

static TimerHandle_t second_timer = NULL;
csro_alarm alarm_group[ALARM_NUM];
bool time_sync = false;

static void check_alarm(void)
{
    debug("check alarm\r\n");
    csro_datetime_print();
    uint16_t now_minutes = date_time.time_info.tm_hour * 60 + date_time.time_info.tm_min;
    uint8_t now_weekday = date_time.time_info.tm_wday;
    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        if (alarm_group[i].valid == true && alarm_group[i].minutes == now_minutes)
        {
            if ((alarm_group[i].weekday & (0x01<<now_weekday)) == (0x01<<now_weekday)) {
                debug("alarm on @ %d, %d, %d\r\n", now_weekday, now_minutes, alarm_group[i].action);
            }
        }
    }
    
}

static void load_alarm_from_nvs(void)
{
    nvs_handle handle;
    uint8_t alarm_count = 0;
    nvs_open("alarms", NVS_READONLY, &handle);
    nvs_get_u8(handle, "alarm_count", &alarm_count);
    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        if (i < alarm_count) {
            char alarm_key[20];
            uint32_t alarm_value = 0;
            sprintf(alarm_key, "alarm%d", i);
            nvs_get_u32(handle, alarm_key, &alarm_value);
            alarm_group[i].valid = true;
            alarm_group[i].weekday = (uint8_t)((alarm_value & 0xFF000000) >> 24);
            alarm_group[i].minutes = (uint16_t)((alarm_value & 0x00FFFF00) >> 8);
            alarm_group[i].action = (uint8_t)(alarm_value & 0x000000FF);
            debug("alarm %d: weekday = %d, minutes = %d, action = %d\r\n", i, alarm_group[i].weekday, alarm_group[i].minutes, alarm_group[i].action);
        }
        else {
            alarm_group[i].valid = false;
            debug("alarm number: %d\r\n", i);
            break;
        }
    }
    nvs_close(handle);
}


static void save_alarm_to_nvs(void)
{
    nvs_handle handle;
    uint8_t alarm_count = 0;
    nvs_open("alarms", NVS_READWRITE, &handle);
    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        if (alarm_group[i].valid == true) {
            alarm_count++;
            char alarm_key[20];
            uint32_t alarm_value = 0;
            sprintf(alarm_key, "alarm%d", i);
            alarm_value = (((uint32_t)alarm_group[i].weekday) << 24) + (((uint32_t)alarm_group[i].minutes) << 8) + alarm_group[i].action;
            nvs_set_u32(handle, alarm_key, alarm_value);
        }
        else {
            break;
        }
    }
    nvs_set_u8(handle, "alarm_count", alarm_count);
    nvs_commit(handle);
    nvs_close(handle);
}
void csro_alarm_add(uint8_t weekday, uint16_t minutes, uint8_t action)
{
    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        if (alarm_group[i].valid == true) {
            if (alarm_group[i].weekday == weekday && alarm_group[i].minutes == minutes) {
                alarm_group[i].action = action;
                save_alarm_to_nvs();
                debug("alarm exist alarm %d, ", i);
                return;
            }
        }
    }

    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        if (alarm_group[i].valid == false) {
            alarm_group[i].valid = true;
            alarm_group[i].weekday = weekday;
            alarm_group[i].minutes = minutes;
            alarm_group[i].action = action;
            save_alarm_to_nvs();
            debug("add new alarm alarm %d", i);
            break;
        }
    }
}


void csro_alarm_modify(uint8_t index, uint8_t weekday, uint16_t minutes, uint8_t action)
{
    if (index > ALARM_NUM - 1) {
        return;
    }
    if (alarm_group[index].valid == true)
    {
        alarm_group[index].weekday = weekday;
        alarm_group[index].minutes = minutes;
        alarm_group[index].action = action;
        save_alarm_to_nvs();
    }
}


void csro_alarm_delete(uint8_t index)
{
    if (index > ALARM_NUM - 1) {
        return;
    }
    if (alarm_group[index].valid == true)
    {
        alarm_group[index].valid = false;
        for(size_t i = 0; i < ALARM_NUM - 1; i++)
        {
            if (i >= index && alarm_group[i+1].valid == true) {
                alarm_group[i].valid = true;
                alarm_group[i].weekday = alarm_group[i + 1].weekday;
                alarm_group[i].minutes = alarm_group[i + 1].minutes;
                alarm_group[i].action = alarm_group[i + 1].action;
                alarm_group[i + 1].valid = false;
            }
        }
        save_alarm_to_nvs();
    }
}


void csro_alarm_clear(void)
{
    for(size_t i = 0; i < ALARM_NUM; i++)
    {
        alarm_group[i].valid = false;
    }
    save_alarm_to_nvs();
}


static void second_timer_callback( TimerHandle_t xTimer )
{
    date_time.time_now++;
    date_time.time_run++;
    localtime_r(&date_time.time_now, &date_time.time_info);
    if (time_sync == true && date_time.time_info.tm_sec == 0) {
        check_alarm();
    }
}

void csro_datetime_print(void)
{
    // bzero(date_time.strtime, 64);
    strftime(date_time.strtime, 64, "%Y-%m-%d %H:%M:%S %a", &date_time.time_info);
    debug("Time is: %s\r\n", date_time.strtime);
}

void csro_datetime_set(char *timestr)
{
    uint32_t tim[6];
    sscanf(timestr, "%d-%d-%d %d:%d:%d", &tim[0], &tim[1], &tim[2], &tim[3], &tim[4], &tim[5]);
    if ((tim[0]<2018) || (tim[1]>12) || (tim[2]>31) || (tim[3]>23) || (tim[4]>59) || (tim[5]>59)) {
        return;
    }
    if (time_sync == false || tim[5] == 30) {
        time_sync = true;
        date_time.time_info.tm_year = tim[0] - 1900;
        date_time.time_info.tm_mon = tim[1] - 1;
        date_time.time_info.tm_mday = tim[2];
        date_time.time_info.tm_hour = tim[3];
        date_time.time_info.tm_min = tim[4];
        date_time.time_info.tm_sec = tim[5];
        date_time.time_now = mktime(&date_time.time_info);
    }
}

void csro_datetime_init(void)
{
    second_timer = xTimerCreate("sys_timer", 1000/portTICK_RATE_MS, pdTRUE, (void *)0, second_timer_callback);
    if (second_timer != NULL) {
        xTimerStart(second_timer, 0);
        load_alarm_from_nvs();
        vTaskDelay(1000 / portTICK_RATE_MS);
        csro_alarm_add(6, 49, 2);
        csro_alarm_add(7, 50, 3);
        csro_alarm_add(12, 51, 1);
    }
}



