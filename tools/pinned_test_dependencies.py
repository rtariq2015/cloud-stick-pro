"""Locate already-installed pinned project test headers; never downloads files."""
from pathlib import Path
import json

def arduino_json_include(root: Path) -> Path:
    for library in sorted((root/'.pio/libdeps').glob('*/ArduinoJson')):
        metadata=library/'library.json'
        version=library/'src/ArduinoJson/version.hpp'
        if (metadata.is_file() and version.is_file() and (library/'src/ArduinoJson.h').is_file()
            and json.loads(metadata.read_text()).get('version')=='7.4.3'
            and '#define ARDUINOJSON_VERSION "7.4.3"' in version.read_text()):
            return library/'src'
    raise RuntimeError('Build a pinned project environment to install ArduinoJson 7.4.3 before native tests')
