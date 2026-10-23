# Task: PRUDP packet decoder

When you play FC5 co-op, the game finds the other player through a matchmaking server, and it talks to that server using an old protocol called PRUDP. PRUDP was made by a company called Quazal and runs over UDP. Every message is a small packet of raw bytes. We want a Python tool that takes one of those packets and tells you what's in it.

## The packets

Each packet is roughly:

- a 10 byte header: who it's from, who it's to, the packet type, a sequence number and a couple of other things
- some packet types have a few extra bytes after the header
- the content, if there is any
- a 4 byte checksum at the end, worked out from everything before it so the receiver can spot corrupted packets

Working out exactly which byte means what is the puzzle. Here are three to work from (hex, one byte per pair):

```
1)  3f 31 20 00 00 00 00 00 00 00 00 00 00 00 3f 31 20 00
2)  31 3f 08 00 00 00 00 00 00 00 78 56 34 12 65 51 80 56
3)  3f 31 72 1c a0 89 1a 9d 2a 00 00 04 00 de ad be ef f8 99 3a 7c
```

1. A real packet captured from the game: its first "I want to connect" message.
2. Our server's reply to it.
3. A data packet whose content is `de ad be ef`.

Our server code generated 2 and 3, not the game. That matters for the bonus question at the end.

## Hints

Try without these first if you like.

- The 3rd byte is two things packed into one: the packet type and a set of flags. Split it into the bottom 3 bits and the top 5 bits.
- There are 5 packet types: SYN, CONNECT, DATA, DISCONNECT and PING. There are 5 flags: ACK, RELIABLE, NEED_ACK, HAS_SIZE and MULTI_ACK. Work out which numbers go with which names.
- Numbers longer than one byte are little endian (lowest byte first), so `2a 00` is 42, not 10752.
- Compare packets 1 and 3. They have different extra fields after the header.
- The checksum is the bytes added up in chunks of 4.

## What to build

- **`analysis/fc5analyze/prudp.py`**: a `decode(data: bytes)` function that returns the decoded fields (a dataclass is good) and whether the checksum is correct.
- **A readable printout**: something like `SYN src=0x3f dst=0x31 flags=NEED_ACK seq=0 checksum=OK`.
- **Proper errors**: packets that are too short, have an unknown type or have a bad checksum should give a clear error, not crash.
- **`analysis/tests/test_prudp.py`**: tests that the three packets above decode correctly, plus a couple of broken ones, like a truncated packet or one with a byte changed.

Plain Python 3.12+, no extra libraries. `struct.unpack` will do most of the work.

## Setup

```sh
cd analysis
uv sync          # installs the project's dependencies
uv run pytest    # runs the tests
```

Work on a branch (`git checkout -b prudp-decoder`) and open a pull request when you're happy with it.

## Checking your answers

Our Rust version is in [`server/src/prudp/transport.rs`](../../server/src/prudp/transport.rs). Look at the `decode` and `compute_checksum` functions. That's the answer key, so have a proper go yourself before looking, or you won't get much practice out of it.

## Bonus: an actual open question

There's a checksum detail we haven't confirmed. If the packet length (minus the checksum) isn't a multiple of 4, there are 1 to 3 leftover bytes at the end. They either:

- get padded with zeros and added in, or
- get ignored.

Our server pads them, and that's how packets 2 and 3 above were made. But some sources say the real protocol ignores them. Packet 1, the only real one from the game, can't settle it, because its leftover bytes are zeros either way.

If you can work out which one is right, that would genuinely help the project. Good places to dig are [nex-go](https://github.com/PretendoNetwork/nex-go) (look for `prudp_packet_v0.go`) and the Pretendo and kinnay NintendoClients wikis. Write up how you reached your answer in the PR.
