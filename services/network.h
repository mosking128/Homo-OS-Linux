#ifndef HACHIMI_NETWORK_SERVICE_H
#define HACHIMI_NETWORK_SERVICE_H
#include "job.h"

#define HACHIMI_NETWORK_MAX_APS 24
typedef struct { char ssid[100]; char security[16]; int signal; } hachimi_access_point_t;
typedef struct { const char * ssid; const char * password; const char * security; } hachimi_wifi_credentials_t;
typedef struct {
    bool wifi_present, wifi_enabled, bluetooth_present, hotspot_present;
    bool ethernet_present, ethernet_enabled, ethernet_connected;
    char ethernet_iface[32];
    char ssid[100], wifi_ip[64], wifi_mac[32], wifi_state[40];
    char ethernet_details[512], ethernet_ip[64], route[256], dns[128];
    hachimi_access_point_t networks[HACHIMI_NETWORK_MAX_APS];
    size_t network_count;
} hachimi_network_state_t;
typedef struct {
    hachimi_service_job_t job;
    hachimi_network_state_t state;
    char operation[24], output[16384], message[512];
    bool failed;
} hachimi_network_service_t;

void hachimi_network_init(hachimi_network_service_t * service);
bool hachimi_network_request(hachimi_network_service_t * service, const char * operation,
                              const hachimi_wifi_credentials_t * credentials);
bool hachimi_network_poll(hachimi_network_service_t * service);
void hachimi_network_cancel(hachimi_network_service_t * service);
#endif
