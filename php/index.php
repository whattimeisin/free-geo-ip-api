<?php

declare(strict_types=1);

const MESSAGE_TEXT = 'If you are using this solution, please reference the main project at https://whattimeis.in. This helps us keep the project actively maintained with new blocks and updates.';

$dbPath = getenv('GEOIP_DB_PATH');
if ($dbPath === false || $dbPath === '') {
    $dbPath = realpath(__DIR__ . '/../config/database/WhatTimeIsIn-geoip.db');
}
$locale = getenv('GEOIP_LOCALE');
if ($locale === false || $locale === '') {
    $locale = 'en';
}

$path = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH);
if ($path !== '/lookup') {
    respondError(404, 'Route not found');
    return;
}

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'GET') {
    respondError(405, 'Method not allowed');
    return;
}

$ip = $_GET['ip'] ?? '';
if ($ip === '') {
    respondError(400, 'Missing ip parameter');
    return;
}

$ipInfo = parseIp($ip);
if ($ipInfo === null) {
    respondError(400, 'Invalid IP address');
    return;
}

[$ipVersion, $ipKey] = $ipInfo;

if ($dbPath === false || !file_exists($dbPath)) {
    respondError(500, 'Database file not found');
    return;
}

try {
    $db = new PDO('sqlite:' . $dbPath, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    ]);
} catch (Throwable $e) {
    respondError(500, 'Internal server error');
    return;
}

$asn = lookupAsn($db, $ipVersion, $ipKey);
$city = lookupCity($db, $ipVersion, $ipKey, $locale);
if ($city !== null) {
    respondJson(200, [
        'status' => 200,
        'ip' => $ip,
        'ip_version' => $ipVersion,
        'location' => formatLocation($city, 'city'),
        'asn' => formatAsn($asn),
        'message' => MESSAGE_TEXT,
    ]);
    return;
}

$country = lookupCountry($db, $ipVersion, $ipKey, $locale);
if ($country !== null) {
    respondJson(200, [
        'status' => 200,
        'ip' => $ip,
        'ip_version' => $ipVersion,
        'location' => formatLocation($country, 'country'),
        'asn' => formatAsn($asn),
        'message' => MESSAGE_TEXT,
    ]);
    return;
}

respondError(404, 'IP not found in ranges');

function parseIp(string $ip): ?array
{
    if (filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
        $long = ip2long($ip);
        if ($long === false) {
            return null;
        }
        $unsigned = sprintf('%u', $long);
        return [4, $unsigned];
    }

    if (filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6)) {
        $packed = inet_pton($ip);
        if ($packed === false) {
            return null;
        }
        $highBytes = substr($packed, 0, 8);
        $parts = unpack('N2', $highBytes);
        if ($parts === false) {
            return null;
        }
        $high = (string) ((int) $parts[1] * 4294967296 + (int) $parts[2]);
        return [6, $high];
    }

    return null;
}

function lookupCity(PDO $db, int $ipVersion, string $ipKey, string $locale): ?array
{
    $stmt = $db->prepare(
        'SELECT
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
            AND l.locale_code = :locale
        WHERE b.ip_version = :ip_version
          AND b.network_start <= :ip_key
          AND b.network_end >= :ip_key
        ORDER BY b.prefix_length DESC
        LIMIT 1'
    );
    $stmt->execute([
        ':locale' => $locale,
        ':ip_version' => $ipVersion,
        ':ip_key' => $ipKey,
    ]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row ?: null;
}

function lookupCountry(PDO $db, int $ipVersion, string $ipKey, string $locale): ?array
{
    $stmt = $db->prepare(
        'SELECT
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
            AND l.locale_code = :locale
        WHERE b.ip_version = :ip_version
          AND b.network_start <= :ip_key
          AND b.network_end >= :ip_key
        ORDER BY b.prefix_length DESC
        LIMIT 1'
    );
    $stmt->execute([
        ':locale' => $locale,
        ':ip_version' => $ipVersion,
        ':ip_key' => $ipKey,
    ]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row ?: null;
}

function lookupAsn(PDO $db, int $ipVersion, string $ipKey): ?array
{
    $stmt = $db->prepare(
        'SELECT
            network,
            prefix_length,
            ip_version,
            autonomous_system_number,
            autonomous_system_organization
        FROM asn_blocks
        WHERE ip_version = :ip_version
          AND network_start <= :ip_key
          AND network_end >= :ip_key
        ORDER BY prefix_length DESC
        LIMIT 1'
    );
    $stmt->execute([
        ':ip_version' => $ipVersion,
        ':ip_key' => $ipKey,
    ]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row ?: null;
}

function formatLocation(array $data, string $source): array
{
    $countryIso = $data['country_iso_code'] ?? null;
    return [
        'source' => $source,
        'network' => [
            'cidr' => $data['network'] ?? null,
            'prefix_length' => toInt($data['prefix_length'] ?? null),
            'ip_version' => toInt($data['ip_version'] ?? null),
        ],
        'geo' => [
            'continent' => [
                'code' => $data['continent_code'] ?? null,
                'name' => $data['continent_name'] ?? null,
            ],
            'country' => [
                'iso_code' => $countryIso,
                'name' => $data['country_name'] ?? null,
                'flag_emoji' => isoToFlag($countryIso),
                'is_in_european_union' => toInt($data['is_in_european_union'] ?? null),
            ],
            'subdivision_1' => [
                'iso_code' => $data['subdivision_1_iso_code'] ?? null,
                'name' => $data['subdivision_1_name'] ?? null,
            ],
            'subdivision_2' => [
                'iso_code' => $data['subdivision_2_iso_code'] ?? null,
                'name' => $data['subdivision_2_name'] ?? null,
            ],
            'city' => [
                'name' => $data['city_name'] ?? null,
                'metro_code' => $data['metro_code'] ?? null,
            ],
            'time_zone' => $data['time_zone'] ?? null,
        ],
        'coordinates' => [
            'latitude' => toFloat($data['latitude'] ?? null),
            'longitude' => toFloat($data['longitude'] ?? null),
            'accuracy_radius' => toInt($data['accuracy_radius'] ?? null),
        ],
        'postal_code' => $data['postal_code'] ?? null,
        'traits' => [
            'is_anonymous_proxy' => toInt($data['is_anonymous_proxy'] ?? null),
            'is_satellite_provider' => toInt($data['is_satellite_provider'] ?? null),
            'is_anycast' => toInt($data['is_anycast'] ?? null),
        ],
        'geoname_id' => toInt($data['geoname_id'] ?? null),
        'registered_country_geoname_id' => toInt($data['registered_country_geoname_id'] ?? null),
        'represented_country_geoname_id' => toInt($data['represented_country_geoname_id'] ?? null),
    ];
}

function formatAsn(?array $data): ?array
{
    if ($data === null) {
        return null;
    }
    return [
        'network' => [
            'cidr' => $data['network'] ?? null,
            'prefix_length' => toInt($data['prefix_length'] ?? null),
            'ip_version' => toInt($data['ip_version'] ?? null),
        ],
        'number' => toInt($data['autonomous_system_number'] ?? null),
        'organization' => $data['autonomous_system_organization'] ?? null,
    ];
}

function isoToFlag(?string $iso): ?string
{
    if ($iso === null || strlen($iso) !== 2) {
        return null;
    }
    $iso = strtoupper($iso);
    $base = 0x1F1E6;
    $first = $base + (ord($iso[0]) - ord('A'));
    $second = $base + (ord($iso[1]) - ord('A'));
    return mb_chr($first, 'UTF-8') . mb_chr($second, 'UTF-8');
}

function respondError(int $status, string $detail): void
{
    respondJson($status, [
        'status' => $status,
        'detail' => $detail,
    ]);
}

function respondJson(int $status, array $payload): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
}

function toInt($value): ?int
{
    if ($value === null || $value === '') {
        return null;
    }
    return (int) $value;
}

function toFloat($value): ?float
{
    if ($value === null || $value === '') {
        return null;
    }
    return (float) $value;
}
