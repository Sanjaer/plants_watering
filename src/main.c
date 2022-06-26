/*
 * TODO
 */



#include <zephyr/zephyr.h>
#include <zephyr/sys/printk.h>
#include <zephyr/posix/time.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_event.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include <zephyr/drivers/gpio.h>

#include <zephyr/net/sntp.h>
#ifdef CONFIG_POSIX_API
#include <arpa/inet.h>
#endif

#include <zephyr/logging/log.h>

#define SNTP_PORT 123

#define PUMP0_NODE DT_ALIAS(pump0)

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

#define SERVER_ADDR "178.215.228.24"


static const struct gpio_dt_spec pump0 = GPIO_DT_SPEC_GET(PUMP0_NODE, gpios);

LOG_MODULE_REGISTER(esp32_wifi_sta, LOG_LEVEL_DBG);

static struct net_mgmt_event_callback dhcp_cb;

static void handler_cb(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}

	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("Your address: %s",
		log_strdup(net_addr_ntop(AF_INET,
				   &iface->config.dhcpv4.requested_ip,
				   buf, sizeof(buf))));
	LOG_INF("Lease time: %u seconds",
			iface->config.dhcpv4.lease_time);
	LOG_INF("Subnet: %s",
		log_strdup(net_addr_ntop(AF_INET,
					&iface->config.ip.ipv4->netmask,
					buf, sizeof(buf))));
	LOG_INF("Router: %s",
		log_strdup(net_addr_ntop(AF_INET,
						&iface->config.ip.ipv4->gw,
						buf, sizeof(buf))));
}

/* GPIOs section */
// GPIO configuration
bool config_gpios(){

	int ret;

	if (!device_is_ready(pump0.port)) {
		return false;
	}

	ret = gpio_pin_configure_dt(&pump0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return false;
	}

	return true;

}

// Task
void blink(){
	config_gpios();
	int ret;
	while (1) {
		printk("Toggle pump0\n");
		ret = gpio_pin_toggle_dt(&pump0);
		if (ret < 0) {
			return;
		}
		k_msleep(SLEEP_TIME_MS);
	}
}

int set_time_sntp () {

	struct sntp_ctx ctx;

	struct sockaddr_in addr;
	struct sntp_time sntp_time;
	int rv;

	struct timespec time_sntp;
	struct timespec actual;
	time_sntp.tv_nsec=0;
	// char *npt_srv_addr;

	/* ipv4 */
	// struct hostent *lh = gethostbyname("es.pool.ntp.org");
	// npt_srv_addr = inet_ntoa(*((struct in_addr*) lh->h_addr_list[0]));
	// printk("npt_srv_addr: %s", npt_srv_addr);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SNTP_PORT);
	inet_pton(AF_INET, SERVER_ADDR, &addr.sin_addr);

	rv = sntp_init(&ctx, (struct sockaddr *) &addr,
		       sizeof(struct sockaddr_in));
	if (rv < 0) {
		LOG_ERR("Failed to init SNTP IPv4 ctx: %d", rv);
		sntp_close(&ctx);
		return rv;
	}

	LOG_INF("Sending SNTP IPv4 request...");
	rv = sntp_query(&ctx, 4 * MSEC_PER_SEC, &sntp_time);
	if (rv < 0) {
		LOG_ERR("SNTP IPv4 request failed: %d", rv);
		sntp_close(&ctx);
		return rv;		
	} else {
		time_sntp.tv_sec=sntp_time.seconds;
		clock_settime(CLOCK_MONOTONIC, &time_sntp);
		LOG_INF("status: %d", rv);
		LOG_INF("time since Epoch: high word: %u, low word: %u",
			(uint32_t)(sntp_time.seconds >> 32), (uint32_t)sntp_time.seconds);
	}
 
	sntp_close(&ctx);

	clock_gettime(CLOCK_MONOTONIC, &actual);
	LOG_INF("clock_gettime returned %lld\n", actual.tv_sec);

}

/* Wifi section */
void connect_wifi(void){
	struct net_if *iface;

	net_mgmt_init_event_callback(&dhcp_cb, handler_cb,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&dhcp_cb);

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("wifi interface not available");
		return;
	}

	net_dhcpv4_start(iface);

	if (!IS_ENABLED(CONFIG_ESP32_WIFI_STA_AUTO)) {
		wifi_config_t wifi_config = {
			.sta = {
				.ssid = CONFIG_ESP32_WIFI_SSID,
				.password = CONFIG_ESP32_WIFI_PASSWORD,
			},
		};

		esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);

		ret |= esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
		ret |= esp_wifi_connect();
		if (ret != ESP_OK) {
			LOG_ERR("connection failed");
		}
	}

	k_busy_wait(SLEEP_TIME_MS*5);

	while (1){
		printk("sntp: %d", set_time_sntp());
		k_msleep(SLEEP_TIME_MS*5);
	}
}


K_THREAD_DEFINE(wifi_id, STACKSIZE, connect_wifi, NULL, NULL, NULL,
		PRIORITY, 0, 0);

