// cloud_publisher.h
#ifndef CLOUD_PUBLISHER_H
#define CLOUD_PUBLISHER_H

#include "esp_err.h"
#include "../../reactor_control.h"  // For reactor_telemetry_t

// Publish telemetry to default MQTT topic
esp_err_t cloud_publisher_publish_telemetry(const reactor_telemetry_t* telemetry);

// Publish alert message
esp_err_t cloud_publisher_publish_alert(const char* level, const char* message);

#endif // CLOUD_PUBLISHER_H

