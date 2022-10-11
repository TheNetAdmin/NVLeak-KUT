import click
import random
import struct


def gen_byte(pattern: int, total_bytes: int, curr_bytes: int) -> int:
    if pattern == 0:
        return 0xAA
    elif pattern == 1:
        return 0xCC
    elif pattern == 2:
        return 0xF0
    elif pattern == 3:
        return random.randint(0x00, 0xFF)
    elif pattern == 4:
        return 0xFF
    elif pattern == 5:
        return 0x00
    elif pattern == 6:
        if 0 < curr_bytes <= total_bytes / 4 or total_bytes / 4 * 3 < curr_bytes:
            return 0x00
        else:
            return 0xFF


def gen_64bit(pattern: int, total_bytes: int, curr_bytes: int) -> int:
    d = 0
    for i in range(8):
        d |= gen_byte(pattern, total_bytes, curr_bytes + i) << (i * 8)
    return d


@click.command()
@click.option("-b", "--total_bits", required=True, default=64)
@click.option("-o", "--output_file", required=True)
@click.option("-p", "--pattern", required=True, type=int)
def gen_pattern(total_bits, output_file, pattern):
    """
    Pattern:
        0: 0b01010101....
        1: 0b00110011....
        2: 0b00001111....
        3: random
        4: 0b11111111....
        5: 0b00000000....
        6: 0b0000ffff0000ffff....
    """

    assert total_bits >= 64
    assert total_bits % 64 == 0
    assert 0 <= pattern <= 6
    total_bytes = total_bits / 8
    data = []
    # Setup stage
    setup_data = 0xCC  # First 8 bits are for setup, always 0xcc
    curr_bytes = 1
    for i in range(7):
        curr_bytes += 1
        setup_data |= gen_byte(pattern, total_bytes, curr_bytes) << ((i + 1) * 8)
    data.append(setup_data)
    for _ in range(total_bits // 64 - 1):
        data.append(gen_64bit(pattern, total_bytes, curr_bytes))
        curr_bytes += 8

    with open(output_file, "wb") as f:
        for d in data:
            b = struct.pack("Q", d)
            f.write(b)


if __name__ == "__main__":
    gen_pattern()
