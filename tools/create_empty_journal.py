"""Generate the public empty internal journal; never reads/writes a dongle."""
import hashlib
import importlib.metadata
from pathlib import Path
import sys


def main():
    if len(sys.argv) != 2 or importlib.metadata.version('littlefs-python') != '0.19.0':
        raise SystemExit('Use littlefs-python 0.19.0 and one output filename')
    import littlefs
    context = littlefs.UserContext(buffsize=0x100000)
    fs = littlefs.LittleFS(context=context, mount=False, block_size=4096, block_count=256,
                          read_size=256, prog_size=256, cache_size=512, lookahead_size=128, block_cycles=512)
    fs.format(); fs.mount(); fs.mkdir('/queue')
    if fs.listdir('/') != ['queue'] or fs.listdir('/queue') != []:
        raise SystemExit('Unexpected journal contents')
    fs.unmount(); data = bytes(context.buffer)
    if hashlib.sha256(data).hexdigest() != '2853d81054fb721d2ecef4dd9c5b6185f1c9b9163605528d822afdad7a1c95f4':
        raise SystemExit('Journal image differs from reviewed format')
    Path(sys.argv[1]).write_bytes(data)
    print('Empty internal /queue journal created; microSD not accessed')


if __name__ == '__main__': main()
