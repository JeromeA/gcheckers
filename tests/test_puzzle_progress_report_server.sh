#!/bin/sh

set -eu

: "${GCHECKERS_PUZZLE_REPORT_URL:?GCHECKERS_PUZZLE_REPORT_URL must be set}"
: "${GCHECKERS_PUZZLE_REPORT_STORE_DIR:?GCHECKERS_PUZZLE_REPORT_STORE_DIR must be set}"

uuid='3c98f7b2-1111-4111-8111-123456789abc'
report_file="${GCHECKERS_PUZZLE_REPORT_STORE_DIR}/${uuid}.jsonl"

rm -f "${report_file}"

response=$(
  curl -fsS -X POST "${GCHECKERS_PUZZLE_REPORT_URL}" \
    -H 'Content-Type: application/json' \
    --data '{"schema_version":1,"user_id":"3c98f7b2-1111-4111-8111-123456789abc","client":{"app_id":"io.github.jeromea.gcheckers","app_version":"dev"},"attempts":[{"attempt_id":"8db8aaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","puzzle_id":"international/puzzle-0007.sgf","puzzle_number":7,"puzzle_source_name":"puzzle-0007.sgf","puzzle_ruleset":"international","attacker":"white","started_unix_ms":1713300000000,"finished_unix_ms":1713300005000,"result":"failure","failure_on_first_move":true,"report_count":0,"failed_first_move":{"length":2,"captures":0,"path":[12,16]}}]}'
)

printf '%s' "${response}" | grep -F '"ok": true' >/dev/null
printf '%s' "${response}" | grep -F "\"user_id\": \"${uuid}\"" >/dev/null

test -f "${report_file}"
grep -F "\"user_id\":\"${uuid}\"" "${report_file}" >/dev/null
grep -F '"puzzle_id":"international/puzzle-0007.sgf"' "${report_file}" >/dev/null

printf 'Puzzle progress PHP server test passed.\n'
