## @file
#  @brief Verify checked seed behavior, provenance, and toolchain convergence.
import unittest

from tests.bootstrap_seed_certification_cases import BootstrapSeedCertificationCases
from tests.bootstrap_seed_codegen_cases import BootstrapSeedCodegenCases
from tests.bootstrap_seed_custody_cases import BootstrapSeedCustodyCases
from tests.bootstrap_seed_installed_cases import BootstrapSeedInstalledCases
from tests.bootstrap_seed_manifest_cases import BootstrapSeedManifestCases
from tests.bootstrap_seed_matrix_cases import BootstrapSeedMatrixCases
from tests.bootstrap_seed_publication_cases import BootstrapSeedPublicationCases
from tests.bootstrap_seed_reconstruction_cases import BootstrapSeedReconstructionCases
from tests.bootstrap_seed_runner_cases import BootstrapSeedRunnerCases
from tests.bootstrap_seed_suite_fixtures import BootstrapSeedFixtures


class ToolchainBootstrapSeedCliTests(
    BootstrapSeedFixtures,
    BootstrapSeedManifestCases,
    BootstrapSeedReconstructionCases,
    BootstrapSeedCustodyCases,
    BootstrapSeedCertificationCases,
    BootstrapSeedInstalledCases,
    BootstrapSeedCodegenCases,
    BootstrapSeedPublicationCases,
    BootstrapSeedMatrixCases,
    BootstrapSeedRunnerCases,
    unittest.TestCase,
):
    """Verify the checked seeds across both supported host platforms."""


if __name__ == "__main__":
    unittest.main()
