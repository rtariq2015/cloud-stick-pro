"""Regression guard for the physical end-of-scan panic; not a whole-program proof.

ESP-IDF libc aborts if localtime_r reaches a recursive environment lock while
interrupts are disabled. Keep time conversion and known blocking I/O outside
the short status spinlock. Check the production source, plus the old failure.
"""
import re
from pathlib import Path

regions = re.compile(r'portENTER_CRITICAL\([^)]*\);(.*?)portEXIT_CRITICAL\(', re.S)
blocking = re.compile(r'\b(backup_window_open|localtime_r|gmtime_r|storage_ready|storage_owner|load_config|save_config|vTaskDelay|xSemaphoreTake)\s*\(')

def violations(source):
    return [m.group(1) for region in regions.findall(source) for m in blocking.finditer(region)]

old = 'portENTER_CRITICAL(&statusMux);status.scanComplete=consistent&&backup_window_open();portEXIT_CRITICAL(&statusMux);'
assert violations(old) == ['backup_window_open']
root = Path(__file__).resolve().parents[1]
count = 0
for file in (root/'src').glob('*.cpp'):
    source = file.read_text()
    count += len(regions.findall(source))
    assert not violations(source), (file.name, violations(source))
assert count >= 10
print(f'Critical-section regression: {count} regions checked; original panic path rejected')
