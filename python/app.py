import os
import sqlite3
from ipaddress import ip_address
from typing import Any, Dict, Optional

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

DEFAULT_DB_PATH = os.path.normpath(
    os.path.join(
        os.path.dirname(__file__), "..", "config", "database", "WhatTimeIsIn-geoip.db"
    )
)
DB_PATH = os.getenv("GEOIP_DB_PATH", DEFAULT_DB_PATH)
DEFAULT_LOCALE = os.getenv("GEOIP_LOCALE", "en")

app = FastAPI(title="WhatTimeIsIn GeoIP Lookup")


@app.exception_handler(HTTPException)
def http_exception_handler(request: Request, exc: HTTPException) -> JSONResponse:
    return JSONResponse(
        status_code=exc.status_code,
        content={"status": exc.status_code, "detail": exc.detail},
    )


@app.exception_handler(StarletteHTTPException)
def starlette_http_exception_handler(
    request: Request, exc: StarletteHTTPException
) -> JSONResponse:
    detail = exc.detail
    if exc.status_code == 404 and exc.detail == "Not Found":
        detail = "Route not found"
    return JSONResponse(
        status_code=exc.status_code,
        content={"status": exc.status_code, "detail": detail},
    )


def _connect_db() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def _ipv6_to_db_key(ipv6_int: int) -> int:
    return ipv6_int >> 64


def _lookup_city(
    conn: sqlite3.Connection, ip_version: int, ip_key: int, locale: str
) -> Optional[Dict[str, Any]]:
    row = conn.execute(
        """
        SELECT
            b.network,
            b.prefix_length,
            b.ip_version,
            b.geoname_id,
            b.registered_country_geoname_id,
            b.represented_country_geoname_id,
            b.is_anonymous_proxy,
            b.is_satellite_provider,
            b.is_anycast,
            b.postal_code,
            b.latitude,
            b.longitude,
            b.accuracy_radius,
            l.continent_code,
            l.continent_name,
            l.country_iso_code,
            l.country_name,
            l.subdivision_1_iso_code,
            l.subdivision_1_name,
            l.subdivision_2_iso_code,
            l.subdivision_2_name,
            l.city_name,
            l.metro_code,
            l.time_zone,
            l.is_in_european_union
        FROM city_blocks b
        LEFT JOIN city_locations l
            ON l.geoname_id = b.geoname_id
            AND l.locale_code = ?
        WHERE b.ip_version = ?
          AND b.network_start <= ?
          AND b.network_end >= ?
        ORDER BY b.prefix_length DESC
        LIMIT 1
        """,
        (locale, ip_version, ip_key, ip_key),
    ).fetchone()
    return dict(row) if row else None


def _lookup_country(
    conn: sqlite3.Connection, ip_version: int, ip_key: int, locale: str
) -> Optional[Dict[str, Any]]:
    row = conn.execute(
        """
        SELECT
            b.network,
            b.prefix_length,
            b.ip_version,
            b.geoname_id,
            b.registered_country_geoname_id,
            b.represented_country_geoname_id,
            b.is_anonymous_proxy,
            b.is_satellite_provider,
            b.is_anycast,
            l.continent_code,
            l.continent_name,
            l.country_iso_code,
            l.country_name,
            l.is_in_european_union
        FROM country_blocks b
        LEFT JOIN country_locations l
            ON l.geoname_id = b.geoname_id
            AND l.locale_code = ?
        WHERE b.ip_version = ?
          AND b.network_start <= ?
          AND b.network_end >= ?
        ORDER BY b.prefix_length DESC
        LIMIT 1
        """,
        (locale, ip_version, ip_key, ip_key),
    ).fetchone()
    return dict(row) if row else None


def _lookup_asn(
    conn: sqlite3.Connection, ip_version: int, ip_key: int
) -> Optional[Dict[str, Any]]:
    row = conn.execute(
        """
        SELECT
            network,
            prefix_length,
            ip_version,
            autonomous_system_number,
            autonomous_system_organization
        FROM asn_blocks
        WHERE ip_version = ?
          AND network_start <= ?
          AND network_end >= ?
        ORDER BY prefix_length DESC
        LIMIT 1
        """,
        (ip_version, ip_key, ip_key),
    ).fetchone()
    return dict(row) if row else None


def _format_location(data: Dict[str, Any], source: str) -> Dict[str, Any]:
    country_iso = data.get("country_iso_code")
    return {
        "source": source,
        "network": {
            "cidr": data.get("network"),
            "prefix_length": data.get("prefix_length"),
            "ip_version": data.get("ip_version"),
        },
        "geo": {
            "continent": {
                "code": data.get("continent_code"),
                "name": data.get("continent_name"),
            },
            "country": {
                "iso_code": country_iso,
                "name": data.get("country_name"),
                "flag_emoji": _iso_to_flag(country_iso),
                "is_in_european_union": data.get("is_in_european_union"),
            },
            "subdivision_1": {
                "iso_code": data.get("subdivision_1_iso_code"),
                "name": data.get("subdivision_1_name"),
            },
            "subdivision_2": {
                "iso_code": data.get("subdivision_2_iso_code"),
                "name": data.get("subdivision_2_name"),
            },
            "city": {
                "name": data.get("city_name"),
                "metro_code": data.get("metro_code"),
            },
            "time_zone": data.get("time_zone"),
        },
        "coordinates": {
            "latitude": data.get("latitude"),
            "longitude": data.get("longitude"),
            "accuracy_radius": data.get("accuracy_radius"),
        },
        "postal_code": data.get("postal_code"),
        "traits": {
            "is_anonymous_proxy": data.get("is_anonymous_proxy"),
            "is_satellite_provider": data.get("is_satellite_provider"),
            "is_anycast": data.get("is_anycast"),
        },
        "geoname_id": data.get("geoname_id"),
        "registered_country_geoname_id": data.get("registered_country_geoname_id"),
        "represented_country_geoname_id": data.get("represented_country_geoname_id"),
    }


def _format_asn(asn: Optional[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    if not asn:
        return None
    return {
        "network": {
            "cidr": asn.get("network"),
            "prefix_length": asn.get("prefix_length"),
            "ip_version": asn.get("ip_version"),
        },
        "number": asn.get("autonomous_system_number"),
        "organization": asn.get("autonomous_system_organization"),
    }


def _iso_to_flag(iso_code: Optional[str]) -> Optional[str]:
    if not iso_code or len(iso_code) != 2:
        return None
    base = 0x1F1E6
    return chr(base + ord(iso_code[0].upper()) - ord("A")) + chr(
        base + ord(iso_code[1].upper()) - ord("A")
    )


@app.get("/lookup")
def lookup(ip: str = Query(..., description="IPv4 or IPv6")) -> Dict[str, Any]:
    try:
        parsed_ip = ip_address(ip)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail="Invalid IP address") from exc

    ip_version = 6 if parsed_ip.version == 6 else 4
    ip_key = (
        _ipv6_to_db_key(int(parsed_ip)) if parsed_ip.version == 6 else int(parsed_ip)
    )

    with _connect_db() as conn:
        asn_result = _lookup_asn(conn, ip_version, ip_key)
        city_result = _lookup_city(conn, ip_version, ip_key, DEFAULT_LOCALE)
        if city_result:
            return {
                "status": 200,
                "ip": ip,
                "ip_version": ip_version,
                "location": _format_location(city_result, "city"),
                "asn": _format_asn(asn_result),
                "message": "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.",
            }

        country_result = _lookup_country(conn, ip_version, ip_key, DEFAULT_LOCALE)
        if country_result:
            return {
                "status": 200,
                "ip": ip,
                "ip_version": ip_version,
                "location": _format_location(country_result, "country"),
                "asn": _format_asn(asn_result),
                "message": "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.",
            }

    raise HTTPException(status_code=404, detail="IP not found in ranges")


if __name__ == "__main__":
    import uvicorn

    port = int(os.getenv("GEOIP_PORT", "5022"))
    uvicorn.run(app, host="0.0.0.0", port=port)
