package main

import (
	"database/sql"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"runtime"
	"unicode"

	_ "github.com/mattn/go-sqlite3"
)

const messageText = "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates."

type responsePayload struct {
	Status    int         `json:"status"`
	IP        string      `json:"ip"`
	IPVersion int64       `json:"ip_version"`
	Location  interface{} `json:"location"`
	ASN       interface{} `json:"asn"`
	Message   string      `json:"message"`
}

type cityRow struct {
	Network                     string
	PrefixLength                int64
	IPVersion                   int64
	GeonameID                   sql.NullInt64
	RegisteredCountryGeonameID  sql.NullInt64
	RepresentedCountryGeonameID sql.NullInt64
	IsAnonymousProxy            sql.NullInt64
	IsSatelliteProvider         sql.NullInt64
	IsAnycast                   sql.NullInt64
	PostalCode                  sql.NullString
	Latitude                    sql.NullFloat64
	Longitude                   sql.NullFloat64
	AccuracyRadius              sql.NullInt64
	ContinentCode               sql.NullString
	ContinentName               sql.NullString
	CountryISOCode              sql.NullString
	CountryName                 sql.NullString
	Subdivision1ISOCode         sql.NullString
	Subdivision1Name            sql.NullString
	Subdivision2ISOCode         sql.NullString
	Subdivision2Name            sql.NullString
	CityName                    sql.NullString
	MetroCode                   sql.NullString
	TimeZone                    sql.NullString
	IsInEuropeanUnion           sql.NullInt64
}

type countryRow struct {
	Network                     string
	PrefixLength                int64
	IPVersion                   int64
	GeonameID                   sql.NullInt64
	RegisteredCountryGeonameID  sql.NullInt64
	RepresentedCountryGeonameID sql.NullInt64
	IsAnonymousProxy            sql.NullInt64
	IsSatelliteProvider         sql.NullInt64
	IsAnycast                   sql.NullInt64
	ContinentCode               sql.NullString
	ContinentName               sql.NullString
	CountryISOCode              sql.NullString
	CountryName                 sql.NullString
	IsInEuropeanUnion           sql.NullInt64
}

type asnRow struct {
	Network                string
	PrefixLength           int64
	IPVersion              int64
	AutonomousSystemNumber sql.NullInt64
	AutonomousSystemOrg    sql.NullString
}

func main() {
	dbPath := os.Getenv("GEOIP_DB_PATH")
	if dbPath == "" {
		dbPath = defaultDBPath()
	}
	locale := os.Getenv("GEOIP_LOCALE")
	if locale == "" {
		locale = "en"
	}
	port := os.Getenv("GEOIP_PORT")
	if port == "" {
		port = "5022"
	}

	db, err := sql.Open("sqlite3", dbPath)
	if err != nil {
		log.Fatalf("db open error: %v", err)
	}
	defer db.Close()

	mux := http.NewServeMux()
	mux.Handle("/lookup", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			writeError(w, http.StatusMethodNotAllowed, "Method not allowed")
			return
		}
		handleLookup(w, r, db, locale)
	}))

	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/lookup" {
			writeError(w, http.StatusNotFound, "Route not found")
			return
		}
		mux.ServeHTTP(w, r)
	})

	log.Printf("GeoIP API running on http://localhost:%s", port)
	log.Fatal(http.ListenAndServe(":"+port, handler))
}

func defaultDBPath() string {
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		return "../config/database/WhatTimeIsIn-geoip.db"
	}
	return filepath.Clean(filepath.Join(filepath.Dir(filename), "..", "config", "database", "WhatTimeIsIn-geoip.db"))
}

func handleLookup(w http.ResponseWriter, r *http.Request, db *sql.DB, locale string) {
	ipStr := r.URL.Query().Get("ip")
	if ipStr == "" {
		writeError(w, http.StatusBadRequest, "Missing ip parameter")
		return
	}
	addr, err := netip.ParseAddr(ipStr)
	if err != nil {
		writeError(w, http.StatusBadRequest, "Invalid IP address")
		return
	}

	ipVersion, ipKey, err := ipKeyFromAddr(addr)
	if err != nil {
		writeError(w, http.StatusBadRequest, "Invalid IP address")
		return
	}

	asn, err := lookupASN(db, ipVersion, ipKey)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "Internal server error")
		return
	}

	city, err := lookupCity(db, ipVersion, ipKey, locale)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "Internal server error")
		return
	}
	if city != nil {
		resp := responsePayload{
			Status:    200,
			IP:        ipStr,
			IPVersion: ipVersion,
			Location:  formatLocationFromCity(city, "city"),
			ASN:       formatASN(asn),
			Message:   messageText,
		}
		writeJSON(w, http.StatusOK, resp)
		return
	}

	country, err := lookupCountry(db, ipVersion, ipKey, locale)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "Internal server error")
		return
	}
	if country != nil {
		resp := responsePayload{
			Status:    200,
			IP:        ipStr,
			IPVersion: ipVersion,
			Location:  formatLocationFromCountry(country, "country"),
			ASN:       formatASN(asn),
			Message:   messageText,
		}
		writeJSON(w, http.StatusOK, resp)
		return
	}

	writeError(w, http.StatusNotFound, "IP not found in ranges")
}

func ipKeyFromAddr(addr netip.Addr) (int64, int64, error) {
	if addr.Is4() {
		ip4 := addr.As4()
		ipNum := binary.BigEndian.Uint32(ip4[:])
		return 4, int64(ipNum), nil
	}
	if addr.Is6() {
		ip6 := addr.As16()
		high := binary.BigEndian.Uint64(ip6[:8])
		if high > (^uint64(0)>>1) {
			return 6, 0, fmt.Errorf("ipv6 out of range")
		}
		return 6, int64(high), nil
	}
	return 0, 0, fmt.Errorf("invalid ip")
}

func lookupCity(db *sql.DB, ipVersion int64, ipKey int64, locale string) (*cityRow, error) {
	row := db.QueryRow(`
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
    `, locale, ipVersion, ipKey, ipKey)

	var result cityRow
	err := row.Scan(
		&result.Network,
		&result.PrefixLength,
		&result.IPVersion,
		&result.GeonameID,
		&result.RegisteredCountryGeonameID,
		&result.RepresentedCountryGeonameID,
		&result.IsAnonymousProxy,
		&result.IsSatelliteProvider,
		&result.IsAnycast,
		&result.PostalCode,
		&result.Latitude,
		&result.Longitude,
		&result.AccuracyRadius,
		&result.ContinentCode,
		&result.ContinentName,
		&result.CountryISOCode,
		&result.CountryName,
		&result.Subdivision1ISOCode,
		&result.Subdivision1Name,
		&result.Subdivision2ISOCode,
		&result.Subdivision2Name,
		&result.CityName,
		&result.MetroCode,
		&result.TimeZone,
		&result.IsInEuropeanUnion,
	)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &result, nil
}

func lookupCountry(db *sql.DB, ipVersion int64, ipKey int64, locale string) (*countryRow, error) {
	row := db.QueryRow(`
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
    `, locale, ipVersion, ipKey, ipKey)

	var result countryRow
	err := row.Scan(
		&result.Network,
		&result.PrefixLength,
		&result.IPVersion,
		&result.GeonameID,
		&result.RegisteredCountryGeonameID,
		&result.RepresentedCountryGeonameID,
		&result.IsAnonymousProxy,
		&result.IsSatelliteProvider,
		&result.IsAnycast,
		&result.ContinentCode,
		&result.ContinentName,
		&result.CountryISOCode,
		&result.CountryName,
		&result.IsInEuropeanUnion,
	)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &result, nil
}

func lookupASN(db *sql.DB, ipVersion int64, ipKey int64) (*asnRow, error) {
	row := db.QueryRow(`
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
    `, ipVersion, ipKey, ipKey)

	var result asnRow
	err := row.Scan(
		&result.Network,
		&result.PrefixLength,
		&result.IPVersion,
		&result.AutonomousSystemNumber,
		&result.AutonomousSystemOrg,
	)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &result, nil
}

func formatASN(row *asnRow) map[string]any {
	if row == nil {
		return nil
	}
	return map[string]any{
		"network": map[string]any{
			"cidr":          row.Network,
			"prefix_length": row.PrefixLength,
			"ip_version":    row.IPVersion,
		},
		"number":       nullInt64(row.AutonomousSystemNumber),
		"organization": nullString(row.AutonomousSystemOrg),
	}
}

func formatLocationFromCity(row *cityRow, source string) map[string]any {
	countryISOValue := nullStringValue(row.CountryISOCode)
	countryISO := nullString(row.CountryISOCode)
	return map[string]any{
		"source": source,
		"network": map[string]any{
			"cidr":          row.Network,
			"prefix_length": row.PrefixLength,
			"ip_version":    row.IPVersion,
		},
		"geo": map[string]any{
			"continent": map[string]any{
				"code": nullString(row.ContinentCode),
				"name": nullString(row.ContinentName),
			},
			"country": map[string]any{
				"iso_code":            countryISO,
				"name":                nullString(row.CountryName),
				"flag_emoji":          isoToFlag(countryISOValue),
				"is_in_european_union": nullInt64(row.IsInEuropeanUnion),
			},
			"subdivision_1": map[string]any{
				"iso_code": nullString(row.Subdivision1ISOCode),
				"name":     nullString(row.Subdivision1Name),
			},
			"subdivision_2": map[string]any{
				"iso_code": nullString(row.Subdivision2ISOCode),
				"name":     nullString(row.Subdivision2Name),
			},
			"city": map[string]any{
				"name":       nullString(row.CityName),
				"metro_code": nullString(row.MetroCode),
			},
			"time_zone": nullString(row.TimeZone),
		},
		"coordinates": map[string]any{
			"latitude":        nullFloat64(row.Latitude),
			"longitude":       nullFloat64(row.Longitude),
			"accuracy_radius": nullInt64(row.AccuracyRadius),
		},
		"postal_code": nullString(row.PostalCode),
		"traits": map[string]any{
			"is_anonymous_proxy":  nullInt64(row.IsAnonymousProxy),
			"is_satellite_provider": nullInt64(row.IsSatelliteProvider),
			"is_anycast":            nullInt64(row.IsAnycast),
		},
		"geoname_id":                     nullInt64(row.GeonameID),
		"registered_country_geoname_id":  nullInt64(row.RegisteredCountryGeonameID),
		"represented_country_geoname_id": nullInt64(row.RepresentedCountryGeonameID),
	}
}

func formatLocationFromCountry(row *countryRow, source string) map[string]any {
	countryISOValue := nullStringValue(row.CountryISOCode)
	countryISO := nullString(row.CountryISOCode)
	return map[string]any{
		"source": source,
		"network": map[string]any{
			"cidr":          row.Network,
			"prefix_length": row.PrefixLength,
			"ip_version":    row.IPVersion,
		},
		"geo": map[string]any{
			"continent": map[string]any{
				"code": nullString(row.ContinentCode),
				"name": nullString(row.ContinentName),
			},
			"country": map[string]any{
				"iso_code":            countryISO,
				"name":                nullString(row.CountryName),
				"flag_emoji":          isoToFlag(countryISOValue),
				"is_in_european_union": nullInt64(row.IsInEuropeanUnion),
			},
			"subdivision_1": map[string]any{
				"iso_code": nil,
				"name":     nil,
			},
			"subdivision_2": map[string]any{
				"iso_code": nil,
				"name":     nil,
			},
			"city": map[string]any{
				"name":       nil,
				"metro_code": nil,
			},
			"time_zone": nil,
		},
		"coordinates": map[string]any{
			"latitude":        nil,
			"longitude":       nil,
			"accuracy_radius": nil,
		},
		"postal_code": nil,
		"traits": map[string]any{
			"is_anonymous_proxy":  nullInt64(row.IsAnonymousProxy),
			"is_satellite_provider": nullInt64(row.IsSatelliteProvider),
			"is_anycast":            nullInt64(row.IsAnycast),
		},
		"geoname_id":                     nullInt64(row.GeonameID),
		"registered_country_geoname_id":  nullInt64(row.RegisteredCountryGeonameID),
		"represented_country_geoname_id": nullInt64(row.RepresentedCountryGeonameID),
	}
}

func isoToFlag(iso string) any {
	if len(iso) != 2 {
		return nil
	}
	r1 := rune(0x1F1E6) + rune(unicode.ToUpper(rune(iso[0]))-'A')
	r2 := rune(0x1F1E6) + rune(unicode.ToUpper(rune(iso[1]))-'A')
	return string([]rune{r1, r2})
}

func writeError(w http.ResponseWriter, status int, detail string) {
	writeJSON(w, status, map[string]any{
		"status": status,
		"detail": detail,
	})
}

func writeJSON(w http.ResponseWriter, status int, payload interface{}) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	encoder := json.NewEncoder(w)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(payload); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

func nullString(value sql.NullString) any {
	if !value.Valid {
		return nil
	}
	return value.String
}

func nullStringValue(value sql.NullString) string {
	if !value.Valid {
		return ""
	}
	return value.String
}

func nullInt64(value sql.NullInt64) any {
	if !value.Valid {
		return nil
	}
	return value.Int64
}

func nullFloat64(value sql.NullFloat64) any {
	if !value.Valid {
		return nil
	}
	return value.Float64
}
