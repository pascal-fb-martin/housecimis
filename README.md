# HouseCMIS

## Overview

A micro service that interfaces with the CMIS web API to get the daily Et0 and calculates daily, weekly and monthly watering indexes.

This project depends on [echttp](https://github.com/pascal-fb-martin/echttp) and [houseportal](https://github.com/pascal-fb-martin/houseportal). It accepts all standard options of echttp and the houseportal client runtime. See these two projects for more information.

This project is also an example of a minimal micro-service based on echttp and houseportal.

## Command line options.

The HouseCMIS service is entirely configured from the command line. This is because there are only a handful of configuration items, and one of them (the access key) is confidential and must be accessible only on the local machine.

In addition to the echttp standard options, this service supports the following command line options:
* -key=STRING: the CMIS API key. This is a mandatory argument, there is no default.
* -station=NUMBER: the ID of the CMIS station used for calculating Et0. This is a mandatory argument, there is no default.
* -priority=NUMBER: the priority to assign to this watering index. Default is 9.
* -reference=NUMBER: the reference daily Et0, in 1/100 of an inch. Default is 21 (don't ask.. it seems a reasonable value for my location).
* -index=daily|weekly|monthly: which watering index to return.
* -d: enables debug traces.

## Interface

The waterwise service accepts the `/cmis/status` HTTP URL and returns the watering index information in the JSON format, for example:
```
{
  "waterwise": {
    "timestamp": 1602561976,
    "host": "andresy",
    "status": {
      "origin": "https://et.water.ca.gov/api/data",
      "state": "a",
      "type": "weekly",
      "index": 52,
      "received": 1730522922,
      "updated": 1730447322,
      "et0": 77,
      "et0Reference": 148
    }
  }
}
```
The timestamp field represents the time of the request, the host field represents the name of the server hosting this micro-service.

The origin field represent the URL used to obtain the index. The state field is either 'u' (not yet initialized), 'f' (failed to access the CMIS web site), 'e' (malformed JSON data received) or 'a' (active). Except when active, an error field provides a description of the error.

The `index` field is a percentage (which can be above 100), the `received` field indicate when this index was obtained, and the `updated` field indicates an estimated time when the CMIS site calculated the latest daily Et0 (basically, on the next day).

The `et0` value is the Et0 calculated from the daily requests to CMIS. If the index type is `daily`, this is the raw value provided by CMIS. If the index type is `weekly`, this is the sum of the last seven daily Et0 received. If the index type is `monthly`, this is the sum of the last 30 or 31 Et0 received.

The `et0Reference` value is the daily, weekly or monthly Et0 reference value derived from the `-reference` command line option.

The service also accepts the "/cmis/set?index=[daily|weekly|monthly]" request, which changes which index is returned by the "/cmis/ststus" request.

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network host` option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

