/**
 * @file cli.c
 * @brief Room console: status, manual override, energy report.
 * @author Opiyo Oscar
 * @requirements FR-LGT-08, FR-LGT-09
 */

#include "cli.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cocis_time_sntp.h"
#include "cocis_types.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "room_hal.h"
#include "sdkconfig.h"
#include "storage.h"

static const char *TAG = "room_cli";

static const char *state_name(cocis_light_state_t state)
{
    switch (state) {
    case COCIS_LIGHT_OFF:     return "OFF";
    case COCIS_LIGHT_ON:      return "ON";
    case COCIS_LIGHT_WARNING: return "WARNING";
    default:                  return "?";
    }
}

static void print_help(void)
{
    printf("\nUsage: cocis <command> [args]   (the cocis prefix is optional)\n\n"
           "Commands\n"
           "  help                    this list\n"
           "  status                  sensors and lamp state\n"
           "  override [min]          force the lamp on (bounded)\n"
           "  light on|off|auto       remote-style lamp control\n"
           "  sim motion on|off|clear inject PIR state (no wiring needed)\n"
           "  sim lux <lux>|clear     inject LDR lux value\n"
           "  sim clear               return to the real sensors\n"
           "  schedule                show the synced opening hours\n"
           "  energy                  energy used/saved/cost\n"
           "  export                  write logs/export/energy_summary.csv\n"
           "  diag                    self-test and heap\n"
           "  net                     connectivity\n\n");
}

static void cmd_sim(room_app_t *app, int argc, char **argv)
{
    if (argc >= 2 && strcasecmp(argv[1], "clear") == 0) {
        room_app_sim_motion(app, false, false);
        room_app_sim_lux(app, false, 0);
        printf("sensors: real hardware\n");
        return;
    }

    if (argc >= 3 && strcasecmp(argv[1], "motion") == 0) {
        if (strcasecmp(argv[2], "on") == 0) {
            room_app_sim_motion(app, true, true);
            printf("motion: forced ON\n");
        } else if (strcasecmp(argv[2], "off") == 0) {
            room_app_sim_motion(app, true, false);
            printf("motion: forced OFF\n");
        } else {
            printf("usage: sim motion on|off\n");
        }
        return;
    }

    if (argc >= 3 && strcasecmp(argv[1], "lux") == 0) {
        if (strcasecmp(argv[2], "clear") == 0) {
            room_app_sim_lux(app, false, 0);
            printf("lux: real sensor\n");
        } else {
            room_app_sim_lux(app, true, atoi(argv[2]));
            printf("lux: forced %d\n", app->sim_lux);
        }
        return;
    }

    printf("usage: sim motion on|off | sim lux <value> | sim clear\n");
}

/** Sum every daily energy CSV and write an aggregated management report. */
static void cmd_export(room_app_t *app)
{
    (void)app;
    if (!storage_ready()) {
        printf("SD card unavailable, cannot export\n");
        return;
    }

    DIR *dir = opendir(STORAGE_MOUNT_POINT "/logs/energy");
    if (dir == NULL) {
        printf("no energy logs yet\n");
        return;
    }

    static char report[2048];
    int offset = snprintf(report, sizeof(report),
                          "date,room,used_mwh,saved_mwh,cost_ugx\n");
    static char content[8192];
    uint64_t total_used = 0;
    uint64_t total_saved = 0;
    int files = 0;

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && offset < (int)sizeof(report) - 80) {
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5 ||
            strcasecmp(entry->d_name + name_len - 4, ".csv") != 0) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "logs/energy/%s", entry->d_name);

        size_t length = 0;
        if (storage_read(path, content, sizeof(content), &length) != ESP_OK) {
            continue;
        }

        uint64_t used = 0;
        uint64_t saved = 0;
        for (char *line = strtok(content, "\r\n"); line != NULL;
             line = strtok(NULL, "\r\n")) {
            char stamp[32];
            char room[16];
            unsigned long long lamp_on = 0;
            unsigned long long baseline = 0;
            unsigned long long line_used = 0;
            unsigned long long line_saved = 0;
            if (sscanf(line, "%31[^,],%15[^,],%llu,%llu,%llu,%llu",
                       stamp, room, &lamp_on, &baseline,
                       &line_used, &line_saved) == 6) {
                used += line_used;
                saved += line_saved;
            }
            (void)stamp;
            (void)room;
            (void)lamp_on;
            (void)baseline;
        }

        char date[9];
        snprintf(date, sizeof(date), "%.8s", entry->d_name);
        uint64_t cost = (saved * COCIS_TARIFF_UGX_PER_KWH) / 1000000ull;
        offset += snprintf(report + offset, sizeof(report) - (size_t)offset,
                           "%s,R%d,%llu,%llu,%llu\n", date,
                           CONFIG_COCIS_ROOM_ID,
                           (unsigned long long)used,
                           (unsigned long long)saved,
                           (unsigned long long)cost);
        total_used += used;
        total_saved += saved;
        files++;
    }
    closedir(dir);

    uint64_t total_cost = (total_saved * COCIS_TARIFF_UGX_PER_KWH) / 1000000ull;
    if (offset < 0) {
        offset = 0;
    } else if ((size_t)offset >= sizeof(report)) {
        offset = (int)sizeof(report) - 1;
    }
    snprintf(report + offset, sizeof(report) - (size_t)offset,
             "TOTAL,R%d,%llu,%llu,%llu\n", CONFIG_COCIS_ROOM_ID,
             (unsigned long long)total_used,
             (unsigned long long)total_saved,
             (unsigned long long)total_cost);

    esp_err_t err = storage_write("export/energy_summary.csv", report);
    if (err != ESP_OK) {
        printf("export failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("exported %d day(s) to logs/export/energy_summary.csv\n", files);
    printf("used %llu mWh, saved %llu mWh (%llu UGX)\n",
           (unsigned long long)total_used,
           (unsigned long long)total_saved,
           (unsigned long long)total_cost);
}

static void cmd_schedule(room_app_t *app)
{
    static const char *names[] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
    for (int day = 0; day < COCIS_WEEKDAY_COUNT; day++) {
        const cocis_day_t *window = &app->schedule.days[day];
        if (window->closed) {
            printf("%s closed\n", names[day]);
        } else {
            printf("%s %02u:%02u-%02u:%02u\n", names[day],
                   (unsigned)(window->open_min / 60), (unsigned)(window->open_min % 60),
                   (unsigned)(window->close_min / 60), (unsigned)(window->close_min % 60));
        }
    }
    printf("version %lu, now %s\n", (unsigned long)app->schedule.version,
           room_app_is_open(app) ? "OPEN" : "CLOSED");
}

static void execute(room_app_t *app, char *line)
{
    char *argv[5] = {0};
    int argc = 0;
    char *save = NULL;
    for (char *token = strtok_r(line, " \t", &save);
         token != NULL && argc < 5;
         token = strtok_r(NULL, " \t", &save)) {
        argv[argc++] = token;
    }
    if (argc == 0) {
        return;
    }

    /* Accept the git-style `cocis <command>` prefix as well as bare
     * commands, so scripts can use one consistent form. */
    if (strcasecmp(argv[0], "cocis") == 0) {
        for (int i = 1; i < argc; i++) {
            argv[i - 1] = argv[i];
        }
        argv[--argc] = NULL;
        if (argc == 0) {
            print_help();
            return;
        }
    }

    if (strcasecmp(argv[0], "help") == 0) {
        print_help();
    } else if (strcasecmp(argv[0], "status") == 0) {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0;
        bool time_ok = cocis_time_now_eat(&year, &month, &day, &hour, &minute, NULL);
        printf("Room      : %d\n", CONFIG_COCIS_ROOM_ID);
        printf("Time      : %s\n", time_ok ? "synced" : "not synced");
        printf("Motion    : %s%s\n", app->light.occupied ? "occupied" : "vacant",
               app->sim_motion_active ? " (simulated)" : "");
        printf("Light     : %s (%d lux%s)\n", state_name(app->light.state),
               app->sim_lux_active ? app->sim_lux : room_hal_read_lux(),
               app->sim_lux_active ? " simulated" : "");
        printf("Schedule  : %s (v%lu)\n", room_app_is_open(app) ? "open" : "closed",
               (unsigned long)app->schedule.version);
        printf("Relay     : %s\n", app->last_relay ? "on" : "off");
        printf("Override  : %u s left\n",
               (unsigned)(cocis_light_override_remaining_ms(&app->light, room_now_ms()) / 1000u));
        printf("Network   : %s\n", cocis_net_connected(&app->net) ? "online" : "offline");
        printf("SD card   : %s\n", storage_ready() ? "mounted" : "unavailable");
    } else if (strcasecmp(argv[0], "light") == 0) {
        if (argc >= 2) {
            room_app_set_light(app, argv[1]);
            printf("light: %s\n", argv[1]);
        } else {
            printf("usage: light on|off|auto\n");
        }
    } else if (strcasecmp(argv[0], "sim") == 0) {
        cmd_sim(app, argc, argv);
    } else if (strcasecmp(argv[0], "schedule") == 0) {
        cmd_schedule(app);
    } else if (strcasecmp(argv[0], "export") == 0) {
        cmd_export(app);
    } else if (strcasecmp(argv[0], "diag") == 0) {
        room_diag_t diag = room_hal_diag();
        printf("gpio      : %s\n", diag.gpio_ok ? "ok" : "fail");
        printf("adc       : %s\n", diag.adc_ok ? "ok" : "fail");
        printf("button    : %s\n", diag.button_ok ? "ok" : "fail");
        printf("heap      : %lu bytes free\n",
               (unsigned long)esp_get_free_heap_size());
    } else if (strcasecmp(argv[0], "override") == 0) {
        uint32_t minutes = argc >= 2 ? (uint32_t)atoi(argv[1]) : 0;
        room_app_override(app, minutes);
        printf("override active for %u s\n",
               (unsigned)(cocis_light_override_remaining_ms(&app->light, room_now_ms()) / 1000u));
    } else if (strcasecmp(argv[0], "energy") == 0) {
        printf("lamp on   : %llu s\n", (unsigned long long)app->energy.lamp_on_s);
        printf("baseline  : %llu s\n", (unsigned long long)app->energy.baseline_s);
        printf("used      : %llu mWh\n",
               (unsigned long long)cocis_energy_mwh_used(&app->energy, COCIS_LAMP_W));
        printf("saved     : %llu mWh\n",
               (unsigned long long)cocis_energy_mwh_saved(&app->energy, COCIS_LAMP_W));
        printf("cost saved: %llu UGX\n",
               (unsigned long long)((cocis_energy_mwh_saved(&app->energy, COCIS_LAMP_W) *
                                     COCIS_TARIFF_UGX_PER_KWH) / 1000000ull));
    } else if (strcasecmp(argv[0], "net") == 0) {
        printf("wifi: %s, mqtt: %s\n",
               app->net.wifi_connected ? "up" : "down",
               app->net.mqtt_connected ? "up" : "down");
        printf("client: %s\n", app->net.client_id);
    } else {
        printf("unknown command: %s (try help)\n", argv[0]);
    }
}

static void cli_task(void *arg)
{
    room_app_t *app = (room_app_t *)arg;

    if (!uart_is_driver_installed(UART_NUM_0)) {
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0));
    }
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
    setvbuf(stdout, NULL, _IONBF, 0);

    char line[128];
    size_t length = 0;

    printf("\nCoCIS Room %d console. Type `help`.\nR%d> ", CONFIG_COCIS_ROOM_ID,
           CONFIG_COCIS_ROOM_ID);

    while (true) {
        uint8_t ch;
        int read = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (read <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (length > 0) {
                line[length] = '\0';
                printf("\n");
                execute(app, line);
                length = 0;
            }
            printf("R%d> ", CONFIG_COCIS_ROOM_ID);
        } else if (ch == 0x7f || ch == '\b') {
            if (length > 0) {
                length--;
                printf("\b \b");
            }
        } else if (isprint(ch) && length + 1 < sizeof(line)) {
            line[length++] = (char)ch;
            printf("%c", ch);
        }
    }
}

void cli_start(room_app_t *app)
{
    ESP_LOGI(TAG, "console ready");
    xTaskCreate(cli_task, "cli_task", 6144, app, 3, NULL);
}
