from dataclasses import dataclass

@dataclass
class PRUDPPacket:
    isValid : bool
    type : str
    source : int
    destination : int
    flags : str
    session : int
    seq : int
    checksumOK : bool

def printout_string(packet: PRUDPPacket) -> str:
    string = ''
    if packet.isValid:
        string = f'{packet.type} src={hex(packet.source)} dst={hex(packet.destination)} flags={packet.flags}'
    else:
        string = 'Packet is invalid'
    return string


def decode(data: bytes) -> PRUDPPacket:
    packetTypes = {
        0 : 'SYN',
        1 : 'CONNECT',
        2 : 'DATA',
        3 : 'DISCONNECT',
        4 : 'PING'
    }

    packetFlags = {
        8 : 'ACK',
        16 : 'RELIABLE',
        32 : 'NEED_ACK',
        64 : 'HAS_SIZE'
    }

    isValid = True
    source = data[0]
    destination = data[1]
    type = data[2] & 0b0000_0111
    if packetTypes.get(type) == None:
        isValid = False
    flags = data[2] & 0b1111_1000
    flagsString = ''
    comma = False
    for flag in packetFlags.keys():
        if (flags & flag) == flag:
            if comma:
                flagsString += ','
            flagsString += packetFlags[flag]
            comma = True
    session = data[3]

    packet = PRUDPPacket(isValid, packetTypes[type], source, destination, flagsString, session, 0, True)
    return packet

def test_decode_valid():
    packets = [
        bytes.fromhex('3f 31 20 00 00 00 00 00 00 00 00 00 00 00 3f 31 20 00'),
        bytes.fromhex('31 3f 08 00 00 00 00 00 00 00 78 56 34 12 65 51 80 56'),
        bytes.fromhex('3f 31 72 1c a0 89 1a 9d 2a 00 00 04 00 de ad be ef f8 99 3a 7c')
    ]

    for packet in packets:
        p = decode(packet)
        print(printout_string(p))

test_decode_valid()