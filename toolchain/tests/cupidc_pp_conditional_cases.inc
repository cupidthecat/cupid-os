/* Checked active-source conditional-expression manifest.
 *
 * Define CUPIDC_PP_CONDITIONAL_CASE(expression, if_count, elif_count,
 * expected_i386) before including this file.  The expected result uses the
 * freestanding C11 i386 profile: __STDC_VERSION__=201112L, four-byte
 * pointers, __BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__,
 * OPL_ENABLE_STEREOEXT=0, OPL_QUIRK_CHANNELSAMPLEDELAY=1,
 * __bool_true_false_are_defined=1, ORIGCODE undefined, and the host/platform
 * macros undefined.
 */
CUPIDC_PP_CONDITIONAL_CASE("! defined ( _WIN32 ) && ! defined ( __MACOSX__ ) && ! defined ( __DJGPP__ )", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("! defined ( __STDC_VERSION__ ) || ( __STDC_VERSION__ < 202311L )", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("! defined ( __cplusplus )", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )", 1u, 1u, 0)
CUPIDC_PP_CONDITIONAL_CASE("( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("0", 18u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("1", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("OPL_ENABLE_STEREOEXT", 11u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("OPL_ENABLE_STEREOEXT && ! defined OPL_SIN", 1u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("OPL_QUIRK_CHANNELSAMPLEDELAY", 4u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("ORIGCODE", 31u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("_MSC_VER < 1400", 1u, 0u, 1)
CUPIDC_PP_CONDITIONAL_CASE("_WIN64", 1u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( _MSC_VER ) && ! defined ( __cplusplus )", 1u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( _WIN32 )", 18u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( _WIN32 ) && ! defined ( _WIN32_WCE )", 1u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( _WIN32 ) || defined ( __DJGPP__ )", 2u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( __DJGPP__ )", 0u, 2u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( __MACOSX__ )", 0u, 1u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( __SIZEOF_POINTER__ ) && ( __SIZEOF_POINTER__ == 8 )", 1u, 0u, 0)
CUPIDC_PP_CONDITIONAL_CASE("defined ( __cplusplus ) || defined ( __bool_true_false_are_defined )", 1u, 0u, 1)
