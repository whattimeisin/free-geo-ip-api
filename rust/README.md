# WhatTimeIsIn GeoIP (Rust)

Free, local GeoIP API using Rust and SQLite.

---

## Requirements

- Rust (cargo)

---

## Run

From the language folder:

```bash
cd rust
chmod +x ./start.sh
./start.sh
```

The API will be available at:

```
http://localhost:5022
```

---

## Database download

Download the database from:

```
https://whattimeis.in/public/downloads/
```

Save it into the project root at:

```
config/database/WhatTimeIsIn-geoip.db
```
# WhatTimeIsIn GeoIP (Rust)

A lightweight, privacy-friendly GeoIP lookup service built with Rust.
It provides location and timezone-related information for both IPv4 and IPv6
addresses using a **local SQLite database**, with no external API calls.

This service is designed to be fast, simple to deploy, and suitable for
projects that need reliable timezone detection without sending user IPs
to third-party services.

---

## Overview

WhatTimeIsIn GeoIP consolidates geolocation and ASN data from the
**MaxMind IP database**, processed and stored locally for efficient lookups.

All queries are resolved locally, making this service ideal for
privacy-conscious applications and low-cost infrastructure setups.

---

## Data source & updates

- Location and ASN data are consolidated from the **MaxMind IP database**
- The database is **updated once per month** to reflect:
  - New IP block allocations
  - Changes in ownership (ASN)
  - Location and timezone corrections
- Accuracy may vary by region and depends on the quality of the upstream data

Keeping the database up to date is strongly recommended for best results.

---

## Scope & limitations

- Intended for **general geolocation and timezone detection**
- Not designed for precise or address-level accuracy
- Results may change over time as IP ranges are reassigned
- Some IPs may resolve only to country-level data

---

## Requirements

- Rust (cargo)

---

## Install Rust

On macOS/Linux:

- https://rustup.rs

On Windows:

- https://rustup.rs

---

## Run

From the language folder:

```bash
chmod +x ./start.sh
./start.sh
```

The API will be available at:

```
http://localhost:5022
```

---

## Database download

Download the database from:

```
https://whattimeis.in/public/downloads/
```

Save it into the project root at:

```
config/database/WhatTimeIsIn-geoip.db
```

---

## API contract

- **Endpoint:** `GET /lookup`
- **Query parameters:**
  - `ip` (string, required): IPv4 or IPv6 address
- **Response format:** JSON
- **Character encoding:** UTF-8

---

## Usage

Lookup an IP address:

```bash
curl "http://localhost:5022/lookup?ip=1.178.1.0"
curl "http://localhost:5022/lookup?ip=2001:0218:6002:0000:0000:0000:0000:0000"
```

Postman (GET):

- **Method:** `GET`
- **URL:** `http://localhost:5022/lookup`
- **Params:** `ip=1.178.1.0`

---

## Example response

```json
{
  "status": 200,
  "ip": "2001:0218:6002:0000:0000:0000:0000:0000",
  "ip_version": 6,
  "location": {
    "source": "city",
    "network": {
      "cidr": "2001:218:6002::/48",
      "prefix_length": 48,
      "ip_version": 6
    },
    "geo": {
      "continent": {
        "code": "AS",
        "name": "Asia"
      },
      "country": {
        "iso_code": "HK",
        "name": "Hong Kong",
        "flag_emoji": "🇭🇰",
        "is_in_european_union": 0
      },
      "subdivision_1": {
        "iso_code": null,
        "name": null
      },
      "subdivision_2": {
        "iso_code": null,
        "name": null
      },
      "city": {
        "name": "Hong Kong",
        "metro_code": null
      },
      "time_zone": "Asia/Hong_Kong"
    },
    "coordinates": {
      "latitude": 22.2842,
      "longitude": 114.1759,
      "accuracy_radius": 20
    },
    "postal_code": null,
    "traits": {
      "is_anonymous_proxy": 0,
      "is_satellite_provider": 0,
      "is_anycast": 0
    },
    "geoname_id": 1819729,
    "registered_country_geoname_id": 1861060,
    "represented_country_geoname_id": null
  },
  "asn": {
    "network": {
      "cidr": "2001:218:4000::/34",
      "prefix_length": 34,
      "ip_version": 6
    },
    "number": 2914,
    "organization": "NTT-DATA-2914"
  },
  "message": "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates."
}
```

---

## Error handling

The API uses standard HTTP status codes:

- `400 Bad Request` for invalid IPs or missing `ip` parameter
- `404 Not Found` when no matching range is found or route does not exist

Example error responses:

```json
{
  "status": 400,
  "detail": "Invalid IP address"
}
```

```json
{
  "status": 404,
  "detail": "IP not found in ranges"
}
```

```json
{
  "status": 404,
  "detail": "Route not found"
}
```

---

## Environment variables

- `GEOIP_DB_PATH`: override the default database path
- `GEOIP_LOCALE`: locale for location names (default: `en`)
- `GEOIP_PORT`: API port (default: `5022`)

---

## Attribution

If you use this service or its data in your project, please include a reference
to the main project:

**https://whattimeis.in**

Attribution helps keep the database updated monthly and supports continued
development of new blocks and improvements.

---

Maintained by the **WhatTimeIsIn** project  
https://whattimeis.in
