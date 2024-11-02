/* HouseCMIS - A simple home web service to estimate an index from CMIS.
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

static int Debug = 0;

#define DEBUG if (Debug) printf

// Configuration items (from command line only, small or confidential data)..
//
static const char *CMISAppKey;
static int    CMISStation = 0;
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

static int    CMISIndexDaily = 0;
static int    CMISIndexWeekly = 0;
static int    CMISIndexMonthly = 0;

static int    CMISPriority = 9;
static char   CMISState[2] = "u";
static char   CMISError[256] = "";
static time_t CMISUpdate = 0;
static time_t CMISReceived = 0;
static time_t CMISQueried = 0;

static const char *CMISUrl = "https://et.water.ca.gov/api/data";

static const char CMISEt0Path[] = ".Data.Providers[0].Records[0].DayEto.Value";

static const char *CMISIndexType = "Weekly";
static int   *CMISIndex = &CMISIndexWeekly;

static void housecmis_select_index (const char *value) {
    if (!strcasecmp ("daily", value)) {
        CMISIndexType = "Daily";
        CMISIndex = &CMISIndexDaily;
        Et0Reference = &Et0ReferenceDaily;
        Et0Accumulated = &Et0Daily;
    } else if (!strcasecmp ("weekly", value)) {
        CMISIndexType = "Weekly";
        CMISIndex = &CMISIndexWeekly;
        Et0Reference = &Et0ReferenceWeekly;
        Et0Accumulated = &AccumulatedEt0Weekly;
    } else if (!strcasecmp ("monthly", value)) {
        CMISIndexType = "Monthly";
        CMISIndex = &CMISIndexMonthly;
        Et0Reference = &Et0ReferenceMonthly;
        Et0Accumulated = &AccumulatedEt0Monthly;
    }
}

static const char *housecmis_status (const char *method, const char *uri,
                                     const char *data, int length) {
    static char buffer[65537];
    static char pool[65537];
    static char host[256];
    static char state[2];

    ParserToken token[1024];

    if (host[0] == 0) gethostname (host, sizeof(host));

    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", host);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long)time(0));
    int top = echttp_json_add_object (context, root, "waterindex");
    int container = echttp_json_add_object (context, top, "status");
    echttp_json_add_string (context, container, "name", "cmis");
    echttp_json_add_string (context, container, "type", CMISIndexType);
    echttp_json_add_string (context, container, "origin", CMISUrl);
    echttp_json_add_string (context, container, "state", CMISState);
    if (CMISError[0]) {
        echttp_json_add_string (context, container, "error", CMISError);
    } else {
       echttp_json_add_integer (context, container, "index", *CMISIndex);
       echttp_json_add_integer (context, container, "received", (long)CMISReceived);
       echttp_json_add_integer (context, container, "updated", (long)CMISUpdate);
       echttp_json_add_integer (context, container, "priority", (long)CMISPriority);
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

static const char *housecmis_set (const char *method, const char *uri,
                                  const char *data, int length) {
    const char *value = echttp_parameter_get("index");
    if (value) {
        housecmis_select_index (value);
    }
    return housecmis_status (method, uri, data, length);
}

static int housecmis_convert_et0 (const char *input) {
    return lrint(atof(input) * 100);
}

static void housecmis_response
                (void *origin, int status, char *data, int length) {

    ParserToken tokens[100];
    int  count = 100;

    status = echttp_redirected("GET");
    if (!status) {
        echttp_submit (0, 0, housecmis_response, (void *)0);
        return;
    }

    CMISState[0] = 'e'; // Assume error.
    if (status != 200) {
        snprintf (CMISError, sizeof(CMISError),
                  "HTTP %d on %s", status, CMISUrl);
        return;
    }
    DEBUG ("Response: %s\n", data);

    const char *error = echttp_json_parse (data, tokens, &count);
    if (error) {
        snprintf (CMISError, sizeof(CMISError),
                  "XML syntax error %s", error);
        DEBUG ("Error: %s\n", CMISError);
        return;
    }
    if (count <= 0) {
        snprintf (CMISError, sizeof(CMISError), "no data");
        DEBUG ("Error: %s\n", CMISError);
        return;
    }

    int index = echttp_json_search (tokens, CMISEt0Path);
    if (index <= 0) {
        snprintf (CMISError, sizeof(CMISError), "no daily Et0 found");
        DEBUG ("Error: %s\n", CMISError);
        return;
    }
    Et0Daily = housecmis_convert_et0 (tokens[index].value.string);

    // Record this new Et0 and then calculate our three indexes.
    //
    time_t now = time(0);
    struct tm local = *localtime (&now);

    Et0Weekly[local.tm_wday] = Et0Daily;
    Et0Monthly[local.tm_mday] = Et0Daily;

    static int Et0LastMonth = 32;
    if (local.tm_mday < Et0LastMonth) Et0Monthly[31] = 0; // New month.
    Et0LastMonth = local.tm_mday;

    CMISIndexDaily = (100 * Et0Daily) / Et0ReferenceDaily;

    int i;
    AccumulatedEt0Weekly = 0;
    for (i = 0; i < 7; ++i) {
        if (Et0Weekly[i] > 0)
            AccumulatedEt0Weekly += Et0Weekly[i];
        else
            AccumulatedEt0Weekly += Et0Daily; // Semi-decent default.
    }
    CMISIndexWeekly = (100 * AccumulatedEt0Weekly) / (7 * Et0ReferenceDaily);

    AccumulatedEt0Monthly = 0;
    for (i = 1; i < 32; ++i) {
        if (Et0Monthly[i] > 0)
            AccumulatedEt0Monthly += Et0Monthly[i];
        else
            AccumulatedEt0Monthly += Et0Daily; // Semi-decent default.
    }
    CMISIndexMonthly = (100 * AccumulatedEt0Monthly) / (31 * Et0ReferenceDaily);

    CMISState[0] = 'a'; // No error found.
    CMISError[0] = 0;
    CMISUpdate = now - (local.tm_hour * 3600)
                     - (local.tm_min * 60)
                     - local.tm_sec + 3600; // Beginning of day.
    CMISReceived = now;

    CMISQueried = 0; // Query complete.
    DEBUG ("Success.\n");
}

static void housecmis_background (int fd, int mode) {

    static time_t LastCall = 0;
    time_t now = time(0);
    time_t yesterday = now - (24 * 3600);

    if (now == LastCall) return;
    LastCall = now;

    if (echttp_dynamic_port()) {
        static time_t Renewed = 0;
        if (Renewed) {
            if (now > Renewed + 60) {
                houseportal_renew();
                Renewed = now;
            }
        } else if (now % 5 == 0) {
            static const char *path[] = {"waterindex:/cmis"};
            houseportal_register (echttp_port(4), path, 1);
            Renewed = now;
        }
    }

    if (CMISQueried) {
        if (now < CMISQueried + 300) return; // Do not retry too often.
    }
    CMISQueried = now;

    if (yesterday < CMISUpdate + 24 * 3600) return; // Ask only once a day.

    struct tm local = *localtime (&yesterday);

    int year = 1900 + local.tm_year;
    int month = 1 + local.tm_mon;

    char url[1024];
    snprintf (url, sizeof(url), "%s?appkey=%s&targets=%d&startDate=%04d-%02d-%02d&endDate=%04d-%02d-%02d&dataItems=day-eto",
              CMISUrl, CMISAppKey, CMISStation,
              year, month, local.tm_mday,
              year, month, local.tm_mday);

    const char *error = echttp_client ("GET", url);
    if (error) {
        snprintf (CMISError, sizeof(CMISError),
                  "cannot connect, %s", error);
        CMISState[0] = 'f';
        return;
    }
    DEBUG ("Query: %s\n", url);

    echttp_attribute_set ("Accept", "application/json");

    echttp_submit (0, 0, housecmis_response, (void *)0);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    housecmis_select_index ("weekly");

    int i;
    const char *value;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-priority=", argv[i], &value)) {
            CMISPriority = atoi(value);
            if (CMISPriority < 0) CMISPriority = 0;
        } else if (echttp_option_match ("-key=", argv[i], &value)) {
            CMISAppKey = value;
        } else if (echttp_option_match ("-station=", argv[i], &value)) {
            CMISStation = atoi(value);
        } else if (echttp_option_match ("-reference=", argv[i], &value)) {
            Et0ReferenceDaily = atoi(value);
        } else if (echttp_option_match ("-index=", argv[i], &value)) {
            housecmis_select_index (value);
        } else if (echttp_option_present ("-d", argv[i])) {
            Debug = 1;
        }
    }
    if (!CMISAppKey) exit(1);
    if (!CMISPriority) exit(2);

    Et0ReferenceWeekly = Et0ReferenceDaily * 7;
    Et0ReferenceMonthly = Et0ReferenceDaily * 31;

    DEBUG ("Starting.\n");

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port())
        houseportal_initialize (argc, argv);
    echttp_route_uri ("/cmis/set", housecmis_set);
    echttp_route_uri ("/cmis/status", housecmis_status);
    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housecmis_background);
    echttp_loop();
}

