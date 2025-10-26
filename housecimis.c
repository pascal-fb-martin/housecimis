/* HouseCIMIS - A simple home web service to estimate an index from CIMIS.
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <math.h>

#include "echttp.h"
#include "echttp_static.h"
#include "echttp_json.h"
#include "echttp_xml.h"
#include "houseportalclient.h"

#include "housediscover.h"
#include "houselog.h"
#include "houselog_sensor.h"

static int Debug = 0;

#define DEBUG if (Debug) printf

// Configuration items (from command line only, small or confidential data)..
//
static const char *CIMISAppKey;
static char   CIMISStationName[127];
static int    CIMISStation = 0;
static int    Et0ReferenceDaily = 21; // Max daily value in August 2024.

static int    Et0ReferenceWeekly = 0;
static int    Et0ReferenceMonthly = 0;

static int   *Et0Reference = 0;

static int    Et0Daily = 0;
static int    AccumulatedEt0Weekly = 0;
static int    AccumulatedEt0Monthly = 0;

static int   *Et0Accumulated = 0;

static int    Et0Weekly[7] = {0};
static int    Et0Monthly[32] = {0};

static int    CIMISIndexDaily = 0;
static int    CIMISIndexWeekly = 0;
static int    CIMISIndexMonthly = 0;

static int    CIMISPriority = 9;
static char   CIMISState[2] = "u";
static char   CIMISError[256] = "";
static time_t CIMISUpdate = 0;
static time_t CIMISReceived = 0;
static time_t CIMISQueried = 0;

#define MAX_CIMIS_REQUEST_PER_DAY   16
static int    TodayRequestCount = 0;
static int    TodayLimitLogged = 0;
static int    Today = 0; // year* 10000 + month*100 + day.

static const char *CIMISUrl = "https://et.water.ca.gov/api/data";

static const char CIMISEt0Path[] = ".Data.Providers[0].Records[0].DayEto.Value";

static const char *CIMISIndexType = "Weekly";
static int   *CIMISIndex = &CIMISIndexWeekly;

static void housecimis_select_index (const char *value) {
    if (!strcasecmp ("daily", value)) {
        CIMISIndexType = "Daily";
        CIMISIndex = &CIMISIndexDaily;
        Et0Reference = &Et0ReferenceDaily;
        Et0Accumulated = &Et0Daily;
    } else if (!strcasecmp ("weekly", value)) {
        CIMISIndexType = "Weekly";
        CIMISIndex = &CIMISIndexWeekly;
        Et0Reference = &Et0ReferenceWeekly;
        Et0Accumulated = &AccumulatedEt0Weekly;
    } else if (!strcasecmp ("monthly", value)) {
        CIMISIndexType = "Monthly";
        CIMISIndex = &CIMISIndexMonthly;
        Et0Reference = &Et0ReferenceMonthly;
        Et0Accumulated = &AccumulatedEt0Monthly;
    }
}

static const char *housecimis_status (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[65537];
    static char pool[65537];
    static char host[256];

    ParserToken token[1024];

    if (host[0] == 0) gethostname (host, sizeof(host));

    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", host);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long long)time(0));
    int top = echttp_json_add_object (context, root, "waterindex");
    int container = echttp_json_add_object (context, top, "status");
    echttp_json_add_string (context, container, "name", "cimis");
    echttp_json_add_string (context, container, "type", CIMISIndexType);
    echttp_json_add_string (context, container, "origin", CIMISUrl);
    echttp_json_add_string (context, container, "state", CIMISState);
    if (CIMISError[0]) {
        echttp_json_add_string (context, container, "error", CIMISError);
    } else {
       echttp_json_add_integer (context, container, "index", *CIMISIndex);
       echttp_json_add_integer (context, container, "received", (long)CIMISReceived);
       echttp_json_add_integer (context, container, "updated", (long)CIMISUpdate);
       echttp_json_add_integer (context, container, "priority", (long)CIMISPriority);
       echttp_json_add_integer (context, container, "et0", (long)(*Et0Accumulated));
       echttp_json_add_integer (context, container, "et0Reference", (long)(*Et0Reference));
    }

    const char *error = echttp_json_export (context, buffer, sizeof(buffer));
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *housecimis_set (const char *method, const char *uri,
                                   const char *data, int length) {
    const char *value = echttp_parameter_get("index");
    if (value) {
        housecimis_select_index (value);
    }
    return housecimis_status (method, uri, data, length);
}

static int housecimis_convert_et0 (const char *input) {
    return lrint(atof(input) * 100);
}

static void housecimis_response
                (void *origin, int status, char *data, int length) {

    ParserToken tokens[100];
    int  count = 100;

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housecimis_response, (void *)0);
        return;
    }

    CIMISState[0] = 'e'; // Assume error.
    if (status != 200) {
        snprintf (CIMISError, sizeof(CIMISError),
                  "HTTP %d on %s", status, CIMISUrl);
        houselog_trace (HOUSE_FAILURE, "HTTP",
                        "ERROR %d on %s", status, CIMISUrl);
        return;
    }
    DEBUG ("Response: %s\n", data);

    // When CIMIS rejects a request, it may return an HTTP body
    // that provides a support ID to call the CIMIS support with.
    // Warning: CIMIS logs are erased after a day or two, so the
    // ID's lifetime is very short.
    //
    const char *content = echttp_attribute_get ("Content-Type");
    if ((!content) || (!strcmp (content, "text/html"))) {
        char *supportid = strstr (data, "Your support ID is: ");
        if (supportid) {
            supportid = strchr (supportid, ':') + 2; // Skip ": ".
            char *endid = strchr (supportid, '<');
            if (endid) {
                *endid = 0;
                houselog_event ("CIMIS", "INDEX", "ERROR",
                                "REQUEST REJECTED, SUPPORT ID %s", supportid);
                snprintf (CIMISError, sizeof(CIMISError),
                          "REQUEST REJECTED, ID %s", supportid);
            }
        }
        return;
    }

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        snprintf (CIMISError, sizeof(CIMISError),
                  "JSON syntax error %s", error);
        DEBUG ("Error: %s\n", CIMISError);
        houselog_trace (HOUSE_FAILURE, "JSON", "SYNTAX ERROR %s", error);
        return;
    }

    if (count <= 0) {
        snprintf (CIMISError, sizeof(CIMISError), "no data");
        DEBUG ("Error: %s\n", CIMISError);
        houselog_trace (HOUSE_FAILURE, "JSON", "NO DATA");
        return;
    }

    int index = echttp_json_search (tokens, CIMISEt0Path);
    if (index <= 0) {
        snprintf (CIMISError, sizeof(CIMISError), "no daily Et0 found");
        DEBUG ("Error: %s\n", CIMISError);
        houselog_trace (HOUSE_FAILURE, "JSON", CIMISError);
        return;
    }
    Et0Daily = housecimis_convert_et0 (tokens[index].value.string);

    // Record this new Et0 and then calculate our three indexes.
    //
    time_t now = time(0);
    struct tm local = *localtime (&now);

    Et0Weekly[local.tm_wday] = Et0Daily;
    Et0Monthly[local.tm_mday] = Et0Daily;

    static int Et0LastMonth = 32;
    if (local.tm_mday < Et0LastMonth) Et0Monthly[31] = 0; // New month.
    Et0LastMonth = local.tm_mday;

    CIMISIndexDaily = (100 * Et0Daily) / Et0ReferenceDaily;

    int i;
    AccumulatedEt0Weekly = 0;
    for (i = 0; i < 7; ++i) {
        if (Et0Weekly[i] > 0)
            AccumulatedEt0Weekly += Et0Weekly[i];
        else
            AccumulatedEt0Weekly += Et0Daily; // Semi-decent default.
    }
    CIMISIndexWeekly = (100 * AccumulatedEt0Weekly) / (7 * Et0ReferenceDaily);

    AccumulatedEt0Monthly = 0;
    for (i = 1; i < 32; ++i) {
        if (Et0Monthly[i] > 0)
            AccumulatedEt0Monthly += Et0Monthly[i];
        else
            AccumulatedEt0Monthly += Et0Daily; // Semi-decent default.
    }
    CIMISIndexMonthly = (100 * AccumulatedEt0Monthly) / (31 * Et0ReferenceDaily);

    CIMISState[0] = 'a'; // No error found.
    CIMISError[0] = 0;
    CIMISUpdate = now - (local.tm_hour * 3600)
                      - (local.tm_min * 60)
                      - local.tm_sec; // Beginning of day.
    CIMISReceived = now;

    CIMISQueried = 0; // Query complete.
    DEBUG ("Success.\n");
    houselog_event ("CIMIS", "INDEX", "NEW",
                    "INDEX %d%% (DAILY) %d%% (WEEKLY) %d%% (MONTHLY)", 
                    CIMISIndexDaily, CIMISIndexWeekly, CIMISIndexMonthly);
    struct timeval timestamp;
    timestamp.tv_sec = CIMISUpdate;
    timestamp.tv_usec = 0;

    char ascii[16];
    snprintf (ascii, sizeof(ascii), "%d.%02d", Et0Daily/100, Et0Daily%100);
    houselog_sensor_data (&timestamp, CIMISStationName, "et0.daily", ascii, "In");
    houselog_sensor_numeric (&timestamp, CIMISStationName, "index.daily", CIMISIndexDaily, "%");
    houselog_sensor_numeric (&timestamp, CIMISStationName, "index.weekly", CIMISIndexWeekly, "%");
    houselog_sensor_numeric (&timestamp, CIMISStationName, "index.monthly", CIMISIndexMonthly, "%");
    houselog_sensor_flush();
}

static void housecimis_background (int fd, int mode) {

    static time_t LastCall = 0;
    time_t now = time(0);
    time_t yesterday = now - (24 * 3600);

    if (now == LastCall) return;
    LastCall = now;

    houseportal_background (now);
    housediscover (now);
    houselog_background (now);
    houselog_sensor_background (now);

    if (CIMISQueried) {
        if (now < CIMISQueried + 300) return; // Do not retry too often.
    }
    CIMISQueried = now;

    // Ask only once a day and no sooner than 1 AM (give CIMIS one hour for
    // computing their daily average Et0).
    if (yesterday < CIMISUpdate + 3600) return;

    // No matter what happened, limit the number of requests per day.
    // This applies even when the CIMIS site returns errors.
    // This is independent from the CIMIS response to make this mechanism
    // simple and robust (hopefully).
    //
    struct tm local = *localtime (&now);
    int year = 1900 + local.tm_year;
    int month = 1 + local.tm_mon;
    int today = (year * 10000) + (month * 100) + local.tm_mday;
    if (today != Today) {
        TodayRequestCount = 0;
        TodayLimitLogged = 0;
        Today = today;
    }
    if (TodayRequestCount > MAX_CIMIS_REQUEST_PER_DAY) {
        if (!TodayLimitLogged) {
            TodayLimitLogged = 1;
            houselog_event ("CIMIS", "INDEX", "SUSPEND",
                            "CIMIS DAILY REQUEST LIMIT OF %d REACHED",
                            MAX_CIMIS_REQUEST_PER_DAY);
        }
        return;
    }
    TodayRequestCount += 1;

    // Everything is OK, build and issue the HTTP request.
    //
    local = *localtime (&yesterday);
    year = 1900 + local.tm_year;
    month = 1 + local.tm_mon;

    char url[1024];
    snprintf (url, sizeof(url), "%s?appkey=%s&targets=%d&startDate=%04d-%02d-%02d&endDate=%04d-%02d-%02d&dataItems=day-eto",
              CIMISUrl, CIMISAppKey, CIMISStation,
              year, month, local.tm_mday,
              year, month, local.tm_mday);

    const char *error = echttp_client ("GET", url);
    if (error) {
        snprintf (CIMISError, sizeof(CIMISError),
                  "cannot connect, %s", error);
        CIMISState[0] = 'f';
        houselog_trace (HOUSE_FAILURE, "HTTP", "CONNECTION ERROR: %s", error);
        return;
    }
    DEBUG ("Query: %s\n", url);

    echttp_attribute_set ("Accept", "application/json");

    echttp_submit (0, 0, housecimis_response, (void *)0);
    houselog_trace (HOUSE_INFO, "HTTP", "REQUEST ISSUED TO %s", CIMISUrl);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    housecimis_select_index ("weekly");

    int i;
    const char *value;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-priority=", argv[i], &value)) {
            CIMISPriority = atoi(value);
            if (CIMISPriority < 0) CIMISPriority = 0;
        } else if (echttp_option_match ("-key=", argv[i], &value)) {
            CIMISAppKey = value;
        } else if (echttp_option_match ("-station=", argv[i], &value)) {
            snprintf (CIMISStationName, sizeof(CIMISStationName),
                      "CIMIS.%s", value); // No name, use station ID.
            CIMISStation = atoi(value);
        } else if (echttp_option_match ("-reference=", argv[i], &value)) {
            Et0ReferenceDaily = atoi(value);
        } else if (echttp_option_match ("-index=", argv[i], &value)) {
            housecimis_select_index (value);
        } else if (echttp_option_present ("-d", argv[i])) {
            Debug = 1;
        }
    }
    if (!CIMISAppKey) {
        fprintf (stderr, "Missing CIMIS application key\n");
        exit(1);
    }
    if (CIMISPriority <= 0) {
        fprintf (stderr, "Invalid index priority argument\n");
        exit(2);
    }

    Et0ReferenceWeekly = Et0ReferenceDaily * 7;
    Et0ReferenceMonthly = Et0ReferenceDaily * 31;

    DEBUG ("Starting.\n");

    echttp_default ("-http-service=dynamic");
    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"waterindex:/cimis"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }

    housediscover_initialize (argc, argv);
    houselog_initialize ("cimis", argc, argv);
    houselog_sensor_initialize ("cimis", argc, argv);

    echttp_route_uri ("/cimis/set", housecimis_set);
    echttp_route_uri ("/cimis/status", housecimis_status);
    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housecimis_background);
    echttp_loop();
}

