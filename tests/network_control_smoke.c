#include "services/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do { if(!(v)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v); exit(1); } } while(0)
static const char * reply;
static char requested[32];
bool hachimi_system_services_supported(void) { return true; }
void hachimi_job_init(hachimi_service_job_t * job,char * output,size_t capacity)
{
    memset(job,0,sizeof(*job)); job->output=output; job->capacity=capacity;
}
bool hachimi_job_start(hachimi_service_job_t * job,hachimi_helper_t helper,const char * operation,const char * payload)
{
    (void)payload; CHECK(helper==HACHIMI_HELPER_NETWORK);
    snprintf(requested,sizeof(requested),"%s",operation); job->running=true; return true;
}
bool hachimi_job_poll(hachimi_service_job_t * job)
{
    if(!job->running) return false;
    snprintf(job->output,job->capacity,"%s",reply); job->running=false; job->success=true; return true;
}
void hachimi_job_cancel(hachimi_service_job_t * job) { job->running=false; }
static void status(hachimi_network_service_t * service,const char * output)
{
    reply=output; CHECK(hachimi_network_request(service,"status",NULL)); CHECK(hachimi_network_poll(service)); CHECK(!service->failed);
}
int main(void)
{
    static hachimi_network_service_t service; hachimi_network_init(&service);
    status(&service,"WIFI\t1\t1\t48616368696d69\t192.0.2.51\tCOMPLETED\t00:11\n"
        "ETH\teth0\t1\t198.51.100.2/24\t00:00\t1\nETH\teth2\t0\t-\t00:22\t0\nETH\teth3\t1\t203.0.113.2/24\t00:33\t1\n");
    CHECK(!strcmp(service.state.ssid,"Hachimi")); CHECK(service.state.wifi_enabled);
    CHECK(service.state.ethernet_present && !service.state.ethernet_enabled && !service.state.ethernet_connected);
    CHECK(!strcmp(service.state.ethernet_iface,"eth2")); CHECK(!strcmp(service.state.ethernet_ip,"未连接"));
    reply="OK\te5b7b2e5bc80e590af\n";
    CHECK(hachimi_network_request(&service,"ethernet-on",NULL)); CHECK(!strcmp(requested,"ethernet-on")); CHECK(hachimi_network_poll(&service));
    CHECK(hachimi_network_request(&service,"ethernet-off",NULL)); CHECK(!strcmp(requested,"ethernet-off")); CHECK(hachimi_network_poll(&service));
    status(&service,"ETH\teth2\t1\t198.51.100.50/24\t00:22\t1\n");
    CHECK(service.state.ethernet_connected && service.state.ethernet_enabled);
    CHECK(!strcmp(service.state.ethernet_ip,"198.51.100.50/24"));
    status(&service,"ETH\teth2\t1\t198.51.100.50/24\t00:22\n"); CHECK(service.state.ethernet_enabled);
    status(&service,""); CHECK(!service.state.ethernet_present);
    CHECK(!hachimi_network_request(&service,"ethernet-arbitrary",NULL));
    puts("PASS Ethernet operation allowlist, eth2 preference, admin/carrier/IP status, absent-device reset and legacy status format");
    return 0;
}
