<?php

declare(strict_types=1);

if (getenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR') === false || getenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR') === '') {
  putenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR=' . __DIR__ . '/puzzle-progress-report-data');
}

function gcheckers_report_server_send_json(int $status, array $payload): void
{
  http_response_code($status);
  header('Content-Type: application/json');
  echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n";
}

function gcheckers_report_server_store_dir(): string
{
  $override = getenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR');
  if (is_string($override) && $override !== '') {
    return $override;
  }

  return __DIR__ . '/puzzle-progress-report-data';
}

function gcheckers_report_server_uuid_is_valid(string $uuid): bool
{
  return preg_match('/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$/',
                    $uuid) === 1;
}

function gcheckers_report_server_append_report(string $store_dir, string $user_id, string $raw_body): string
{
  if (!is_dir($store_dir) && !mkdir($store_dir, 0775, true) && !is_dir($store_dir)) {
    throw new RuntimeException(sprintf('Failed to create report directory %s', $store_dir));
  }

  $path = sprintf('%s/%s.jsonl', rtrim($store_dir, '/'), $user_id);
  $bytes = file_put_contents($path, $raw_body . "\n", FILE_APPEND | LOCK_EX);
  if ($bytes === false) {
    throw new RuntimeException(sprintf('Failed to append report to %s', $path));
  }

  return $path;
}

function gcheckers_report_server_handle_request(): void
{
  $method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
  if ($method !== 'POST') {
    header('Allow: POST');
    gcheckers_report_server_send_json(405, ['ok' => false, 'error' => 'Only POST is supported']);
    return;
  }

  $raw_body = file_get_contents('php://input');
  if (!is_string($raw_body) || $raw_body === '') {
    gcheckers_report_server_send_json(400, ['ok' => false, 'error' => 'Request body must be non-empty JSON']);
    return;
  }

  try {
    $payload = json_decode($raw_body, true, 512, JSON_THROW_ON_ERROR);
  } catch (JsonException $e) {
    gcheckers_report_server_send_json(400, ['ok' => false, 'error' => 'Invalid JSON: ' . $e->getMessage()]);
    return;
  }

  if (!is_array($payload)) {
    gcheckers_report_server_send_json(400, ['ok' => false, 'error' => 'JSON body must decode to an object']);
    return;
  }

  $user_id = $payload['user_id'] ?? null;
  if (!is_string($user_id) || !gcheckers_report_server_uuid_is_valid($user_id)) {
    gcheckers_report_server_send_json(400, ['ok' => false, 'error' => 'user_id must be a UUID string']);
    return;
  }

  $attempts = $payload['attempts'] ?? null;
  if (!is_array($attempts)) {
    gcheckers_report_server_send_json(400, ['ok' => false, 'error' => 'attempts must be an array']);
    return;
  }

  try {
    $stored_path = gcheckers_report_server_append_report(gcheckers_report_server_store_dir(), $user_id, $raw_body);
  } catch (RuntimeException $e) {
    gcheckers_report_server_send_json(500, ['ok' => false, 'error' => $e->getMessage()]);
    return;
  }

  gcheckers_report_server_send_json(200, [
    'ok' => true,
    'user_id' => $user_id,
    'stored_path' => $stored_path,
    'attempt_count' => count($attempts),
  ]);
}

gcheckers_report_server_handle_request();
