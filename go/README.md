# WhatTimeIsIn GeoIP (Go)

Free, local GeoIP API using Go and SQLite.

---

## Requirements

- Go 1.21+
- CGO enabled (required by `github.com/mattn/go-sqlite3`)

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
