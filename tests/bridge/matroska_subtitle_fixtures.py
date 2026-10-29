"""Small, distributable extraction fixtures. No installed demuxer required."""
import pathlib
import struct
import sys
import zlib
from embedded_subtitle_playback import pgs_sample


def vint(number):
    for width in range(1, 9):
        if number < (1 << (width * 7)) - 1:
            return ((1 << (width * 7)) | number).to_bytes(width, 'big')
    raise ValueError(number)


def element(tag, data):
    return bytes.fromhex(tag) + vint(len(data)) + data


def uint(tag, number):
    return element(tag, number.to_bytes(max(1, (number.bit_length() + 7) // 8), 'big'))


def make(path, encoding=None, pgs=False, ass=False):
    text = lambda tag, value: element(tag, value.encode())
    header = element('1a45dfa3', text('4282', 'matroska') + uint('4285', 2))
    info = element('1549a966', uint('2ad7b1', 1000000))
    tracks = element('ae', uint('d7', 1)+uint('73c5', 1)+uint('83', 1)+text('86', 'V_MJPEG'))
    for number, language in ((2, 'eng'), (3, 'zho')):
        entry = uint('d7', number)+uint('73c5', number+100)+uint('83', 17)
        entry += text('22b59c', language)+text('536e', f'Source {number}')
        entry += text('86', 'S_HDMV/PGS' if pgs else 'S_TEXT/ASS' if ass else 'S_TEXT/UTF8')
        if ass:
            entry += text('63a2', '[Script Info]\nScriptType: v4.00+\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n')
        if encoding:
            compression = uint('4254', 0 if encoding == 'zlib' else 3)
            if encoding == 'header':
                compression += element('4255', b'line ')
            entry += element('6d80', element('6240', uint('5031', 0)+uint('5032', 1)
                             +element('5034', compression)))
        tracks += element('ae', entry)
    clusters = element('1f43b675', uint('e7', 0)+element('a3', b'\x81\0\0\x80VIDEO_START'))
    for start, end in ((2000, 5000), (90000, 93000)):
        # Unselected video data is deliberately large, recognizable, and not
        # a valid JPEG: extraction must neither decode nor return these bytes.
        body = uint('e7', start)+element('a3', b'\x81\0\0\x80'+b'UNSELECTED_VIDEO'*10000)
        for track in (2, 3):
            data = pgs_sample(start % 65536) if pgs else b'line '+str(track).encode()
            if ass:
                data = b'12,3,Default,Speaker,1,2,3,Scroll up;0;100;10,{\\pos(600,20)}line '+str(track).encode()+b'\\Nsecond, clause'
            if encoding == 'zlib':
                data = zlib.compress(data)
            elif encoding == 'header':
                data = data[5:]
            block = vint(track)+struct.pack('>hB', 0, 0)+data
            body += element('a0', element('a1', block)+uint('9b', end-start))
            if pgs:
                body += element('a3', vint(track)+struct.pack('>hB', end-start, 0x80)+pgs_sample((start+1) % 65536, clear=True))
        clusters += element('1f43b675', body)
    path.write_bytes(header+element('18538067', info+element('1654ae6b', tracks)+clusters))


if __name__ == '__main__':
    folder = pathlib.Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    make(folder/'plain.mkv')
    make(folder/'zlib.mkv', encoding='zlib')
    make(folder/'header.mkv', encoding='header')
    make(folder/'pgs.mkv', pgs=True)
    make(folder/'ass.mkv', ass=True)
