#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sqlite3.h>
#include <sstream>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr const char *kMessage =
    "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.";

struct CityRow {
  std::string network;
  int64_t prefix_length;
  int64_t ip_version;
  std::optional<int64_t> geoname_id;
  std::optional<int64_t> registered_country_geoname_id;
  std::optional<int64_t> represented_country_geoname_id;
  std::optional<int64_t> is_anonymous_proxy;
  std::optional<int64_t> is_satellite_provider;
  std::optional<int64_t> is_anycast;
  std::optional<std::string> postal_code;
  std::optional<double> latitude;
  std::optional<double> longitude;
  std::optional<int64_t> accuracy_radius;
  std::optional<std::string> continent_code;
  std::optional<std::string> continent_name;
  std::optional<std::string> country_iso_code;
  std::optional<std::string> country_name;
  std::optional<std::string> subdivision_1_iso_code;
  std::optional<std::string> subdivision_1_name;
  std::optional<std::string> subdivision_2_iso_code;
  std::optional<std::string> subdivision_2_name;
  std::optional<std::string> city_name;
  std::optional<std::string> metro_code;
  std::optional<std::string> time_zone;
  std::optional<int64_t> is_in_european_union;
};

struct CountryRow {
  std::string network;
  int64_t prefix_length;
  int64_t ip_version;
  std::optional<int64_t> geoname_id;
  std::optional<int64_t> registered_country_geoname_id;
  std::optional<int64_t> represented_country_geoname_id;
  std::optional<int64_t> is_anonymous_proxy;
  std::optional<int64_t> is_satellite_provider;
  std::optional<int64_t> is_anycast;
  std::optional<std::string> continent_code;
  std::optional<std::string> continent_name;
  std::optional<std::string> country_iso_code;
  std::optional<std::string> country_name;
  std::optional<int64_t> is_in_european_union;
};

struct AsnRow {
  std::string network;
  int64_t prefix_length;
  int64_t ip_version;
  std::optional<int64_t> autonomous_system_number;
  std::optional<std::string> autonomous_system_organization;
};

std::string default_db_path() {
  auto base = std::filesystem::path(__FILE__).parent_path().parent_path();
  return (base / "config" / "database" / "WhatTimeIsIn-geoip.db").string();
}

std::optional<int64_t> column_int64(sqlite3_stmt *stmt, int idx) {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_int64(stmt, idx);
}

std::optional<double> column_double(sqlite3_stmt *stmt, int idx) {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_double(stmt, idx);
}

std::optional<std::string> column_text(sqlite3_stmt *stmt, int idx) {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  const unsigned char *text = sqlite3_column_text(stmt, idx);
  return std::string(reinterpret_cast<const char *>(text));
}

std::string json_escape(const std::string &input) {
  std::ostringstream out;
  for (unsigned char c : input) {
    switch (c) {
    case '\"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (c < 0x20) {
        out << "\\u" << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << static_cast<int>(c) << std::dec
            << std::nouppercase;
      } else {
        out << c;
      }
    }
  }
  return out.str();
}

std::string json_string(const std::optional<std::string> &value) {
  if (!value.has_value()) {
    return "null";
  }
  return "\"" + json_escape(*value) + "\"";
}

std::string json_number(const std::optional<int64_t> &value) {
  if (!value.has_value()) {
    return "null";
  }
  return std::to_string(*value);
}

std::string json_number(const std::optional<double> &value) {
  if (!value.has_value()) {
    return "null";
  }
  std::ostringstream out;
  out << *value;
  return out.str();
}

std::string utf8_from_codepoint(int codepoint) {
  std::string out;
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

std::optional<std::string> iso_to_flag(const std::optional<std::string> &iso) {
  if (!iso.has_value() || iso->size() != 2) {
    return std::nullopt;
  }
  std::string upper = *iso;
  for (auto &ch : upper) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  int code1 = 0x1F1E6 + (upper[0] - 'A');
  int code2 = 0x1F1E6 + (upper[1] - 'A');
  return utf8_from_codepoint(code1) + utf8_from_codepoint(code2);
}

std::string format_location(const CityRow &row, const std::string &source) {
  std::ostringstream out;
  auto flag = iso_to_flag(row.country_iso_code);
  out << "{"
      << "\"source\":\"" << json_escape(source) << "\","
      << "\"network\":{"
      << "\"cidr\":\"" << json_escape(row.network) << "\","
      << "\"prefix_length\":" << row.prefix_length << ","
      << "\"ip_version\":" << row.ip_version
      << "},"
      << "\"geo\":{"
      << "\"continent\":{"
      << "\"code\":" << json_string(row.continent_code) << ","
      << "\"name\":" << json_string(row.continent_name)
      << "},"
      << "\"country\":{"
      << "\"iso_code\":" << json_string(row.country_iso_code) << ","
      << "\"name\":" << json_string(row.country_name) << ","
      << "\"flag_emoji\":" << json_string(flag) << ","
      << "\"is_in_european_union\":" << json_number(row.is_in_european_union)
      << "},"
      << "\"subdivision_1\":{"
      << "\"iso_code\":" << json_string(row.subdivision_1_iso_code) << ","
      << "\"name\":" << json_string(row.subdivision_1_name)
      << "},"
      << "\"subdivision_2\":{"
      << "\"iso_code\":" << json_string(row.subdivision_2_iso_code) << ","
      << "\"name\":" << json_string(row.subdivision_2_name)
      << "},"
      << "\"city\":{"
      << "\"name\":" << json_string(row.city_name) << ","
      << "\"metro_code\":" << json_string(row.metro_code)
      << "},"
      << "\"time_zone\":" << json_string(row.time_zone)
      << "},"
      << "\"coordinates\":{"
      << "\"latitude\":" << json_number(row.latitude) << ","
      << "\"longitude\":" << json_number(row.longitude) << ","
      << "\"accuracy_radius\":" << json_number(row.accuracy_radius)
      << "},"
      << "\"postal_code\":" << json_string(row.postal_code) << ","
      << "\"traits\":{"
      << "\"is_anonymous_proxy\":" << json_number(row.is_anonymous_proxy) << ","
      << "\"is_satellite_provider\":" << json_number(row.is_satellite_provider) << ","
      << "\"is_anycast\":" << json_number(row.is_anycast)
      << "},"
      << "\"geoname_id\":" << json_number(row.geoname_id) << ","
      << "\"registered_country_geoname_id\":"
      << json_number(row.registered_country_geoname_id) << ","
      << "\"represented_country_geoname_id\":"
      << json_number(row.represented_country_geoname_id)
      << "}";
  return out.str();
}

std::string format_location(const CountryRow &row, const std::string &source) {
  std::ostringstream out;
  auto flag = iso_to_flag(row.country_iso_code);
  out << "{"
      << "\"source\":\"" << json_escape(source) << "\","
      << "\"network\":{"
      << "\"cidr\":\"" << json_escape(row.network) << "\","
      << "\"prefix_length\":" << row.prefix_length << ","
      << "\"ip_version\":" << row.ip_version
      << "},"
      << "\"geo\":{"
      << "\"continent\":{"
      << "\"code\":" << json_string(row.continent_code) << ","
      << "\"name\":" << json_string(row.continent_name)
      << "},"
      << "\"country\":{"
      << "\"iso_code\":" << json_string(row.country_iso_code) << ","
      << "\"name\":" << json_string(row.country_name) << ","
      << "\"flag_emoji\":" << json_string(flag) << ","
      << "\"is_in_european_union\":" << json_number(row.is_in_european_union)
      << "},"
      << "\"subdivision_1\":{"
      << "\"iso_code\":null,"
      << "\"name\":null"
      << "},"
      << "\"subdivision_2\":{"
      << "\"iso_code\":null,"
      << "\"name\":null"
      << "},"
      << "\"city\":{"
      << "\"name\":null,"
      << "\"metro_code\":null"
      << "},"
      << "\"time_zone\":null"
      << "},"
      << "\"coordinates\":{"
      << "\"latitude\":null,"
      << "\"longitude\":null,"
      << "\"accuracy_radius\":null"
      << "},"
      << "\"postal_code\":null,"
      << "\"traits\":{"
      << "\"is_anonymous_proxy\":" << json_number(row.is_anonymous_proxy) << ","
      << "\"is_satellite_provider\":" << json_number(row.is_satellite_provider) << ","
      << "\"is_anycast\":" << json_number(row.is_anycast)
      << "},"
      << "\"geoname_id\":" << json_number(row.geoname_id) << ","
      << "\"registered_country_geoname_id\":"
      << json_number(row.registered_country_geoname_id) << ","
      << "\"represented_country_geoname_id\":"
      << json_number(row.represented_country_geoname_id)
      << "}";
  return out.str();
}

std::string format_asn(const std::optional<AsnRow> &row) {
  if (!row.has_value()) {
    return "null";
  }
  std::ostringstream out;
  out << "{"
      << "\"network\":{"
      << "\"cidr\":\"" << json_escape(row->network) << "\","
      << "\"prefix_length\":" << row->prefix_length << ","
      << "\"ip_version\":" << row->ip_version
      << "},"
      << "\"number\":" << json_number(row->autonomous_system_number) << ","
      << "\"organization\":"
      << json_string(row->autonomous_system_organization)
      << "}";
  return out.str();
}

std::optional<AsnRow> lookup_asn(sqlite3 *db, int64_t ip_version, int64_t ip_key) {
  const char *sql =
      "SELECT network, prefix_length, ip_version, "
      "autonomous_system_number, autonomous_system_organization "
      "FROM asn_blocks "
      "WHERE ip_version = ? AND network_start <= ? AND network_end >= ? "
      "ORDER BY prefix_length DESC LIMIT 1";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_int64(stmt, 1, ip_version);
  sqlite3_bind_int64(stmt, 2, ip_key);
  sqlite3_bind_int64(stmt, 3, ip_key);
  AsnRow row;
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    row.network = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    row.prefix_length = sqlite3_column_int64(stmt, 1);
    row.ip_version = sqlite3_column_int64(stmt, 2);
    row.autonomous_system_number = column_int64(stmt, 3);
    row.autonomous_system_organization = column_text(stmt, 4);
    sqlite3_finalize(stmt);
    return row;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

std::optional<CityRow> lookup_city(sqlite3 *db, int64_t ip_version,
                                   int64_t ip_key,
                                   const std::string &locale) {
  const char *sql =
      "SELECT b.network, b.prefix_length, b.ip_version, b.geoname_id, "
      "b.registered_country_geoname_id, b.represented_country_geoname_id, "
      "b.is_anonymous_proxy, b.is_satellite_provider, b.is_anycast, "
      "b.postal_code, b.latitude, b.longitude, b.accuracy_radius, "
      "l.continent_code, l.continent_name, l.country_iso_code, l.country_name, "
      "l.subdivision_1_iso_code, l.subdivision_1_name, "
      "l.subdivision_2_iso_code, l.subdivision_2_name, l.city_name, "
      "l.metro_code, l.time_zone, l.is_in_european_union "
      "FROM city_blocks b "
      "LEFT JOIN city_locations l ON l.geoname_id = b.geoname_id "
      "AND l.locale_code = ? "
      "WHERE b.ip_version = ? AND b.network_start <= ? AND b.network_end >= ? "
      "ORDER BY b.prefix_length DESC LIMIT 1";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, locale.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, ip_version);
  sqlite3_bind_int64(stmt, 3, ip_key);
  sqlite3_bind_int64(stmt, 4, ip_key);

  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  CityRow row;
  row.network = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  row.prefix_length = sqlite3_column_int64(stmt, 1);
  row.ip_version = sqlite3_column_int64(stmt, 2);
  row.geoname_id = column_int64(stmt, 3);
  row.registered_country_geoname_id = column_int64(stmt, 4);
  row.represented_country_geoname_id = column_int64(stmt, 5);
  row.is_anonymous_proxy = column_int64(stmt, 6);
  row.is_satellite_provider = column_int64(stmt, 7);
  row.is_anycast = column_int64(stmt, 8);
  row.postal_code = column_text(stmt, 9);
  row.latitude = column_double(stmt, 10);
  row.longitude = column_double(stmt, 11);
  row.accuracy_radius = column_int64(stmt, 12);
  row.continent_code = column_text(stmt, 13);
  row.continent_name = column_text(stmt, 14);
  row.country_iso_code = column_text(stmt, 15);
  row.country_name = column_text(stmt, 16);
  row.subdivision_1_iso_code = column_text(stmt, 17);
  row.subdivision_1_name = column_text(stmt, 18);
  row.subdivision_2_iso_code = column_text(stmt, 19);
  row.subdivision_2_name = column_text(stmt, 20);
  row.city_name = column_text(stmt, 21);
  row.metro_code = column_text(stmt, 22);
  row.time_zone = column_text(stmt, 23);
  row.is_in_european_union = column_int64(stmt, 24);

  sqlite3_finalize(stmt);
  return row;
}

std::optional<CountryRow> lookup_country(sqlite3 *db, int64_t ip_version,
                                         int64_t ip_key,
                                         const std::string &locale) {
  const char *sql =
      "SELECT b.network, b.prefix_length, b.ip_version, b.geoname_id, "
      "b.registered_country_geoname_id, b.represented_country_geoname_id, "
      "b.is_anonymous_proxy, b.is_satellite_provider, b.is_anycast, "
      "l.continent_code, l.continent_name, l.country_iso_code, l.country_name, "
      "l.is_in_european_union "
      "FROM country_blocks b "
      "LEFT JOIN country_locations l ON l.geoname_id = b.geoname_id "
      "AND l.locale_code = ? "
      "WHERE b.ip_version = ? AND b.network_start <= ? AND b.network_end >= ? "
      "ORDER BY b.prefix_length DESC LIMIT 1";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, locale.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, ip_version);
  sqlite3_bind_int64(stmt, 3, ip_key);
  sqlite3_bind_int64(stmt, 4, ip_key);

  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  CountryRow row;
  row.network = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  row.prefix_length = sqlite3_column_int64(stmt, 1);
  row.ip_version = sqlite3_column_int64(stmt, 2);
  row.geoname_id = column_int64(stmt, 3);
  row.registered_country_geoname_id = column_int64(stmt, 4);
  row.represented_country_geoname_id = column_int64(stmt, 5);
  row.is_anonymous_proxy = column_int64(stmt, 6);
  row.is_satellite_provider = column_int64(stmt, 7);
  row.is_anycast = column_int64(stmt, 8);
  row.continent_code = column_text(stmt, 9);
  row.continent_name = column_text(stmt, 10);
  row.country_iso_code = column_text(stmt, 11);
  row.country_name = column_text(stmt, 12);
  row.is_in_european_union = column_int64(stmt, 13);

  sqlite3_finalize(stmt);
  return row;
}

bool parse_ip(const std::string &ip, int64_t &version, int64_t &key) {
  in_addr ipv4_addr{};
  if (inet_pton(AF_INET, ip.c_str(), &ipv4_addr) == 1) {
    version = 4;
    uint32_t value = ntohl(ipv4_addr.s_addr);
    key = static_cast<int64_t>(value);
    return true;
  }

  in6_addr ipv6_addr{};
  if (inet_pton(AF_INET6, ip.c_str(), &ipv6_addr) == 1) {
    version = 6;
    const uint8_t *bytes = ipv6_addr.s6_addr;
    uint64_t high = 0;
    for (int i = 0; i < 8; ++i) {
      high = (high << 8) | bytes[i];
    }
    if (high > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    key = static_cast<int64_t>(high);
    return true;
  }
  return false;
}

} // namespace

int main() {
  std::string db_path = std::getenv("GEOIP_DB_PATH")
                            ? std::getenv("GEOIP_DB_PATH")
                            : default_db_path();
  int port = 5022;
  if (const char *port_env = std::getenv("GEOIP_PORT")) {
    port = std::atoi(port_env);
  }

  auto send_response = [](int client_fd, int status,
                          const std::string &body) {
    std::string reason = "OK";
    if (status == 400)
      reason = "Bad Request";
    else if (status == 404)
      reason = "Not Found";
    else if (status == 405)
      reason = "Method Not Allowed";
    else if (status == 500)
      reason = "Internal Server Error";

    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    std::string response = out.str();
    send(client_fd, response.c_str(), response.size(), 0);
  };

  std::cout << "GeoIP API running on http://localhost:" << port << std::endl;
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create socket." << std::endl;
    return 1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket." << std::endl;
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 10) < 0) {
    std::cerr << "Failed to listen on socket." << std::endl;
    close(server_fd);
    return 1;
  }

  while (true) {
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      continue;
    }
    char buffer[8192];
    ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      close(client_fd);
      continue;
    }
    buffer[received] = '\0';
    std::string request(buffer);
    auto line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
      send_response(client_fd, 400,
                    "{\"status\":400,\"detail\":\"Invalid request\"}");
      close(client_fd);
      continue;
    }
    std::string request_line = request.substr(0, line_end);
    std::istringstream line_stream(request_line);
    std::string method;
    std::string target;
    line_stream >> method >> target;

    std::string path = target;
    std::string query;
    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
      path = target.substr(0, qpos);
      query = target.substr(qpos + 1);
    }

    if (path != "/lookup") {
      send_response(client_fd, 404,
                    "{\"status\":404,\"detail\":\"Route not found\"}");
      close(client_fd);
      continue;
    }

    if (method != "GET") {
      send_response(client_fd, 405,
                    "{\"status\":405,\"detail\":\"Method not allowed\"}");
      close(client_fd);
      continue;
    }

    std::string ip;
    std::istringstream qstream(query);
    std::string pair;
    while (std::getline(qstream, pair, '&')) {
      auto pos = pair.find('=');
      if (pos == std::string::npos) {
        continue;
      }
      auto key = pair.substr(0, pos);
      auto value = pair.substr(pos + 1);
      if (key == "ip") {
        ip = value;
        break;
      }
    }
    if (ip.empty()) {
      send_response(client_fd, 400,
                    "{\"status\":400,\"detail\":\"Missing ip parameter\"}");
      close(client_fd);
      continue;
    }

    int64_t ip_version = 0;
    int64_t ip_key = 0;
    if (!parse_ip(ip, ip_version, ip_key)) {
      send_response(client_fd, 400,
                    "{\"status\":400,\"detail\":\"Invalid IP address\"}");
      close(client_fd);
      continue;
    }

    if (!std::filesystem::exists(db_path)) {
      send_response(client_fd, 500,
                    "{\"status\":500,\"detail\":\"Database file not found\"}");
      close(client_fd);
      continue;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
      if (db) {
        sqlite3_close(db);
      }
      send_response(client_fd, 500,
                    "{\"status\":500,\"detail\":\"Database open failed\"}");
      close(client_fd);
      continue;
    }

    std::string locale = std::getenv("GEOIP_LOCALE") ? std::getenv("GEOIP_LOCALE") : "en";
    auto asn = lookup_asn(db, ip_version, ip_key);
    auto city = lookup_city(db, ip_version, ip_key, locale);
    if (city.has_value()) {
      std::ostringstream out;
      out << "{"
          << "\"status\":200,"
          << "\"ip\":\"" << json_escape(ip) << "\","
          << "\"ip_version\":" << ip_version << ","
          << "\"location\":" << format_location(*city, "city") << ","
          << "\"asn\":" << format_asn(asn) << ","
          << "\"message\":\"" << json_escape(kMessage) << "\""
          << "}";
      send_response(client_fd, 200, out.str());
      sqlite3_close(db);
      close(client_fd);
      continue;
    }

    auto country = lookup_country(db, ip_version, ip_key, locale);
    if (country.has_value()) {
      std::ostringstream out;
      out << "{"
          << "\"status\":200,"
          << "\"ip\":\"" << json_escape(ip) << "\","
          << "\"ip_version\":" << ip_version << ","
          << "\"location\":" << format_location(*country, "country") << ","
          << "\"asn\":" << format_asn(asn) << ","
          << "\"message\":\"" << json_escape(kMessage) << "\""
          << "}";
      send_response(client_fd, 200, out.str());
      sqlite3_close(db);
      close(client_fd);
      continue;
    }

    sqlite3_close(db);
    send_response(client_fd, 404,
                  "{\"status\":404,\"detail\":\"IP not found in ranges\"}");
    close(client_fd);
  }
}
