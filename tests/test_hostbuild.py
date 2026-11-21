## @file
#  @brief Collect Hostbuild disk, ISO, symbol, and JPEG tests.
import unittest

from tests.hostbuild_disk_cases import HostBuildDiskCases
from tests.hostbuild_iso_cases import HostBuildIsoCases
from tests.hostbuild_jpeg_cases import HostBuildJpegCases
from tests.hostbuild_symbol_cases import HostBuildSymbolCases


class HostBuildImageTests(HostBuildDiskCases, unittest.TestCase):
    """Check FAT16 creation, staging, and disk publication."""


class HostBuildIsoTests(HostBuildIsoCases, unittest.TestCase):
    """Check ISO construction, path tables, and fixture publication."""


class HostBuildSymbolTests(HostBuildSymbolCases, unittest.TestCase):
    """Check checked kernel symbol extraction and source publication."""


class HostBuildAssetTests(HostBuildJpegCases, unittest.TestCase):
    """Check JPEG validation and transactional asset wrapping."""


if __name__ == "__main__":
    unittest.main()
