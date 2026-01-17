# WhatTimeIsIn GeoIP (Python)

Free, local GeoIP API using FastAPI and a local SQLite database.

---

## Requirements

- Python 3.10+

---

## Run

From the repository root:

```bash
chmod +x ./python/start.sh
./python/start.sh
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

- Endpoint: `GET /lookup`
- Query parameter: `ip` (IPv4 or IPv6)
- Response format: JSON

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
      "continent": { "code": "AS", "name": "Asia" },
      "country": {
        "iso_code": "HK",
        "name": "Hong Kong",
        "flag_emoji": "🇭🇰",
        "is_in_european_union": 0
      },
      "subdivision_1": { "iso_code": null, "name": null },
      "subdivision_2": { "iso_code": null, "name": null },
      "city": { "name": "Hong Kong", "metro_code": null },
      "time_zone": "Asia/Hong_Kong"
    },
    "coordinates": { "latitude": 22.2842, "longitude": 114.1759, "accuracy_radius": 20 },
    "postal_code": null,
    "traits": { "is_anonymous_proxy": 0, "is_satellite_provider": 0, "is_anycast": 0 },
    "geoname_id": 1819729,
    "registered_country_geoname_id": 1861060,
    "represented_country_geoname_id": null
  },
  "asn": {
    "network": { "cidr": "2001:218:4000::/34", "prefix_length": 34, "ip_version": 6 },
    "number": 2914,
    "organization": "NTT-DATA-2914"
  },
  "message": "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates."
}
```
