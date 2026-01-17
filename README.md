# WhatTimeIsIn GeoIP

A free, local GeoIP API that provides location, timezone, and ASN information for IPv4 and IPv6 without relying on external services. All lookups are resolved using a local SQLite database, keeping IP data private and avoiding third-party calls.

---

## Goals

- Free and local IP geolocation API
- Privacy-friendly (no external network calls)
- Simple to run across multiple languages
- Consistent JSON response across implementations

---

## Available implementations

- Python (`/python`) – FastAPI
- Go (`/go`)
- PHP (`/php`) – Built-in server
- Node.js + TypeScript (`/node`) – Express
- C++ (`/cpp`)
- C# (`/csharp`) – .NET Minimal API
- Rust (`/rust`) – Axum
- Ruby (`/ruby`) – Sinatra

Each folder includes its own `README.md` and `start.sh` with setup instructions.

---

## Database

Download the database from:

```
https://whattimeis.in/public/downloads/
```

Save it into the project root at:

```
config/database/WhatTimeIsIn-geoip.db
```

Data is consolidated from the MaxMind IP database and updated monthly.

---

## Common API Contract

All implementations expose:

- Endpoint: `GET /lookup`
- Query parameter: `ip` (IPv4 or IPv6)
- Response format: JSON

---

## Attribution

If you use this service or its data in your project, please include a reference to:

https://whattimeis.in
