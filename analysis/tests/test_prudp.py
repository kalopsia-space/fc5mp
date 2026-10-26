# TODO - fix: No module named 'fc5analyze'
from fc5analyze.prudp import decode

def test_decode_valid():
    packets = []
    packets[0] = bytes.fromhex('3f 31 20 00 00 00 00 00 00 00 00 00 00 00 3f 31 20 00')
    packets[1] = bytes.fromhex('31 3f 08 00 00 00 00 00 00 00 78 56 34 12 65 51 80 56')
    packets[2] = bytes.fromhex('3f 31 72 1c a0 89 1a 9d 2a 00 00 04 00 de ad be ef f8 99 3a 7c')

    for packet in packets:
        p = decode(packet)
        print(p.printout_string)