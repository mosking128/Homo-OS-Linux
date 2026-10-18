#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void decode_hex(char * destination, size_t capacity, const char * source)
{
    size_t used = 0;
    unsigned byte;
    while(source && source[0] && source[1] && used + 1 < capacity) {
        if(sscanf(source, "%2x", &byte) != 1) break;
        destination[used++] = (char)byte;
        source += 2;
    }
    destination[used] = '\0';
}
static void encode_hex(char * destination, const char * source)
{
    static const char digits[] = "0123456789abcdef";
    while(*source) {
        unsigned char byte = (unsigned char)*source++;
        *destination++ = digits[byte >> 4];
        *destination++ = digits[byte & 15];
    }
    *destination = '\0';
}

void hachimi_network_init(hachimi_network_service_t * service)
{
    memset(service, 0, sizeof(*service));
    hachimi_job_init(&service->job, service->output, sizeof(service->output));
    strcpy(service->state.wifi_ip, "-");
    strcpy(service->state.wifi_mac, "-");
    strcpy(service->state.wifi_state, "DISCONNECTED");
    strcpy(service->state.ethernet_ip, "未连接");
}

bool hachimi_network_request(hachimi_network_service_t * service, const char * operation,
                              const hachimi_wifi_credentials_t * credentials)
{
    static const char * allowed[] = {"status", "scan", "wifi-on", "wifi-off", "connect", "disconnect", "reconnect", "ethernet-on", "ethernet-off"};
    bool valid = false;
    for(size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        if(strcmp(operation, allowed[i]) == 0) valid = true;
    if(!valid || service->job.running) return false;
    char payload[800] = {0};
    if(strcmp(operation, "connect") == 0) {
        if(credentials == NULL || credentials->ssid == NULL || credentials->password == NULL ||
           credentials->security == NULL || strlen(credentials->ssid) >= 100 ||
           strlen(credentials->password) > 64 ||
           (strcmp(credentials->security, "OPEN") != 0 && strcmp(credentials->security, "PSK") != 0)) return false;
        char ssid[201], password[129];
        encode_hex(ssid, credentials->ssid);
        encode_hex(password, credentials->password);
        snprintf(payload, sizeof(payload), "%s\t%s\t%s", ssid, password, credentials->security);
        memset(password, 0, sizeof(password));
    }
    bool started = hachimi_job_start(&service->job, HACHIMI_HELPER_NETWORK, operation,
                                     credentials ? payload : NULL);
    memset(payload, 0, sizeof(payload));
    if(started) snprintf(service->operation, sizeof(service->operation), "%s", operation);
    return started;
}

bool hachimi_network_poll(hachimi_network_service_t * service)
{
    if(!hachimi_job_poll(&service->job)) return false;
    hachimi_network_state_t * state = &service->state;
    service->failed = !service->job.success;
    service->message[0] = '\0';
    if(strcmp(service->operation, "scan") == 0) state->network_count = 0;
    if(strcmp(service->operation, "status") == 0) {
        state->ethernet_details[0] = '\0';
        strcpy(state->ethernet_ip, "未连接");
        state->ethernet_present = state->ethernet_enabled = state->ethernet_connected = false;
        state->ethernet_iface[0] = '\0';
    }
    char * context;
    for(char * line = strtok_r(service->output, "\n", &context); line;
        line = strtok_r(NULL, "\n", &context)) {
        char * fields[10]; size_t count = 1; fields[0] = line;
        for(char * p = line; *p && count < 10; p++) if(*p == '\t') { *p = '\0'; fields[count++] = p + 1; }
        if(count >= 2 && (strcmp(fields[0], "ERROR") == 0 || strcmp(fields[0], "OK") == 0)) {
            decode_hex(service->message, sizeof(service->message), fields[1]);
            if(strcmp(fields[0], "ERROR") == 0) service->failed = true;
        }
        else if(count == 7 && strcmp(fields[0], "WIFI") == 0) {
            state->wifi_present = atoi(fields[1]) != 0;
            state->wifi_enabled = atoi(fields[2]) != 0;
            decode_hex(state->ssid, sizeof(state->ssid), fields[3]);
            snprintf(state->wifi_ip, sizeof(state->wifi_ip), "%s", fields[4]);
            snprintf(state->wifi_state, sizeof(state->wifi_state), "%s", fields[5]);
            snprintf(state->wifi_mac, sizeof(state->wifi_mac), "%s", fields[6]);
        }
        else if((count == 5 || count == 6) && strcmp(fields[0], "ETH") == 0) {
            size_t used = strlen(state->ethernet_details);
            snprintf(state->ethernet_details + used, sizeof(state->ethernet_details) - used,
                     "%s%s  ·  %s\nIP  %s    MAC  %s\n", used ? "\n" : "", fields[1],
                     atoi(fields[2]) ? "网线已连接" : "网线未连接", fields[3], fields[4]);
            if(!state->ethernet_present || !strcmp(fields[1], "eth2")) {
                state->ethernet_present = true;
                state->ethernet_connected = atoi(fields[2]) != 0;
                state->ethernet_enabled = count == 6 ? atoi(fields[5]) != 0 : true;
                snprintf(state->ethernet_iface, sizeof(state->ethernet_iface), "%s", fields[1]);
                snprintf(state->ethernet_ip, sizeof(state->ethernet_ip), "%s",
                         state->ethernet_connected ? fields[3] : "未连接");
            }
        }
        else if(count == 2 && strcmp(fields[0], "ROUTE") == 0) decode_hex(state->route, sizeof(state->route), fields[1]);
        else if(count == 2 && strcmp(fields[0], "DNS") == 0) decode_hex(state->dns, sizeof(state->dns), fields[1]);
        else if(count == 2 && strcmp(fields[0], "BT") == 0) state->bluetooth_present = atoi(fields[1]) != 0;
        else if(count == 2 && strcmp(fields[0], "HOTSPOT") == 0) state->hotspot_present = atoi(fields[1]) != 0;
        else if(count == 4 && strcmp(fields[0], "AP") == 0 && state->network_count < HACHIMI_NETWORK_MAX_APS) {
            hachimi_access_point_t * ap = &state->networks[state->network_count++];
            ap->signal = atoi(fields[1]);
            snprintf(ap->security, sizeof(ap->security), "%s", fields[2]);
            decode_hex(ap->ssid, sizeof(ap->ssid), fields[3]);
        }
    }
    return true;
}
void hachimi_network_cancel(hachimi_network_service_t * service) { hachimi_job_cancel(&service->job); }
