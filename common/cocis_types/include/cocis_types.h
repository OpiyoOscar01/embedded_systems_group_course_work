/**
 * @file cocis_types.h
 * @brief Shared enums, result codes, roles, modes and constants.
 * @author Opiyo Oscar
 * @requirements FR-ACC-05, FR-ACC-08, FR-OCC-01
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Direction of travel. */
typedef enum {
    COCIS_DIR_ENTRY = 0,
    COCIS_DIR_EXIT = 1,
} cocis_dir_t;

/* Access result and reason codes (Table 31). */
typedef enum {
    COCIS_RES_GRANTED = 0,
    COCIS_RES_DENY_UNKNOWN,
    COCIS_RES_DENY_SUSPENDED,
    COCIS_RES_DENY_EXPIRED,
    COCIS_RES_DENY_OUTSIDE_HOURS,
    COCIS_RES_DENY_EXIT_OUTSIDE_HOURS,
    COCIS_RES_DENY_ANTIPASSBACK,
    COCIS_RES_ANOMALY_EXIT_NO_ENTRY,
    COCIS_RES_EMERGENCY_PASS,
    COCIS_RES_ALERT_INTRUSION,
    COCIS_RES_ALERT_TAMPER,
    COCIS_RES_ALERT_BRUTE,
    COCIS_RES_COUNT,
} cocis_result_t;

/* Console roles (Table 40). */
typedef enum {
    COCIS_ROLE_VIEWER = 0,
    COCIS_ROLE_SECURITY,
    COCIS_ROLE_ADMIN,
} cocis_role_t;

/* System operating modes (Table 18). */
typedef enum {
    COCIS_MODE_NORMAL = 0,
    COCIS_MODE_AFTER_HOURS,
    COCIS_MODE_EMERGENCY,
    COCIS_MODE_DEGRADED,
    COCIS_MODE_MAINTENANCE,
} cocis_mode_t;

/* Registry status (FR-ACC-08). */
typedef enum {
    COCIS_USER_ACTIVE = 0,
    COCIS_USER_SUSPENDED,
    COCIS_USER_EXPIRED,
} cocis_user_status_t;

/* Timing and limits (Appendix C). */
#define COCIS_DOOR_OPEN_MS        5000
#define COCIS_DUP_SCAN_MS         2000
#define COCIS_T_VAC_S             300
#define COCIS_T_WARN_S            30
#define COCIS_PIR_HOLD_S          2
#define COCIS_LUX_ON              600
#define COCIS_LUX_OFF             800
#define COCIS_OVERRIDE_MIN        60
#define COCIS_LAMP_W              72
#define COCIS_TARIFF_UGX_PER_KWH  780
#define COCIS_HEARTBEAT_S         5
#define COCIS_OFFLINE_AFTER_S     15
#define COCIS_LOCK_LEASE_MS       3000
#define COCIS_SESSION_IDLE_S      120
#define COCIS_LOGIN_FAIL_MAX      3
#define COCIS_LOCKOUT_S           60
#define COCIS_OCC_SNAPSHOT_S      60
#define COCIS_TASK_WDT_S          10

/* Short values used in demo mode. */
#define COCIS_DEMO_T_VAC_S        20
#define COCIS_DEMO_T_WARN_S       5
#define COCIS_DEMO_PIR_HOLD_S     1
#define COCIS_DEMO_OVERRIDE_S     30

#define COCIS_UID_MAX   32
#define COCIS_NAME_MAX  48

/* Human readable result code for logs and the console. */
static inline const char *cocis_result_str(cocis_result_t r)
{
    switch (r) {
    case COCIS_RES_GRANTED:                 return "GRANTED";
    case COCIS_RES_DENY_UNKNOWN:            return "DENY_UNKNOWN";
    case COCIS_RES_DENY_SUSPENDED:          return "DENY_SUSPENDED";
    case COCIS_RES_DENY_EXPIRED:            return "DENY_EXPIRED";
    case COCIS_RES_DENY_OUTSIDE_HOURS:      return "DENY_OUTSIDE_HOURS";
    case COCIS_RES_DENY_EXIT_OUTSIDE_HOURS: return "DENY_EXIT_OUTSIDE_HOURS";
    case COCIS_RES_DENY_ANTIPASSBACK:       return "DENY_ANTIPASSBACK";
    case COCIS_RES_ANOMALY_EXIT_NO_ENTRY:   return "ANOMALY_EXIT_NO_ENTRY";
    case COCIS_RES_EMERGENCY_PASS:          return "EMERGENCY_PASS";
    case COCIS_RES_ALERT_INTRUSION:         return "ALERT_INTRUSION";
    case COCIS_RES_ALERT_TAMPER:            return "ALERT_TAMPER";
    case COCIS_RES_ALERT_BRUTE:             return "ALERT_BRUTE";
    default:                                return "UNKNOWN";
    }
}
