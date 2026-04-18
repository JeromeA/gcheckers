<?php

declare(strict_types=1);

if (getenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR') === false || getenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR') === '') {
  putenv('GCHECKERS_PUZZLE_REPORT_STORE_DIR=' . __DIR__ . '/puzzle-progress-report-data');
}

require __DIR__ . '/../tools/puzzle_progress_report_server.php';
