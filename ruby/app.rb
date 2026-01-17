require "sinatra"
require "sqlite3"
require "ipaddr"

MESSAGE_TEXT = "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.".freeze

DB_PATH = ENV["GEOIP_DB_PATH"] || File.expand_path("../config/database/WhatTimeIsIn-geoip.db", __dir__)
LOCALE = ENV["GEOIP_LOCALE"] || "en"
PORT = (ENV["GEOIP_PORT"] || "5022").to_i

set :port, PORT
set :bind, "0.0.0.0"

before do
  content_type :json
end

get "/lookup" do
  ip = params["ip"]
  return json_error(400, "Missing ip parameter") if ip.to_s.strip.empty?

  parsed = parse_ip(ip)
  return json_error(400, "Invalid IP address") unless parsed

  ip_version, ip_key = parsed

  unless File.exist?(DB_PATH)
    return json_error(500, "Database file not found")
  end

  db = SQLite3::Database.new(DB_PATH)
  db.results_as_hash = true

  asn = lookup_asn(db, ip_version, ip_key)
  city = lookup_city(db, ip_version, ip_key)
  if city
    return json_success(ip, ip_version, format_location(city, "city"), format_asn(asn))
  end

  country = lookup_country(db, ip_version, ip_key)
  if country
    return json_success(ip, ip_version, format_location(country, "country"), format_asn(asn))
  end

  json_error(404, "IP not found in ranges")
ensure
  db&.close
end

not_found do
  json_error(404, "Route not found")
end

error do
  json_error(500, "Internal server error")
end

def parse_ip(ip)
  ipaddr = IPAddr.new(ip)
  if ipaddr.ipv4?
    [4, ipaddr.to_i]
  elsif ipaddr.ipv6?
    high = ipaddr.to_i >> 64
    return nil if high > (2**63 - 1)

    [6, high]
  end
rescue IPAddr::InvalidAddressError
  nil
end

def lookup_asn(db, ip_version, ip_key)
  db.get_first_row(
    <<~SQL,
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
    SQL
    ip_version, ip_key, ip_key
  )
end

def lookup_city(db, ip_version, ip_key)
  db.get_first_row(
    <<~SQL,
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
    SQL
    LOCALE, ip_version, ip_key, ip_key
  )
end

def lookup_country(db, ip_version, ip_key)
  db.get_first_row(
    <<~SQL,
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
    SQL
    LOCALE, ip_version, ip_key, ip_key
  )
end

def format_location(data, source)
  country_iso = data["country_iso_code"]
  {
    source: source,
    network: {
      cidr: data["network"],
      prefix_length: to_i(data["prefix_length"]),
      ip_version: to_i(data["ip_version"])
    },
    geo: {
      continent: {
        code: data["continent_code"],
        name: data["continent_name"]
      },
      country: {
        iso_code: country_iso,
        name: data["country_name"],
        flag_emoji: iso_to_flag(country_iso),
        is_in_european_union: to_i(data["is_in_european_union"])
      },
      subdivision_1: {
        iso_code: data["subdivision_1_iso_code"],
        name: data["subdivision_1_name"]
      },
      subdivision_2: {
        iso_code: data["subdivision_2_iso_code"],
        name: data["subdivision_2_name"]
      },
      city: {
        name: data["city_name"],
        metro_code: data["metro_code"]
      },
      time_zone: data["time_zone"]
    },
    coordinates: {
      latitude: to_f(data["latitude"]),
      longitude: to_f(data["longitude"]),
      accuracy_radius: to_i(data["accuracy_radius"])
    },
    postal_code: data["postal_code"],
    traits: {
      is_anonymous_proxy: to_i(data["is_anonymous_proxy"]),
      is_satellite_provider: to_i(data["is_satellite_provider"]),
      is_anycast: to_i(data["is_anycast"])
    },
    geoname_id: to_i(data["geoname_id"]),
    registered_country_geoname_id: to_i(data["registered_country_geoname_id"]),
    represented_country_geoname_id: to_i(data["represented_country_geoname_id"])
  }
end

def format_asn(data)
  return nil unless data

  {
    network: {
      cidr: data["network"],
      prefix_length: to_i(data["prefix_length"]),
      ip_version: to_i(data["ip_version"])
    },
    number: to_i(data["autonomous_system_number"]),
    organization: data["autonomous_system_organization"]
  }
end

def iso_to_flag(iso)
  return nil unless iso && iso.length == 2

  base = 0x1F1E6
  chars = iso.upcase.bytes.map { |b| (base + b - "A".ord) }
  chars.pack("U*")
end

def json_success(ip, ip_version, location, asn)
  {
    status: 200,
    ip: ip,
    ip_version: ip_version,
    location: location,
    asn: asn,
    message: MESSAGE_TEXT
  }.to_json
end

def json_error(status, detail)
  status status
  { status: status, detail: detail }.to_json
end

def to_i(value)
  return nil if value.nil? || value == ""

  value.to_i
end

def to_f(value)
  return nil if value.nil? || value == ""

  value.to_f
end
