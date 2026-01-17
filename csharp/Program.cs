using System.Net;
using System.Net.Sockets;
using Microsoft.Data.Sqlite;

const string MessageText =
    "If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.";

var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

var dbPath = Environment.GetEnvironmentVariable("GEOIP_DB_PATH");
if (string.IsNullOrWhiteSpace(dbPath))
{
    dbPath = Path.GetFullPath(
        Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "config", "database", "WhatTimeIsIn-geoip.db")
    );
}

var locale = Environment.GetEnvironmentVariable("GEOIP_LOCALE");
if (string.IsNullOrWhiteSpace(locale))
{
    locale = "en";
}

app.MapGet("/lookup", async (HttpContext context) =>
{
    var ip = context.Request.Query["ip"].ToString();
    if (string.IsNullOrWhiteSpace(ip))
    {
        return Results.Json(new { status = 400, detail = "Missing ip parameter" }, statusCode: 400);
    }

    if (!TryParseIp(ip, out var ipVersion, out var ipKey))
    {
        return Results.Json(new { status = 400, detail = "Invalid IP address" }, statusCode: 400);
    }

    if (!File.Exists(dbPath))
    {
        return Results.Json(new { status = 500, detail = "Database file not found" }, statusCode: 500);
    }

    await using var db = new SqliteConnection($"Data Source={dbPath}");
    await db.OpenAsync();

    var asn = await LookupAsn(db, ipVersion, ipKey);
    var city = await LookupCity(db, ipVersion, ipKey, locale);
    if (city is not null)
    {
        var response = new ResponsePayload(
            Status: 200,
            IP: ip,
            IPVersion: ipVersion,
            Location: FormatLocation(city, "city"),
            ASN: FormatAsn(asn),
            Message: MessageText
        );
        return Results.Json(response, statusCode: 200);
    }

    var country = await LookupCountry(db, ipVersion, ipKey, locale);
    if (country is not null)
    {
        var response = new ResponsePayload(
            Status: 200,
            IP: ip,
            IPVersion: ipVersion,
            Location: FormatLocation(country, "country"),
            ASN: FormatAsn(asn),
            Message: MessageText
        );
        return Results.Json(response, statusCode: 200);
    }

    return Results.Json(new { status = 404, detail = "IP not found in ranges" }, statusCode: 404);
});

app.MapFallback(() =>
    Results.Json(new { status = 404, detail = "Route not found" }, statusCode: 404));

app.Run();

static bool TryParseIp(string ip, out int ipVersion, out long ipKey)
{
    ipVersion = 0;
    ipKey = 0;

    if (IPAddress.TryParse(ip, out var address))
    {
        if (address.AddressFamily == AddressFamily.InterNetwork)
        {
            ipVersion = 4;
            var bytes = address.GetAddressBytes();
            var value = (uint)(bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3]);
            ipKey = value;
            return true;
        }

        if (address.AddressFamily == AddressFamily.InterNetworkV6)
        {
            ipVersion = 6;
            var bytes = address.GetAddressBytes();
            ulong high =
                ((ulong)bytes[0] << 56) |
                ((ulong)bytes[1] << 48) |
                ((ulong)bytes[2] << 40) |
                ((ulong)bytes[3] << 32) |
                ((ulong)bytes[4] << 24) |
                ((ulong)bytes[5] << 16) |
                ((ulong)bytes[6] << 8) |
                bytes[7];

            if (high > long.MaxValue)
            {
                return false;
            }

            ipKey = (long)high;
            return true;
        }
    }

    return false;
}

static async Task<Dictionary<string, object?>?> LookupCity(
    SqliteConnection db,
    int ipVersion,
    long ipKey,
    string locale
)
{
    var cmd = db.CreateCommand();
    cmd.CommandText = @"
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
            AND l.locale_code = $locale
        WHERE b.ip_version = $ipVersion
          AND b.network_start <= $ipKey
          AND b.network_end >= $ipKey
        ORDER BY b.prefix_length DESC
        LIMIT 1
    ";
    cmd.Parameters.AddWithValue("$locale", locale);
    cmd.Parameters.AddWithValue("$ipVersion", ipVersion);
    cmd.Parameters.AddWithValue("$ipKey", ipKey);

    await using var reader = await cmd.ExecuteReaderAsync();
    if (!await reader.ReadAsync())
    {
        return null;
    }

    return ReadRow(reader);
}

static async Task<Dictionary<string, object?>?> LookupCountry(
    SqliteConnection db,
    int ipVersion,
    long ipKey,
    string locale
)
{
    var cmd = db.CreateCommand();
    cmd.CommandText = @"
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
            AND l.locale_code = $locale
        WHERE b.ip_version = $ipVersion
          AND b.network_start <= $ipKey
          AND b.network_end >= $ipKey
        ORDER BY b.prefix_length DESC
        LIMIT 1
    ";
    cmd.Parameters.AddWithValue("$locale", locale);
    cmd.Parameters.AddWithValue("$ipVersion", ipVersion);
    cmd.Parameters.AddWithValue("$ipKey", ipKey);

    await using var reader = await cmd.ExecuteReaderAsync();
    if (!await reader.ReadAsync())
    {
        return null;
    }

    return ReadRow(reader);
}

static async Task<Dictionary<string, object?>?> LookupAsn(
    SqliteConnection db,
    int ipVersion,
    long ipKey
)
{
    var cmd = db.CreateCommand();
    cmd.CommandText = @"
        SELECT
            network,
            prefix_length,
            ip_version,
            autonomous_system_number,
            autonomous_system_organization
        FROM asn_blocks
        WHERE ip_version = $ipVersion
          AND network_start <= $ipKey
          AND network_end >= $ipKey
        ORDER BY prefix_length DESC
        LIMIT 1
    ";
    cmd.Parameters.AddWithValue("$ipVersion", ipVersion);
    cmd.Parameters.AddWithValue("$ipKey", ipKey);

    await using var reader = await cmd.ExecuteReaderAsync();
    if (!await reader.ReadAsync())
    {
        return null;
    }

    return ReadRow(reader);
}

static Dictionary<string, object?> ReadRow(SqliteDataReader reader)
{
    var result = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase);
    for (var i = 0; i < reader.FieldCount; i++)
    {
        var name = reader.GetName(i);
        result[name] = reader.IsDBNull(i) ? null : reader.GetValue(i);
    }
    return result;
}

static Location FormatLocation(Dictionary<string, object?> data, string source)
{
    var countryIso = AsString(data.GetValueOrDefault("country_iso_code"));
    return new Location(
        Source: source,
        Network: new Network(
            Cidr: AsString(data.GetValueOrDefault("network")),
            PrefixLength: AsLong(data.GetValueOrDefault("prefix_length")),
            IPVersion: AsLong(data.GetValueOrDefault("ip_version"))
        ),
        Geo: new Geo(
            Continent: new Continent(
                Code: AsString(data.GetValueOrDefault("continent_code")),
                Name: AsString(data.GetValueOrDefault("continent_name"))
            ),
            Country: new Country(
                IsoCode: countryIso,
                Name: AsString(data.GetValueOrDefault("country_name")),
                FlagEmoji: IsoToFlag(countryIso),
                IsInEuropeanUnion: AsLong(data.GetValueOrDefault("is_in_european_union"))
            ),
            Subdivision1: new Subdivision(
                IsoCode: AsString(data.GetValueOrDefault("subdivision_1_iso_code")),
                Name: AsString(data.GetValueOrDefault("subdivision_1_name"))
            ),
            Subdivision2: new Subdivision(
                IsoCode: AsString(data.GetValueOrDefault("subdivision_2_iso_code")),
                Name: AsString(data.GetValueOrDefault("subdivision_2_name"))
            ),
            City: new City(
                Name: AsString(data.GetValueOrDefault("city_name")),
                MetroCode: AsString(data.GetValueOrDefault("metro_code"))
            ),
            TimeZone: AsString(data.GetValueOrDefault("time_zone"))
        ),
        Coordinates: new Coordinates(
            Latitude: AsDouble(data.GetValueOrDefault("latitude")),
            Longitude: AsDouble(data.GetValueOrDefault("longitude")),
            AccuracyRadius: AsLong(data.GetValueOrDefault("accuracy_radius"))
        ),
        PostalCode: AsString(data.GetValueOrDefault("postal_code")),
        Traits: new Traits(
            IsAnonymousProxy: AsLong(data.GetValueOrDefault("is_anonymous_proxy")),
            IsSatelliteProvider: AsLong(data.GetValueOrDefault("is_satellite_provider")),
            IsAnycast: AsLong(data.GetValueOrDefault("is_anycast"))
        ),
        GeonameId: AsLong(data.GetValueOrDefault("geoname_id")),
        RegisteredCountryGeonameId: AsLong(data.GetValueOrDefault("registered_country_geoname_id")),
        RepresentedCountryGeonameId: AsLong(data.GetValueOrDefault("represented_country_geoname_id"))
    );
}

static Asn? FormatAsn(Dictionary<string, object?>? data)
{
    if (data is null)
    {
        return null;
    }

    return new Asn(
        Network: new Network(
            Cidr: AsString(data.GetValueOrDefault("network")),
            PrefixLength: AsLong(data.GetValueOrDefault("prefix_length")),
            IPVersion: AsLong(data.GetValueOrDefault("ip_version"))
        ),
        Number: AsLong(data.GetValueOrDefault("autonomous_system_number")),
        Organization: AsString(data.GetValueOrDefault("autonomous_system_organization"))
    );
}

static string? IsoToFlag(string? iso)
{
    if (string.IsNullOrWhiteSpace(iso) || iso.Length != 2)
    {
        return null;
    }

    iso = iso.ToUpperInvariant();
    var first = 0x1F1E6 + (iso[0] - 'A');
    var second = 0x1F1E6 + (iso[1] - 'A');
    return char.ConvertFromUtf32(first) + char.ConvertFromUtf32(second);
}

static string? AsString(object? value) => value?.ToString();

static long? AsLong(object? value)
{
    if (value is null) return null;
    if (value is long longValue) return longValue;
    if (value is int intValue) return intValue;
    if (long.TryParse(value.ToString(), out var parsed)) return parsed;
    return null;
}

static double? AsDouble(object? value)
{
    if (value is null) return null;
    if (value is double doubleValue) return doubleValue;
    if (double.TryParse(value.ToString(), out var parsed)) return parsed;
    return null;
}

record ResponsePayload(
    int Status,
    string IP,
    int IPVersion,
    Location Location,
    Asn? ASN,
    string Message
);

record Location(
    string Source,
    Network Network,
    Geo Geo,
    Coordinates Coordinates,
    string? PostalCode,
    Traits Traits,
    long? GeonameId,
    long? RegisteredCountryGeonameId,
    long? RepresentedCountryGeonameId
);

record Network(string? Cidr, long? PrefixLength, long? IPVersion);

record Geo(
    Continent Continent,
    Country Country,
    Subdivision Subdivision1,
    Subdivision Subdivision2,
    City City,
    string? TimeZone
);

record Continent(string? Code, string? Name);

record Country(string? IsoCode, string? Name, string? FlagEmoji, long? IsInEuropeanUnion);

record Subdivision(string? IsoCode, string? Name);

record City(string? Name, string? MetroCode);

record Coordinates(double? Latitude, double? Longitude, long? AccuracyRadius);

record Traits(long? IsAnonymousProxy, long? IsSatelliteProvider, long? IsAnycast);

record Asn(Network Network, long? Number, string? Organization);
