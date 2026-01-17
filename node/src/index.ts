import express from "express";
import ipaddr from "ipaddr.js";
import { open } from "sqlite";
import sqlite3 from "sqlite3";
import path from "path";
import { fileURLToPath } from "url";

const MESSAGE_TEXT =
  "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const DEFAULT_DB_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "config",
  "database",
  "WhatTimeIsIn-geoip.db"
);

const dbPath = process.env.GEOIP_DB_PATH || DEFAULT_DB_PATH;
const locale = process.env.GEOIP_LOCALE || "en";
const port = Number(process.env.GEOIP_PORT || 5022);

const app = express();

app.get("/lookup", async (req, res) => {
  const ip = String(req.query.ip || "");
  if (!ip) {
    return res.status(400).json({ status: 400, detail: "Missing ip parameter" });
  }

  let ipVersion: number;
  let ipKey: string;
  try {
    const parsed = ipaddr.parse(ip);
    ipVersion = parsed.kind() === "ipv6" ? 6 : 4;
    if (ipVersion === 4) {
      const bytes = parsed.toByteArray();
      const value =
        (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
      ipKey = String(value >>> 0);
    } else {
      const bytes = parsed.toByteArray();
      const high =
        (BigInt(bytes[0]) << 56n) |
        (BigInt(bytes[1]) << 48n) |
        (BigInt(bytes[2]) << 40n) |
        (BigInt(bytes[3]) << 32n) |
        (BigInt(bytes[4]) << 24n) |
        (BigInt(bytes[5]) << 16n) |
        (BigInt(bytes[6]) << 8n) |
        BigInt(bytes[7]);
      ipKey = high.toString();
    }
  } catch {
    return res.status(400).json({ status: 400, detail: "Invalid IP address" });
  }

  try {
    const db = await open({ filename: dbPath, driver: sqlite3.Database });

    const asn = await db.get(
      `
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
      `,
      [ipVersion, ipKey, ipKey]
    );

    const city = await db.get(
      `
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
      `,
      [locale, ipVersion, ipKey, ipKey]
    );

    if (city) {
      await db.close();
      return res.status(200).json({
        status: 200,
        ip,
        ip_version: ipVersion,
        location: formatLocation(city, "city"),
        asn: formatAsn(asn),
        message: MESSAGE_TEXT,
      });
    }

    const country = await db.get(
      `
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
      `,
      [locale, ipVersion, ipKey, ipKey]
    );

    await db.close();

    if (country) {
      return res.status(200).json({
        status: 200,
        ip,
        ip_version: ipVersion,
        location: formatLocation(country, "country"),
        asn: formatAsn(asn),
        message: MESSAGE_TEXT,
      });
    }

    return res
      .status(404)
      .json({ status: 404, detail: "IP not found in ranges" });
  } catch {
    return res
      .status(500)
      .json({ status: 500, detail: "Internal server error" });
  }
});

app.use((req, res) =>
  res.status(404).json({ status: 404, detail: "Route not found" })
);

app.listen(port, () => {
  console.log(`GeoIP API running on http://localhost:${port}`);
});

function formatLocation(data: Record<string, any>, source: string) {
  const countryIso = data.country_iso_code ?? null;
  return {
    source,
    network: {
      cidr: data.network ?? null,
      prefix_length: toInt(data.prefix_length),
      ip_version: toInt(data.ip_version),
    },
    geo: {
      continent: {
        code: data.continent_code ?? null,
        name: data.continent_name ?? null,
      },
      country: {
        iso_code: countryIso,
        name: data.country_name ?? null,
        flag_emoji: isoToFlag(countryIso),
        is_in_european_union: toInt(data.is_in_european_union),
      },
      subdivision_1: {
        iso_code: data.subdivision_1_iso_code ?? null,
        name: data.subdivision_1_name ?? null,
      },
      subdivision_2: {
        iso_code: data.subdivision_2_iso_code ?? null,
        name: data.subdivision_2_name ?? null,
      },
      city: {
        name: data.city_name ?? null,
        metro_code: data.metro_code ?? null,
      },
      time_zone: data.time_zone ?? null,
    },
    coordinates: {
      latitude: toFloat(data.latitude),
      longitude: toFloat(data.longitude),
      accuracy_radius: toInt(data.accuracy_radius),
    },
    postal_code: data.postal_code ?? null,
    traits: {
      is_anonymous_proxy: toInt(data.is_anonymous_proxy),
      is_satellite_provider: toInt(data.is_satellite_provider),
      is_anycast: toInt(data.is_anycast),
    },
    geoname_id: toInt(data.geoname_id),
    registered_country_geoname_id: toInt(data.registered_country_geoname_id),
    represented_country_geoname_id: toInt(data.represented_country_geoname_id),
  };
}

function formatAsn(data: Record<string, any> | undefined) {
  if (!data) return null;
  return {
    network: {
      cidr: data.network ?? null,
      prefix_length: toInt(data.prefix_length),
      ip_version: toInt(data.ip_version),
    },
    number: toInt(data.autonomous_system_number),
    organization: data.autonomous_system_organization ?? null,
  };
}

function isoToFlag(iso: string | null) {
  if (!iso || iso.length !== 2) return null;
  const base = 0x1f1e6;
  const first = base + (iso.toUpperCase().charCodeAt(0) - 65);
  const second = base + (iso.toUpperCase().charCodeAt(1) - 65);
  return String.fromCodePoint(first, second);
}

function toInt(value: any) {
  if (value === null || value === undefined || value === "") return null;
  return Number(value);
}

function toFloat(value: any) {
  if (value === null || value === undefined || value === "") return null;
  return Number(value);
}
