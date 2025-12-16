#include "ping.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

static const char *TAG = "ping";

static void ping_on_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

    printf("%lu bytes from %s icmp_seq=%u ttl=%u time=%lu ms\n",
           (unsigned long)recv_len, ipaddr_ntoa(&target_addr),
           (unsigned)seqno, (unsigned)ttl, (unsigned long)elapsed_time);
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));

    printf("From %s icmp_seq=%u timeout\n", ipaddr_ntoa(&target_addr), (unsigned)seqno);
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

    printf("\n--- ping statistics ---\n");
    printf("%lu packets transmitted, %lu received, %lu%% packet loss, time %lu ms\n",
           (unsigned long)transmitted, (unsigned long)received,
           transmitted > 0 ? ((transmitted - received) * 100 / transmitted) : 0,
           (unsigned long)total_time_ms);

    // Delete the ping session when done
    esp_ping_delete_session(hdl);
}

esp_err_t ping_host(const char *hostname, uint32_t count)
{
    ESP_LOGI(TAG, "Resolving hostname: %s", hostname);

    // Resolve hostname to IP address
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int err = getaddrinfo(hostname, NULL, &hint, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s: %d", hostname, err);
        if (res) freeaddrinfo(res);
        return ESP_FAIL;
    }

    ip_addr_t target_addr;
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &ipv4->sin_addr);
    freeaddrinfo(res);

    ESP_LOGI(TAG, "Resolved %s to %s", hostname, ipaddr_ntoa(&target_addr));

    // Configure ping session
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = count;  // 0 = infinite

    // Set callbacks
    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_on_success,
        .on_ping_timeout = ping_on_timeout,
        .on_ping_end = ping_on_end,
        .cb_args = NULL,
    };

    esp_ping_handle_t ping;
    esp_err_t ret = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ping session: %s", esp_err_to_name(ret));
        return ret;
    }

    printf("PING %s (%s)\n", hostname, ipaddr_ntoa(&target_addr));
    esp_ping_start(ping);

    return ESP_OK;
}
