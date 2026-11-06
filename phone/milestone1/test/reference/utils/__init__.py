# Test support for Espressif's unmodified SRP reference; no hardware access.
def long_to_bytes(value):
    return int(value).to_bytes(max(1, (int(value).bit_length()+7)//8), 'big')

def bytes_to_long(value):
    return int.from_bytes(value, 'big')
