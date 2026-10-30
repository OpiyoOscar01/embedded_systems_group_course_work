/**
 * @file cli.h
 * @brief Serial admin/security console for the entrance hub.
 * @author Opiyo Oscar
 * @requirements FR-CLI-01..06, FR-AUTH-01..05
 */

#pragma once

#include "app.h"

/** Install the UART driver and start the console task. */
void cli_start(hub_app_t *app);
