use axum::{
    extract::Query,
    http::StatusCode,
    response::{IntoResponse, Json, Response},
    routing::get,
    Router,
};
use rusqlite::{params, Connection};
use serde::Serialize;
use std::{collections::HashMap, env, net::IpAddr, path::PathBuf};

const MESSAGE_TEXT: &str = "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.";

#[tokio::main]
async fn main() {
    let port = env::var("GEOIP_PORT").unwrap_or_else(|_| "5022".to_string());
    let app = Router::new().route("/lookup", get(lookup_handler)).fallback(fallback);

    let addr = format!("0.0.0.0:{}", port);
    println!("GeoIP API running on http://{}", addr);
    axum::serve(tokio::net::TcpListener::bind(addr).await.unwrap(), app)
        .await
        .unwrap();
}

async fn fallback() -> Response {
    json_error(StatusCode::NOT_FOUND, "Route not found")
}

async fn lookup_handler(Query(params): Query<HashMap<String, String>>) -> Response {
    let ip = match params.get("ip") {
        Some(value) if !value.is_empty() => value.clone(),
        _ => return json_error(StatusCode::BAD_REQUEST, "Missing ip parameter"),
    };

    let (ip_version, ip_key) = match parse_ip(&ip) {
        Some(value) => value,
        None => return json_error(StatusCode::BAD_REQUEST, "Invalid IP address"),
    };

    let db_path = db_path();
    if !db_path.exists() {
        return json_error(StatusCode::INTERNAL_SERVER_ERROR, "Database file not found");
    }

    let conn = match Connection::open(db_path) {
        Ok(conn) => conn,
        Err(_) => {
            return json_error(StatusCode::INTERNAL_SERVER_ERROR, "Database open failed");
        }
    };

    let asn = lookup_asn(&conn, ip_version, ip_key);
    if let Some(city) = lookup_city(&conn, ip_version, ip_key) {
        let response = ResponsePayload {
            status: 200,
            ip,
            ip_version,
            location: format_location_city(city, "city"),
            asn: format_asn(asn),
            message: MESSAGE_TEXT.to_string(),
        };
        return (StatusCode::OK, Json(response)).into_response();
    }

    if let Some(country) = lookup_country(&conn, ip_version, ip_key) {
        let response = ResponsePayload {
            status: 200,
            ip,
            ip_version,
            location: format_location_country(country, "country"),
            asn: format_asn(asn),
            message: MESSAGE_TEXT.to_string(),
        };
        return (StatusCode::OK, Json(response)).into_response();
    }

    json_error(StatusCode::NOT_FOUND, "IP not found in ranges")
}

fn db_path() -> PathBuf {
    if let Ok(path) = env::var("GEOIP_DB_PATH") {
        return PathBuf::from(path);
    }
    let mut path = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    path.pop();
    path.push("config");
    path.push("database");
    path.push("WhatTimeIsIn-geoip.db");
    path
}

fn parse_ip(ip: &str) -> Option<(i64, i64)> {
    let addr: IpAddr = ip.parse().ok()?;
    match addr {
        IpAddr::V4(v4) => {
            let octets = v4.octets();
            let value = u32::from_be_bytes(octets);
            Some((4, value as i64))
        }
        IpAddr::V6(v6) => {
            let octets = v6.octets();
            let high = u64::from_be_bytes([
                octets[0], octets[1], octets[2], octets[3], octets[4], octets[5], octets[6],
                octets[7],
            ]);
            if high > i64::MAX as u64 {
                None
            } else {
                Some((6, high as i64))
            }
        }
    }
}

#[derive(Debug)]
struct CityRow {
    network: String,
    prefix_length: i64,
    ip_version: i64,
    geoname_id: Option<i64>,
    registered_country_geoname_id: Option<i64>,
    represented_country_geoname_id: Option<i64>,
    is_anonymous_proxy: Option<i64>,
    is_satellite_provider: Option<i64>,
    is_anycast: Option<i64>,
    postal_code: Option<String>,
    latitude: Option<f64>,
    longitude: Option<f64>,
    accuracy_radius: Option<i64>,
    continent_code: Option<String>,
    continent_name: Option<String>,
    country_iso_code: Option<String>,
    country_name: Option<String>,
    subdivision_1_iso_code: Option<String>,
    subdivision_1_name: Option<String>,
    subdivision_2_iso_code: Option<String>,
    subdivision_2_name: Option<String>,
    city_name: Option<String>,
    metro_code: Option<String>,
    time_zone: Option<String>,
    is_in_european_union: Option<i64>,
}

#[derive(Debug)]
struct CountryRow {
    network: String,
    prefix_length: i64,
    ip_version: i64,
    geoname_id: Option<i64>,
    registered_country_geoname_id: Option<i64>,
    represented_country_geoname_id: Option<i64>,
    is_anonymous_proxy: Option<i64>,
    is_satellite_provider: Option<i64>,
    is_anycast: Option<i64>,
    continent_code: Option<String>,
    continent_name: Option<String>,
    country_iso_code: Option<String>,
    country_name: Option<String>,
    is_in_european_union: Option<i64>,
}

#[derive(Debug)]
struct AsnRow {
    network: String,
    prefix_length: i64,
    ip_version: i64,
    autonomous_system_number: Option<i64>,
    autonomous_system_organization: Option<String>,
}

fn lookup_asn(conn: &Connection, ip_version: i64, ip_key: i64) -> Option<AsnRow> {
    let mut stmt = conn
        .prepare(
            "SELECT network, prefix_length, ip_version, autonomous_system_number, autonomous_system_organization
             FROM asn_blocks
             WHERE ip_version = ?1 AND network_start <= ?2 AND network_end >= ?2
             ORDER BY prefix_length DESC LIMIT 1",
        )
        .ok()?;
    stmt.query_row(params![ip_version, ip_key], |row| {
        Ok(AsnRow {
            network: row.get(0)?,
            prefix_length: row.get(1)?,
            ip_version: row.get(2)?,
            autonomous_system_number: row.get(3)?,
            autonomous_system_organization: row.get(4)?,
        })
    })
    .ok()
}

fn lookup_city(conn: &Connection, ip_version: i64, ip_key: i64) -> Option<CityRow> {
    let locale = env::var("GEOIP_LOCALE").unwrap_or_else(|_| "en".to_string());
    let mut stmt = conn
        .prepare(
            "SELECT
                b.network, b.prefix_length, b.ip_version, b.geoname_id,
                b.registered_country_geoname_id, b.represented_country_geoname_id,
                b.is_anonymous_proxy, b.is_satellite_provider, b.is_anycast,
                b.postal_code, b.latitude, b.longitude, b.accuracy_radius,
                l.continent_code, l.continent_name, l.country_iso_code, l.country_name,
                l.subdivision_1_iso_code, l.subdivision_1_name, l.subdivision_2_iso_code,
                l.subdivision_2_name, l.city_name, l.metro_code, l.time_zone,
                l.is_in_european_union
             FROM city_blocks b
             LEFT JOIN city_locations l ON l.geoname_id = b.geoname_id AND l.locale_code = ?1
             WHERE b.ip_version = ?2 AND b.network_start <= ?3 AND b.network_end >= ?3
             ORDER BY b.prefix_length DESC LIMIT 1",
        )
        .ok()?;

    stmt.query_row(params![locale, ip_version, ip_key], |row| {
        Ok(CityRow {
            network: row.get(0)?,
            prefix_length: row.get(1)?,
            ip_version: row.get(2)?,
            geoname_id: row.get(3)?,
            registered_country_geoname_id: row.get(4)?,
            represented_country_geoname_id: row.get(5)?,
            is_anonymous_proxy: row.get(6)?,
            is_satellite_provider: row.get(7)?,
            is_anycast: row.get(8)?,
            postal_code: row.get(9)?,
            latitude: row.get(10)?,
            longitude: row.get(11)?,
            accuracy_radius: row.get(12)?,
            continent_code: row.get(13)?,
            continent_name: row.get(14)?,
            country_iso_code: row.get(15)?,
            country_name: row.get(16)?,
            subdivision_1_iso_code: row.get(17)?,
            subdivision_1_name: row.get(18)?,
            subdivision_2_iso_code: row.get(19)?,
            subdivision_2_name: row.get(20)?,
            city_name: row.get(21)?,
            metro_code: row.get(22)?,
            time_zone: row.get(23)?,
            is_in_european_union: row.get(24)?,
        })
    })
    .ok()
}

fn lookup_country(conn: &Connection, ip_version: i64, ip_key: i64) -> Option<CountryRow> {
    let locale = env::var("GEOIP_LOCALE").unwrap_or_else(|_| "en".to_string());
    let mut stmt = conn
        .prepare(
            "SELECT
                b.network, b.prefix_length, b.ip_version, b.geoname_id,
                b.registered_country_geoname_id, b.represented_country_geoname_id,
                b.is_anonymous_proxy, b.is_satellite_provider, b.is_anycast,
                l.continent_code, l.continent_name, l.country_iso_code, l.country_name,
                l.is_in_european_union
             FROM country_blocks b
             LEFT JOIN country_locations l ON l.geoname_id = b.geoname_id AND l.locale_code = ?1
             WHERE b.ip_version = ?2 AND b.network_start <= ?3 AND b.network_end >= ?3
             ORDER BY b.prefix_length DESC LIMIT 1",
        )
        .ok()?;

    stmt.query_row(params![locale, ip_version, ip_key], |row| {
        Ok(CountryRow {
            network: row.get(0)?,
            prefix_length: row.get(1)?,
            ip_version: row.get(2)?,
            geoname_id: row.get(3)?,
            registered_country_geoname_id: row.get(4)?,
            represented_country_geoname_id: row.get(5)?,
            is_anonymous_proxy: row.get(6)?,
            is_satellite_provider: row.get(7)?,
            is_anycast: row.get(8)?,
            continent_code: row.get(9)?,
            continent_name: row.get(10)?,
            country_iso_code: row.get(11)?,
            country_name: row.get(12)?,
            is_in_european_union: row.get(13)?,
        })
    })
    .ok()
}

#[derive(Serialize)]
struct ResponsePayload {
    status: i32,
    ip: String,
    ip_version: i64,
    location: Location,
    asn: Option<Asn>,
    message: String,
}

#[derive(Serialize)]
struct Location {
    source: String,
    network: Network,
    geo: Geo,
    coordinates: Coordinates,
    postal_code: Option<String>,
    traits: Traits,
    geoname_id: Option<i64>,
    registered_country_geoname_id: Option<i64>,
    represented_country_geoname_id: Option<i64>,
}

#[derive(Serialize)]
struct Network {
    cidr: String,
    prefix_length: i64,
    ip_version: i64,
}

#[derive(Serialize)]
struct Geo {
    continent: Continent,
    country: Country,
    subdivision_1: Subdivision,
    subdivision_2: Subdivision,
    city: City,
    time_zone: Option<String>,
}

#[derive(Serialize)]
struct Continent {
    code: Option<String>,
    name: Option<String>,
}

#[derive(Serialize)]
struct Country {
    iso_code: Option<String>,
    name: Option<String>,
    flag_emoji: Option<String>,
    is_in_european_union: Option<i64>,
}

#[derive(Serialize)]
struct Subdivision {
    iso_code: Option<String>,
    name: Option<String>,
}

#[derive(Serialize)]
struct City {
    name: Option<String>,
    metro_code: Option<String>,
}

#[derive(Serialize)]
struct Coordinates {
    latitude: Option<f64>,
    longitude: Option<f64>,
    accuracy_radius: Option<i64>,
}

#[derive(Serialize)]
struct Traits {
    is_anonymous_proxy: Option<i64>,
    is_satellite_provider: Option<i64>,
    is_anycast: Option<i64>,
}

#[derive(Serialize)]
struct Asn {
    network: Network,
    number: Option<i64>,
    organization: Option<String>,
}

fn format_location_city(row: CityRow, source: &str) -> Location {
    Location {
        source: source.to_string(),
        network: Network {
            cidr: row.network,
            prefix_length: row.prefix_length,
            ip_version: row.ip_version,
        },
        geo: Geo {
            continent: Continent {
                code: row.continent_code,
                name: row.continent_name,
            },
            country: Country {
                iso_code: row.country_iso_code.clone(),
                name: row.country_name.clone(),
                flag_emoji: iso_to_flag(row.country_iso_code.as_deref()),
                is_in_european_union: row.is_in_european_union,
            },
            subdivision_1: Subdivision {
                iso_code: row.subdivision_1_iso_code,
                name: row.subdivision_1_name,
            },
            subdivision_2: Subdivision {
                iso_code: row.subdivision_2_iso_code,
                name: row.subdivision_2_name,
            },
            city: City {
                name: row.city_name,
                metro_code: row.metro_code,
            },
            time_zone: row.time_zone,
        },
        coordinates: Coordinates {
            latitude: row.latitude,
            longitude: row.longitude,
            accuracy_radius: row.accuracy_radius,
        },
        postal_code: row.postal_code,
        traits: Traits {
            is_anonymous_proxy: row.is_anonymous_proxy,
            is_satellite_provider: row.is_satellite_provider,
            is_anycast: row.is_anycast,
        },
        geoname_id: row.geoname_id,
        registered_country_geoname_id: row.registered_country_geoname_id,
        represented_country_geoname_id: row.represented_country_geoname_id,
    }
}

fn format_location_country(row: CountryRow, source: &str) -> Location {
    Location {
        source: source.to_string(),
        network: Network {
            cidr: row.network,
            prefix_length: row.prefix_length,
            ip_version: row.ip_version,
        },
        geo: Geo {
            continent: Continent {
                code: row.continent_code,
                name: row.continent_name,
            },
            country: Country {
                iso_code: row.country_iso_code.clone(),
                name: row.country_name.clone(),
                flag_emoji: iso_to_flag(row.country_iso_code.as_deref()),
                is_in_european_union: row.is_in_european_union,
            },
            subdivision_1: Subdivision {
                iso_code: None,
                name: None,
            },
            subdivision_2: Subdivision {
                iso_code: None,
                name: None,
            },
            city: City {
                name: None,
                metro_code: None,
            },
            time_zone: None,
        },
        coordinates: Coordinates {
            latitude: None,
            longitude: None,
            accuracy_radius: None,
        },
        postal_code: None,
        traits: Traits {
            is_anonymous_proxy: row.is_anonymous_proxy,
            is_satellite_provider: row.is_satellite_provider,
            is_anycast: row.is_anycast,
        },
        geoname_id: row.geoname_id,
        registered_country_geoname_id: row.registered_country_geoname_id,
        represented_country_geoname_id: row.represented_country_geoname_id,
    }
}

fn format_asn(row: Option<AsnRow>) -> Option<Asn> {
    row.map(|asn| Asn {
        network: Network {
            cidr: asn.network,
            prefix_length: asn.prefix_length,
            ip_version: asn.ip_version,
        },
        number: asn.autonomous_system_number,
        organization: asn.autonomous_system_organization,
    })
}

fn iso_to_flag(iso: Option<&str>) -> Option<String> {
    let iso = iso?;
    if iso.len() != 2 {
        return None;
    }
    let upper = iso.to_uppercase();
    let bytes = upper.as_bytes();
    let base = 0x1F1E6;
    let first = base + (bytes[0] - b'A') as u32;
    let second = base + (bytes[1] - b'A') as u32;
    Some(format!(
        "{}{}",
        char::from_u32(first)?,
        char::from_u32(second)?
    ))
}

fn json_error(status: StatusCode, detail: &str) -> Response {
    (status, Json(serde_json::json!({ "status": status.as_u16(), "detail": detail })))
        .into_response()
}
