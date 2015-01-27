#include "espmissingincludes.h"
#include "ets_sys.h"
#include "osapi.h"
#include "httpd.h"
#include "io.h"
#include "httpdespfs.h"
#include "cgi.h"
#include "cgiwifi.h"
#include "stdout.h"
#include "auth.h"
#include "os_type.h"
#include "gpio.h"
#include "espconn.h"

/*****************LIB of MQTT*****************/
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "user_interface.h"
#include "mem.h"
/*********************************************/

#define LED_GPIO 		2 
#define LED_GPIO_MUX 	PERIPHS_IO_MUX_GPIO2_U 
#define LED_GPIO_FUNC 	FUNC_GPIO2

LOCAL os_timer_t mqtt_timer;
bool flag = 0;
MQTT_Client mqttClient;

//Function that tells the authentication system what users/passwords live on the system.
//This is disabled in the default build; if you want to try it, enable the authBasic line in
//the builtInUrls below.
int myPassFn(HttpdConnData *connData, int no, char *user, int userLen, char *pass, int passLen) {
	if (no==0) {
		os_strcpy(user, "admin");
		os_strcpy(pass, "s3cr3t");
		return 1;
//Add more users this way. Check against incrementing no for each user added.
//	} else if (no==1) {
//		os_strcpy(user, "user1");
//		os_strcpy(pass, "something");
//		return 1;
	}
	return 0;
}


/*
This is the main url->function dispatching data struct.
In short, it's a struct with various URLs plus their handlers. The handlers can
be 'standard' CGI functions you wrote, or 'special' CGIs requiring an argument.
They can also be auth-functions. An asterisk will match any url starting with
everything before the asterisks; "*" matches everything. The list will be
handled top-down, so make sure to put more specific rules above the more
general ones. Authorization things (like authBasic) act as a 'barrier' and
should be placed above the URLs they protect.
*/
HttpdBuiltInUrl builtInUrls[]={
	{"/", cgiRedirect, "/wifi/wifi.tpl"},
	//{"/", cgiRedirect, "/index.tpl"},
	// {"/flash.bin", cgiReadFlash, NULL},
	// {"/led.tpl", cgiEspFsTemplate, tplLed},
	// {"/index.tpl", cgiEspFsTemplate, tplCounter},
	// {"/led.cgi", cgiLed, NULL},

	//Routines to make the /wifi URL and everything beneath it work.

//Enable the line below to protect the WiFi configuration with an username/password combo.
//	{"/wifi/*", authBasic, myPassFn},

	{"/wifi", cgiRedirect, "/wifi/wifi.tpl"},
	{"/wifi/", cgiRedirect, "/wifi/wifi.tpl"},
	{"/wifi/wifiscan.cgi", cgiWiFiScan, NULL},
	{"/wifi/wifi.tpl", cgiEspFsTemplate, tplWlan},
	{"/wifi/connect.cgi", cgiWiFiConnect, NULL},
	{"/wifi/setmode.cgi", cgiWifiSetMode, NULL},
	{"*", cgiEspFsHook, NULL}, //Catch-all cgi function for the filesystem
	{NULL, NULL, NULL}
};

void mqttConnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\r\n");
	MQTT_Subscribe(client, "/mqtt/topic/0", 0);
	MQTT_Subscribe(client, "/mqtt/topic/1", 1);
	MQTT_Subscribe(client, "/mqtt/topic/2", 2);

	MQTT_Publish(client, "/mqtt/topic/0", "hello0", 6, 0, 0);
	MQTT_Publish(client, "/mqtt/topic/1", "hello1", 6, 1, 0);
	MQTT_Publish(client, "/mqtt/topic/2", "hello2", 6, 2, 0);

}

void mqttDisconnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

void mqttPublishedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char *topicBuf = (char*)os_zalloc(topic_len+1),
			*dataBuf = (char*)os_zalloc(data_len+1);

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
	os_free(topicBuf);
	os_free(dataBuf);
}

LOCAL void ICACHE_FLASH_ATTR CheckForMQTT_cb(void *arg)
{
	if(flag)
	{
		gpio_output_set(BIT2, 0, BIT2, 0);
		flag = 0;
	}
	else
	{
		gpio_output_set(0, BIT2, BIT2, 0);
		flag = 1;
	}

	if(wifi_station_get_connect_status() == STATION_GOT_IP)
	{
		//CFG_Load();
		// struct station_config config;
		// int i = wifi_station_get_ap_info(&config);

		// os_sprintf(sysCfg.sta_ssid, "%s", config.ssid);
		// os_sprintf(sysCfg.sta_pwd, "%s", config.password);
		// sysCfg.sta_type = STA_TYPE;

		os_sprintf(sysCfg.device_id, MQTT_CLIENT_ID, system_get_chip_id());
		os_sprintf(sysCfg.mqtt_host, "%s", MQTT_HOST);
		sysCfg.mqtt_port = MQTT_PORT;
		os_sprintf(sysCfg.mqtt_user, "%s", MQTT_USER);
		os_sprintf(sysCfg.mqtt_pass, "%s", MQTT_PASS);

		MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
		//MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

		MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
		//MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);

		MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
		MQTT_OnConnected(&mqttClient, mqttConnectedCb);
		MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
		MQTT_OnPublished(&mqttClient, mqttPublishedCb);
		MQTT_OnData(&mqttClient, mqttDataCb);

		//WIFI_Connect("Wi-fi Anderson", "wifi3102230", wifiConnectCb);
		MQTT_Connect(&mqttClient);

		INFO("\r\nMQTT System started ...\r\n");
		os_timer_disarm(&mqtt_timer);
	} 
}

//Main routine. Initialize stdout, the I/O and the webserver and we're done.
void user_init(void) {
	stdoutInit();
	ioInit();
	httpdInit(builtInUrls, 80);
	os_printf("\nWebServer=Ready\n");

	PIN_FUNC_SELECT(LED_GPIO_MUX, LED_GPIO_FUNC); 
	PIN_PULLUP_EN(LED_GPIO_MUX);
	os_printf("\nConfiguring the GPIO2\n");

	os_timer_disarm(&mqtt_timer); 
	os_timer_setfn(&mqtt_timer, (os_timer_func_t *)CheckForMQTT_cb, (void *)0); 
	os_timer_arm(&mqtt_timer, 500, 1);
	os_printf("\nMQTT timer=Started\n");
}