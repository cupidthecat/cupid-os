## @file
#  @brief Collect build graph regression tests under their original unittest names.
import unittest

from tests.build_graph_root_cases import RootGraphCases
from tests.build_graph_ownership_cases import BuildOwnershipCases
from tests.build_graph_directive_cases import DirectiveInventoryCases
from tests.build_graph_source_cases import SourceInventoryCases
from tests.build_graph_seed_cases import SeedDeliveryCases
from tests.build_graph_fixed_point_cases import FixedPointGraphCases
from tests.build_graph_compiler_profile_cases import CompilerProfileCases
from tests.build_graph_publication_cases import BuildPublicationCases
from tests.build_graph_seed_product_cases import SeedProductCases
from tests.build_graph_kernel_recipe_cases import KernelRecipeCases


class BuildGraphAuditCliTests(
    RootGraphCases,
    BuildOwnershipCases,
    DirectiveInventoryCases,
    SourceInventoryCases,
    SeedDeliveryCases,
    FixedPointGraphCases,
    CompilerProfileCases,
    BuildPublicationCases,
    SeedProductCases,
    KernelRecipeCases,
    unittest.TestCase,
):
    pass


if __name__ == "__main__":
    unittest.main()
