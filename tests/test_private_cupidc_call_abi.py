"""Private compiler execution contracts, grouped by language feature."""

from tests.private_cupidc.harness import PrivateCupidCHarness
from tests.private_cupidc.integers_1 import Integers1Cases
from tests.private_cupidc.storage_1 import Storage1Cases
from tests.private_cupidc.simd_1 import Simd1Cases
from tests.private_cupidc.simd_2 import Simd2Cases
from tests.private_cupidc.callbacks_1 import Callbacks1Cases
from tests.private_cupidc.callbacks_2 import Callbacks2Cases
from tests.private_cupidc.callbacks_3 import Callbacks3Cases
from tests.private_cupidc.callbacks_4 import Callbacks4Cases
from tests.private_cupidc.bindings_1 import Bindings1Cases
from tests.private_cupidc.floating_1 import Floating1Cases
from tests.private_cupidc.language_1 import Language1Cases
from tests.private_cupidc.calls_1 import Calls1Cases
from tests.private_cupidc.repl_1 import Repl1Cases

class PrivateCupidCCallAbiTests(
    Integers1Cases,
    Storage1Cases,
    Simd1Cases,
    Simd2Cases,
    Callbacks1Cases,
    Callbacks2Cases,
    Callbacks3Cases,
    Callbacks4Cases,
    Bindings1Cases,
    Floating1Cases,
    Language1Cases,
    Calls1Cases,
    Repl1Cases,
    PrivateCupidCHarness,
):
    pass
