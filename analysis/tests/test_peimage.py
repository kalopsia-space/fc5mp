import pytest
from fc5analyze.peimage import PEImage, VAError, BuildMismatch, DEFAULT_DLL

def img():
    return PEImage(DEFAULT_DLL)

def test_image_base_and_sections():
    pe = img()
    assert pe.image_base == 0x180000000
    xp = pe.section_by_name(".xpdata")
    assert xp.chars == 0xE0000020          # RWX code
    xt = pe.section_by_name(".xtls")
    assert xt.chars == 0x40000040          # readable data/strings

def test_va_roundtrip_known_string():
    pe = img()
    # "Calling RDV CreateSession" lives at VA 0x1841fb128 (.xtls)
    data = pe.read_va(0x1841FB128, 25)
    assert data == b"Calling RDV CreateSession"

def test_va_out_of_range_raises():
    pe = img()
    with pytest.raises(VAError):
        pe.va_to_off(0x1FFFFFFFF)          # far above image

def test_sha_guard():
    pe = img()
    pe.assert_sha(pe.sha256)               # self matches
    with pytest.raises(BuildMismatch):
        pe.assert_sha("00" * 32)
