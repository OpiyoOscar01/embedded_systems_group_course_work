/**
 * @file cli.c
 * @brief Line-oriented console with login, roles and audit logging.
 *
 * Commands are intentionally small and discoverable through `help`. Every
 * state-changing command is logged to the audit chain with the console user
 * as the actor. Tag scans can be injected with `scan in|out <uid>` so the
 * Wokwi demo does not need physical RFID readers.
 *
 * @author Opiyo Oscar
 * @requirements FR-CLI-01..06, FR-AUTH-01..05, FR-ACC-01
 */

#include "cli.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "auth_crypto.h"
#include "cocis_time_sntp.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"
#include "state_store.h"
#include "storage.h"

static const char *TAG = "cli";

#define CLI_LINE_MAX 160

/* Set when a CR is seen so a following LF is not treated as a new line. */
static bool s_swallow_lf;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

/** Current console user name, or "anon". */
static const char *actor_name(hub_app_t *app)
{
    if (app->auth.session_user < 0 || (size_t)app->auth.session_user >= app->auth.count) {
        return "anon";
    }
    return app->auth.users[app->auth.session_user].username;
}

/**
 * Gate a command behind a role. Prints a helpful message when refused.
 * Returns true when the caller may proceed.
 */
static bool require_role(hub_app_t *app, cocis_role_t role)
{
    uint32_t now = hub_now_ms();

    if (cocis_auth_require(&app->auth, now, role)) {
        return true;
    }

    int index = app->auth.session_user;
    if (index >= 0 && (size_t)index < app->auth.count &&
        app->auth.users[index].must_change) {
        printf("Password change required: run `passwd` (you will be prompted).\n");
    } else {
        printf("Not authorised (login or use a higher role).\n");
    }
    return false;
}

/** Read a line without echoing it; one '*' is printed per character. */
static bool read_secret(const char *prompt, char *out, size_t out_len)
{
    printf("%s", prompt);
    size_t length = 0;

    while (true) {
        uint8_t ch;
        int read = uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY);
        if (read <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                s_swallow_lf = true;
            }
            printf("\n");
            out[length] = '\0';
            return true;
        }
        if (ch == 0x7f || ch == '\b') {
            if (length > 0) {
                length--;
                printf("\b \b");
            }
        } else if (isprint(ch) && length + 1 < out_len) {
            out[length++] = (char)ch;
            printf("*");
        }
    }
}

static bool valid_date(const char *text)
{
    if (text == NULL || strlen(text) != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }
    int year = 0, month = 0, day = 0;
    if (sscanf(text, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        return false;
    }
    return year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static cocis_role_t parse_role(const char *text)
{
    if (text == NULL) {
        return COCIS_ROLE_VIEWER;
    }
    if (strcasecmp(text, "admin") == 0) {
        return COCIS_ROLE_ADMIN;
    }
    if (strcasecmp(text, "security") == 0) {
        return COCIS_ROLE_SECURITY;
    }
    return COCIS_ROLE_VIEWER;
}

static cocis_weekday_t parse_day(const char *text)
{
    static const char *names[COCIS_WEEKDAY_COUNT] = {
        "mon", "tue", "wed", "thu", "fri", "sat", "sun",
    };
    for (int i = 0; i < COCIS_WEEKDAY_COUNT; i++) {
        if (text != NULL && strcasecmp(text, names[i]) == 0) {
            return (cocis_weekday_t)i;
        }
    }
    return COCIS_WEEKDAY_COUNT;
}

static const char *day_name(int day)
{
    static const char *names[COCIS_WEEKDAY_COUNT] = {
        "mon", "tue", "wed", "thu", "fri", "sat", "sun",
    };
    return (day >= 0 && day < COCIS_WEEKDAY_COUNT) ? names[day] : "???";
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                   */
/* ------------------------------------------------------------------------- */

static void cmd_help(hub_app_t *app)
{
    cocis_role_t role = COCIS_ROLE_VIEWER;
    cocis_auth_session_role(&app->auth, hub_now_ms(), &role);

    printf("\nUsage: cocis <command> [args]   (the cocis prefix is optional)\n\n"
           "Session\n"
           "  help, login <user> [pass], logout, passwd [old] [new], whoami\n\n"
           "Read-only\n"
           "  status, users, occupancy, nodes, rooms, schedule, diag, net\n\n");

    if (role >= COCIS_ROLE_SECURITY) {
        printf("Security\n"
               "  scan in|out <uid>, alerts, logs [YYYYMMDD], audit [YYYYMMDD],\n"
               "  verify, rollcall, emergency on|off, lights 1|2|all on|off|auto\n\n");
    }
    if (role >= COCIS_ROLE_ADMIN) {
        printf("Administration\n"
               "  adduser, deluser, suspend, resume, schedule set|close, holiday,\n"
               "  door entry|exit, release <uid>, config, reboot\n\n");
    } else {
        printf("Log in with a higher role to see more commands.\n\n");
    }
}

static void cmd_login(hub_app_t *app, int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: login <user> [password]\n");
        return;
    }

    char password[64];
    const char *pass = argc >= 3 ? argv[2] : NULL;
    if (pass == NULL) {
        if (!read_secret("Password: ", password, sizeof(password))) {
            return;
        }
        pass = password;
    }

    cocis_auth_result_t result = cocis_auth_login(&app->auth, argv[1], pass,
                                                  hub_now_ms(),
                                                  cocis_auth_pbkdf2_sha256);

    switch (result) {
    case COCIS_AUTH_OK: {
        cocis_role_t role = COCIS_ROLE_VIEWER;
        cocis_auth_session_role(&app->auth, hub_now_ms(), &role);
        printf("Welcome %s. You are logged in as %s.\n", argv[1], hub_role_str(role));
        break;
    }
    case COCIS_AUTH_MUST_CHANGE:
        printf("Login accepted, but you must set a new password first.\n"
               "Run: passwd   (you will be prompted; at least 8 characters)\n");
        break;
    case COCIS_AUTH_BAD_CREDENTIALS:
        printf("Login failed: incorrect username or password.\n");
        break;
    case COCIS_AUTH_LOCKED:
        printf("Login failed: too many attempts. Try again in 60 seconds.\n");
        break;
    default:
        printf("Login failed (%s).\n", cocis_auth_result_str(result));
        break;
    }

    char detail[64];
    snprintf(detail, sizeof(detail), "%s", cocis_auth_result_str(result));
    hub_app_audit(app, argv[1], "LOGIN", "console", "", "", detail);

    if (result == COCIS_AUTH_BAD_CREDENTIALS || result == COCIS_AUTH_LOCKED) {
        hub_app_publish_event(app, "alert", "{\"kind\":\"console_login\"}");
    }
}

static void cmd_passwd(hub_app_t *app, int argc, char **argv)
{
    if (app->auth.session_user < 0) {
        printf("Please log in first: login <user>\n");
        return;
    }

    char old_buf[64];
    char new_buf[64];
    const char *old_pass = argc >= 2 ? argv[1] : NULL;
    const char *new_pass = argc >= 3 ? argv[2] : NULL;

    if (old_pass == NULL) {
        if (!read_secret("Current password: ", old_buf, sizeof(old_buf))) {
            return;
        }
        old_pass = old_buf;
    }
    if (new_pass == NULL) {
        if (!read_secret("New password: ", new_buf, sizeof(new_buf))) {
            return;
        }
        new_pass = new_buf;
    }
    if (strlen(new_pass) < 8) {
        printf("Password not changed: use at least 8 characters.\n");
        return;
    }

    const char *username = app->auth.users[app->auth.session_user].username;
    uint8_t salt[COCIS_AUTH_SALT_LEN];
    cocis_auth_random_salt(salt);

    cocis_auth_result_t result =
        cocis_auth_change_password(&app->auth, username, old_pass, new_pass,
                                   salt, hub_now_ms(), cocis_auth_pbkdf2_sha256);
    if (result == COCIS_AUTH_OK) {
        printf("Password changed. Use the new password from now on.\n");
    } else if (result == COCIS_AUTH_BAD_CREDENTIALS) {
        printf("Password not changed: the old password is incorrect.\n");
    } else {
        printf("Password not changed (%s).\n", cocis_auth_result_str(result));
    }

    if (result == COCIS_AUTH_OK) {
        state_store_save_auth(&app->auth);
        hub_app_audit(app, username, "PASSWORD_CHANGE", username, "", "", "OK");
    }
}

static void cmd_status(hub_app_t *app)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    bool time_ok = cocis_time_now_eat(&year, &month, &day, &hour, &minute, NULL);

    printf("Time      : %s", time_ok ? "" : "NOT SYNCED ");
    if (time_ok) {
        printf("%04d-%02d-%02d %02d:%02d EAT\n", year, month, day, hour, minute);
    } else {
        printf("\n");
    }
    printf("Mode      : %s\n", app->emergency.active ? "EMERGENCY" : "NORMAL");
    printf("Occupancy : %u\n", (unsigned)cocis_occupancy_count(&app->occupancy));
    printf("Schedule  : v%lu\n", (unsigned long)app->schedule.version);
    printf("SD card   : %s\n", storage_ready() ? "mounted" : "unavailable");
    printf("Network   : %s\n", cocis_net_connected(&app->net) ? "online" : "offline");
    printf("Users     : %u registered\n", (unsigned)app->registry.count);
}

static void cmd_users(hub_app_t *app)
{
    int year = 0, month = 0, day = 0;
    bool time_ok = cocis_time_now_eat(&year, &month, &day, NULL, NULL, NULL);

    printf("%-12s %-16s %-8s %-10s %-10s\n",
           "UID", "NAME", "ROLE", "STATUS", "EXPIRY");
    for (size_t i = 0; i < app->registry.count; i++) {
        const cocis_user_t *user = &app->registry.users[i];
        cocis_user_status_t status = time_ok
            ? cocis_user_effective_status(user, year, month, day)
            : user->status;
        printf("%-12s %-16s %-8s %-10s %-10s\n",
               user->uid, user->name, hub_role_str(user->role),
               hub_status_str(status),
               user->expiry[0] != '\0' ? user->expiry : "-");
    }
}

static void cmd_adduser(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_ADMIN)) {
        return;
    }
    if (argc < 3) {
        printf("usage: adduser <uid> <name> [viewer|security|admin] [YYYY-MM-DD]\n");
        return;
    }

    cocis_user_t user = {0};
    snprintf(user.uid, sizeof(user.uid), "%s", argv[1]);
    snprintf(user.name, sizeof(user.name), "%s", argv[2]);
    user.role = argc >= 4 ? parse_role(argv[3]) : COCIS_ROLE_VIEWER;
    user.status = COCIS_USER_ACTIVE;
    if (argc >= 5) {
        if (!valid_date(argv[4])) {
            printf("bad expiry date\n");
            return;
        }
        snprintf(user.expiry, sizeof(user.expiry), "%s", argv[4]);
    }
    snprintf(user.created_at, sizeof(user.created_at), "2026-10-29T00:00:00");

    if (!cocis_registry_add(&app->registry, &user)) {
        printf("adduser failed (duplicate or table full)\n");
        return;
    }

    hub_app_save_users(app);
    hub_app_audit(app, actor_name(app), "USER_ADD", user.uid, "", user.name, "OK");
    printf("added %s (%s)\n", user.uid, hub_role_str(user.role));
}

static void cmd_deluser(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_ADMIN) || argc < 2) {
        return;
    }
    if (!cocis_registry_remove(&app->registry, argv[1])) {
        printf("no such uid\n");
        return;
    }
    hub_app_save_users(app);
    hub_app_audit(app, actor_name(app), "USER_DEL", argv[1], "", "", "OK");
    printf("removed %s\n", argv[1]);
}

static void cmd_set_status(hub_app_t *app, int argc, char **argv,
                           cocis_user_status_t status)
{
    if (!require_role(app, COCIS_ROLE_ADMIN) || argc < 2) {
        return;
    }
    if (!cocis_registry_set_status(&app->registry, argv[1], status)) {
        printf("no such uid\n");
        return;
    }
    hub_app_save_users(app);
    hub_app_audit(app, actor_name(app),
                  status == COCIS_USER_SUSPENDED ? "USER_SUSPEND" : "USER_RESUME",
                  argv[1], "", hub_status_str(status), "OK");
    printf("%s is now %s\n", argv[1], hub_status_str(status));
}

static void cmd_scan(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_SECURITY)) {
        return;
    }
    if (argc < 3) {
        printf("usage: scan in|out <uid>\n");
        return;
    }

    cocis_dir_t direction;
    if (strcasecmp(argv[1], "in") == 0 || strcasecmp(argv[1], "entry") == 0) {
        direction = COCIS_DIR_ENTRY;
    } else if (strcasecmp(argv[1], "out") == 0 || strcasecmp(argv[1], "exit") == 0) {
        direction = COCIS_DIR_EXIT;
    } else {
        printf("direction must be in or out\n");
        return;
    }
    hub_app_scan(app, argv[2], direction);
}

static void cmd_schedule(hub_app_t *app, int argc, char **argv)
{
    if (argc == 1) {
        for (int day = 0; day < COCIS_WEEKDAY_COUNT; day++) {
            const cocis_day_t *window = &app->schedule.days[day];
            if (window->closed) {
                printf("%s closed\n", day_name(day));
            } else {
                printf("%s %02u:%02u-%02u:%02u\n", day_name(day),
                       (unsigned)(window->open_min / 60), (unsigned)(window->open_min % 60),
                       (unsigned)(window->close_min / 60), (unsigned)(window->close_min % 60));
            }
        }
        for (size_t i = 0; i < app->schedule.holiday_count; i++) {
            printf("holiday %s\n", app->schedule.holidays[i]);
        }
        printf("version %lu\n", (unsigned long)app->schedule.version);
        return;
    }

    if (!require_role(app, COCIS_ROLE_ADMIN)) {
        return;
    }

    cocis_schedule_t updated = app->schedule;

    if (strcasecmp(argv[1], "set") == 0 && argc >= 5) {
        cocis_weekday_t day = parse_day(argv[2]);
        if (day == COCIS_WEEKDAY_COUNT) {
            printf("bad day\n");
            return;
        }
        if (!cocis_schedule_set_day(&updated, day, argv[3], argv[4])) {
            printf("bad or crossing window (open must be before close)\n");
            return;
        }
    } else if (strcasecmp(argv[1], "close") == 0 && argc >= 3) {
        cocis_weekday_t day = parse_day(argv[2]);
        if (day == COCIS_WEEKDAY_COUNT) {
            printf("bad day\n");
            return;
        }
        cocis_schedule_close_day(&updated, day);
    } else {
        printf("usage: schedule set <day> <open> <close> | schedule close <day>\n");
        return;
    }

    hub_app_apply_schedule(app, &updated);
    printf("schedule updated to v%lu\n", (unsigned long)app->schedule.version);
}

static void cmd_holiday(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_ADMIN) || argc < 3) {
        printf("usage: holiday add|del <YYYY-MM-DD>\n");
        return;
    }

    cocis_schedule_t updated = app->schedule;
    bool ok;
    if (strcasecmp(argv[1], "add") == 0) {
        ok = cocis_schedule_add_holiday(&updated, argv[2]);
    } else if (strcasecmp(argv[1], "del") == 0) {
        ok = cocis_schedule_del_holiday(&updated, argv[2]);
    } else {
        printf("usage: holiday add|del <YYYY-MM-DD>\n");
        return;
    }

    if (!ok) {
        printf("holiday change rejected (bad date, duplicate or full list)\n");
        return;
    }

    hub_app_apply_schedule(app, &updated);
    printf("holiday list updated\n");
}

static void cmd_occupancy(hub_app_t *app)
{
    printf("%u inside\n", (unsigned)cocis_occupancy_count(&app->occupancy));
    for (size_t i = 0; i < app->occupancy.count; i++) {
        const cocis_occupant_t *person = cocis_occupancy_at(&app->occupancy, i);
        printf("  %-12s since %lu\n", person->uid, (unsigned long)person->since_utc);
    }
}

static void cmd_rollcall(hub_app_t *app)
{
    if (!require_role(app, COCIS_ROLE_SECURITY)) {
        return;
    }
    printf("roll call (%u captured)\n", (unsigned)cocis_roll_call_count(&app->roll_call));
    for (size_t i = 0; i < app->roll_call.count; i++) {
        printf("  %-12s\n", app->roll_call.people[i].uid);
    }
}

static void cmd_emergency(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_SECURITY) || argc < 2) {
        printf("usage: emergency on|off\n");
        return;
    }

    if (strcasecmp(argv[1], "on") == 0) {
        hub_app_set_emergency(app, true, "CLI");
        printf("emergency mode ON, doors released\n");
    } else if (strcasecmp(argv[1], "off") == 0) {
        hub_app_set_emergency(app, false, "CLI");
        printf("emergency mode OFF\n");
    } else {
        printf("usage: emergency on|off\n");
    }
}

static void cmd_door(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_ADMIN) || argc < 2) {
        printf("usage: door entry|exit\n");
        return;
    }

    hub_door_t door;
    if (strcasecmp(argv[1], "entry") == 0) {
        door = HUB_DOOR_ENTRY;
    } else if (strcasecmp(argv[1], "exit") == 0) {
        door = HUB_DOOR_EXIT;
    } else {
        printf("usage: door entry|exit\n");
        return;
    }

    hub_hal_set_door(door, true);
    hub_hal_beep(120);
    cocis_door_fsm_t *fsm = door == HUB_DOOR_ENTRY ? &app->door_entry : &app->door_exit;
    cocis_door_fsm_request(fsm, hub_now_ms());

    hub_app_audit(app, actor_name(app), "REMOTE_UNLOCK",
                  door == HUB_DOOR_ENTRY ? "entry" : "exit", "", "", "OK");
    printf("door released for %u ms\n", (unsigned)COCIS_DOOR_OPEN_MS);
}

/** Print the last `lines` records of a log file (whole file fits in RAM). */
static void print_file_tail(const char *path, unsigned lines)
{
    static char buffer[8192];
    size_t length = 0;

    esp_err_t err = storage_read(path, buffer, sizeof(buffer), &length);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("%s: no records yet\n", path);
        return;
    }
    if (err != ESP_OK) {
        printf("%s: %s\n", path, esp_err_to_name(err));
        return;
    }

    size_t end = length;
    while (end > 0 && (buffer[end - 1] == '\n' || buffer[end - 1] == '\r')) {
        end--;
    }

    size_t start = 0;
    unsigned seen = 0;
    for (size_t i = end; i > 0; i--) {
        if (buffer[i - 1] == '\n' && seen++ == lines) {
            start = i;
            break;
        }
    }

    if (end > start) {
        fwrite(buffer + start, 1, end - start, stdout);
        printf("\n");
    } else {
        printf("no records\n");
    }
}

/** Build today's log path, or parse an optional YYYYMMDD argument. */
static bool log_path(const char *kind, int argc, char **argv, char *path,
                     size_t path_len)
{
    char date[9];
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, sizeof(date), "%Y%m%d", &utc);

    if (argc >= 2) {
        if (strlen(argv[1]) != 8) {
            printf("date must be YYYYMMDD\n");
            return false;
        }
        for (int i = 0; i < 8; i++) {
            if (!isdigit((unsigned char)argv[1][i])) {
                printf("date must be YYYYMMDD\n");
                return false;
            }
        }
        snprintf(date, sizeof(date), "%s", argv[1]);
    }

    snprintf(path, path_len, "logs/%s/%s.csv", kind, date);
    return true;
}

static void cmd_log(hub_app_t *app, const char *kind, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_SECURITY)) {
        return;
    }

    char path[64];
    if (!log_path(kind, argc, argv, path, sizeof(path))) {
        return;
    }

    printf("== %s ==\n", path);
    print_file_tail(path, 20);
}

static void cmd_verify(hub_app_t *app)
{
    if (!require_role(app, COCIS_ROLE_SECURITY)) {
        return;
    }

    char date[9];
    char stamp[20];
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, sizeof(date), "%Y%m%d", &utc);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &utc);
    (void)stamp;

    char path[64];
    snprintf(path, sizeof(path), "logs/access/%s.csv", date);

    cocis_log_verify_t result;
    esp_err_t err = cocis_log_store_verify(path, NULL, &result);
    if (err != ESP_OK) {
        printf("verify: %s\n", esp_err_to_name(err));
        return;
    }

    if (result.ok) {
        printf("verify: %u records OK\n", (unsigned)result.lines);
        hub_app_audit(app, actor_name(app), "LOG_VERIFY", path, "", "", "OK");
    } else {
        printf("verify: TAMPERED at line %u\n", (unsigned)result.bad_line);
        hub_app_audit(app, actor_name(app), "LOG_VERIFY", path, "", "", "TAMPER");
        hub_app_publish_event(app, "alert", "{\"kind\":\"log_tamper\"}");
    }
}

static void cmd_nodes(hub_app_t *app)
{
    printf("%-6s %-10s %-10s\n", "NODE", "LINK", "SCHEDULE");
    printf("%-6s %-10s v%lu\n", "HUB",
           cocis_net_connected(&app->net) ? "online" : "offline",
           (unsigned long)app->schedule.version);
    for (int room = 1; room <= 2; room++) {
        const hub_room_t *state = &app->rooms[room];
        if (!state->known) {
            printf("R%-5d %-10s unknown\n", room, "no data");
        } else {
            printf("R%-5d %-10s %s\n", room,
                   hub_app_room_online(app, room) ? "online" : "offline",
                   state->occupied ? "occupied" : "vacant");
        }
    }
}

static void cmd_rooms(hub_app_t *app)
{
    for (int room = 1; room <= 2; room++) {
        const hub_room_t *state = &app->rooms[room];
        if (!state->known) {
            printf("Room %d: no telemetry yet\n", room);
            continue;
        }
        uint32_t age = (hub_now_ms() - state->last_seen_ms) / 1000u;
        printf("Room %d: %s, %s, lamp %s, %d lux, last seen %lu s ago\n",
               room,
               hub_app_room_online(app, room) ? "online" : "offline",
               state->occupied ? "occupied" : "vacant",
               state->lamp ? "on" : "off",
               state->lux, (unsigned long)age);
    }
}

static void cmd_alerts(hub_app_t *app)
{
    printf("Denials (threshold %d)\n", COCIS_DENIAL_THRESHOLD);
    if (app->alerts.count == 0) {
        printf("  none\n");
    }
    for (size_t i = 0; i < app->alerts.count; i++) {
        printf("  %-12s %u\n", app->alerts.entries[i].uid,
               (unsigned)app->alerts.entries[i].denials);
    }
    printf("Tamper : %s\n", app->alerts.have_tamper ? "seen" : "clear");
}

static void cmd_release(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_ADMIN)) {
        return;
    }
    if (argc < 2) {
        printf("usage: release <uid>\n");
        return;
    }
    if (hub_app_release(app, argv[1], actor_name(app))) {
        printf("released %s through the exit\n", argv[1]);
    } else {
        printf("%s is not inside\n", argv[1]);
    }
}

static void cmd_lights(hub_app_t *app, int argc, char **argv)
{
    if (!require_role(app, COCIS_ROLE_SECURITY)) {
        return;
    }
    if (argc < 3) {
        printf("usage: lights 1|2|all on|off|auto\n");
        return;
    }

    const char *mode = argv[2];
    if (strcasecmp(mode, "on") != 0 && strcasecmp(mode, "off") != 0 &&
        strcasecmp(mode, "auto") != 0) {
        printf("mode must be on, off or auto\n");
        return;
    }

    if (strcasecmp(argv[1], "all") == 0) {
        hub_app_set_light(app, 1, mode, actor_name(app));
        hub_app_set_light(app, 2, mode, actor_name(app));
        printf("lights R1/R2 -> %s\n", mode);
    } else {
        int room = atoi(argv[1]);
        if (room < 1 || room > 2) {
            printf("room must be 1, 2 or all\n");
            return;
        }
        hub_app_set_light(app, room, mode, actor_name(app));
        printf("lights R%d -> %s\n", room, mode);
    }
}

static void cmd_config(hub_app_t *app)
{
    if (!require_role(app, COCIS_ROLE_ADMIN)) {
        return;
    }
    printf("door open  : %u ms\n", (unsigned)COCIS_DOOR_OPEN_MS);
    printf("vacancy    : %d s\n", COCIS_T_VAC_S);
    printf("warning    : %d s\n", COCIS_T_WARN_S);
    printf("heartbeat  : %d s\n", COCIS_HEARTBEAT_S);
    printf("lux on/off : %d / %d\n", COCIS_LUX_ON, COCIS_LUX_OFF);
    printf("schedule   : v%lu, %u holidays\n",
           (unsigned long)app->schedule.version,
           (unsigned)app->schedule.holiday_count);
    printf("users      : %u\n", (unsigned)app->registry.count);
}

static void cmd_diag(hub_app_t *app)
{
    hub_diag_t diag = hub_hal_diag();
    printf("gpio      : %s\n", diag.gpio_ok ? "ok" : "fail");
    printf("servo     : %s\n", diag.servo_ok ? "ok" : "fail");
    printf("buzzer    : %s\n", diag.buzzer_ok ? "ok" : "fail");
    printf("buttons   : %s\n", diag.buttons_ok ? "ok" : "fail");
    printf("oled      : %s\n", diag.oled_ok ? "ok" : "absent");
    printf("sd card   : %s\n", storage_ready() ? "mounted" : "unavailable");
    printf("heap      : %lu bytes free\n",
           (unsigned long)esp_get_free_heap_size());
    (void)app;
}

static void cmd_net(hub_app_t *app)
{
    printf("wifi: %s, mqtt: %s\n",
           app->net.wifi_connected ? "up" : "down",
           app->net.mqtt_connected ? "up" : "down");
    printf("client: %s\n", app->net.client_id);
    printf("broker: %s\n", CONFIG_COCIS_MQTT_URI);
}

/* ------------------------------------------------------------------------- */
/* Dispatcher                                                                 */
/* ------------------------------------------------------------------------- */

static void execute(hub_app_t *app, char *line)
{
    char *argv[8] = {0};
    int argc = 0;
    char *save = NULL;
    for (char *token = strtok_r(line, " \t", &save);
         token != NULL && argc < 8;
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
            cmd_help(app);
            return;
        }
    }

    const char *command = argv[0];

    if (strcasecmp(command, "help") == 0 || strcmp(command, "?") == 0) {
        cmd_help(app);
    } else if (strcasecmp(command, "login") == 0) {
        cmd_login(app, argc, argv);
    } else if (strcasecmp(command, "logout") == 0) {
        hub_app_audit(app, actor_name(app), "LOGOUT", "console", "", "", "OK");
        cocis_auth_logout(&app->auth);
        printf("logged out\n");
    } else if (strcasecmp(command, "passwd") == 0) {
        cmd_passwd(app, argc, argv);
    } else if (strcasecmp(command, "whoami") == 0) {
        cocis_role_t role;
        if (cocis_auth_session_role(&app->auth, hub_now_ms(), &role)) {
            printf("%s (%s)\n", actor_name(app), hub_role_str(role));
        } else if (app->auth.session_user >= 0) {
            printf("password change required\n");
        } else {
            printf("anonymous\n");
        }
    } else if (strcasecmp(command, "status") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_status(app);
        }
    } else if (strcasecmp(command, "users") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_users(app);
        }
    } else if (strcasecmp(command, "adduser") == 0) {
        cmd_adduser(app, argc, argv);
    } else if (strcasecmp(command, "deluser") == 0) {
        cmd_deluser(app, argc, argv);
    } else if (strcasecmp(command, "suspend") == 0) {
        cmd_set_status(app, argc, argv, COCIS_USER_SUSPENDED);
    } else if (strcasecmp(command, "resume") == 0) {
        cmd_set_status(app, argc, argv, COCIS_USER_ACTIVE);
    } else if (strcasecmp(command, "scan") == 0) {
        cmd_scan(app, argc, argv);
    } else if (strcasecmp(command, "schedule") == 0) {
        cmd_schedule(app, argc, argv);
    } else if (strcasecmp(command, "holiday") == 0) {
        cmd_holiday(app, argc, argv);
    } else if (strcasecmp(command, "occupancy") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_occupancy(app);
        }
    } else if (strcasecmp(command, "rollcall") == 0) {
        cmd_rollcall(app);
    } else if (strcasecmp(command, "emergency") == 0) {
        cmd_emergency(app, argc, argv);
    } else if (strcasecmp(command, "door") == 0) {
        cmd_door(app, argc, argv);
    } else if (strcasecmp(command, "release") == 0) {
        cmd_release(app, argc, argv);
    } else if (strcasecmp(command, "lights") == 0) {
        cmd_lights(app, argc, argv);
    } else if (strcasecmp(command, "nodes") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_nodes(app);
        }
    } else if (strcasecmp(command, "rooms") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_rooms(app);
        }
    } else if (strcasecmp(command, "alerts") == 0) {
        if (require_role(app, COCIS_ROLE_SECURITY)) {
            cmd_alerts(app);
        }
    } else if (strcasecmp(command, "logs") == 0) {
        cmd_log(app, "access", argc, argv);
    } else if (strcasecmp(command, "audit") == 0) {
        cmd_log(app, "audit", argc, argv);
    } else if (strcasecmp(command, "diag") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_diag(app);
        }
    } else if (strcasecmp(command, "config") == 0) {
        cmd_config(app);
    } else if (strcasecmp(command, "verify") == 0) {
        cmd_verify(app);
    } else if (strcasecmp(command, "net") == 0) {
        if (require_role(app, COCIS_ROLE_VIEWER)) {
            cmd_net(app);
        }
    } else if (strcasecmp(command, "reboot") == 0) {
        if (require_role(app, COCIS_ROLE_ADMIN)) {
            hub_app_audit(app, actor_name(app), "REBOOT", "HUB", "", "", "OK");
            printf("rebooting...\n");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    } else {
        printf("unknown command: %s (try help)\n", command);
    }
}

/* ------------------------------------------------------------------------- */
/* UART reader                                                                */
/* ------------------------------------------------------------------------- */

static void cli_task(void *arg)
{
    hub_app_t *app = (hub_app_t *)arg;

    if (!uart_is_driver_installed(UART_NUM_0)) {
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0));
    }
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
    setvbuf(stdout, NULL, _IONBF, 0);

    char line[CLI_LINE_MAX];
    size_t length = 0;

    printf("\nCoCIS Entrance Hub console. Type `help`.\nHUB> ");

    while (true) {
        uint8_t ch;
        int read = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (read <= 0) {
            continue;
        }

        /* A terminal may send CRLF; do not treat the LF as a second line. */
        if (s_swallow_lf && ch == '\n') {
            s_swallow_lf = false;
            continue;
        }
        s_swallow_lf = (ch == '\r');

        if (ch == '\r' || ch == '\n') {
            if (length > 0) {
                line[length] = '\0';
                printf("\n");
                execute(app, line);
                length = 0;
            }
            printf("HUB> ");
        } else if (ch == 0x7f || ch == '\b') {
            if (length > 0) {
                length--;
                printf("\b \b");
            }
        } else if (isprint(ch)) {
            if (length + 1 < sizeof(line)) {
                line[length++] = (char)ch;
                printf("%c", ch);
            }
        }
    }
}

void cli_start(hub_app_t *app)
{
    ESP_LOGI(TAG, "console ready");
    xTaskCreate(cli_task, "cli_task", 8192, app, 3, NULL);
}
