#include "cupidbuild_iso.h"

static ctool_obj_iso_fixture_entry_t many[512];
static char paths[512][4];
static unsigned char manifest_bytes[512 * 5];

int main(void) {
  static const unsigned char data[] = {'a', 'b', 'c'};
  ctool_source_t file = {{{"d/f", 3u}}, {data, 3u}};
  ctool_source_t manifest = {{{"fixtures.manifest", 17u}},
                           {(const unsigned char *)"d\nd/f\n", 6u}};
  ctool_obj_iso_fixture_entry_t entries[] = {
      {{"d/f", 3u}, CTOOL_OBJ_ISO_FIXTURE_FILE, &file},
      {{"d", 1u}, CTOOL_OBJ_ISO_FIXTURE_DIRECTORY, (const ctool_source_t *)0}};
  ctool_obj_iso_fixture_request_t inventory = {entries, 2u};
  cupidbuild_iso_inventory_report_t report;
  char error[64];
  ctool_u32 index;
  if (!cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                         error, sizeof(error)) ||
      report.directories != 2u || report.files != 1u ||
      report.directory_depth != 2u || report.file_bytes != 3u || error[0] != '\0')
    return 1;

  manifest.contents.data = (const unsigned char *)"d\nd/g\n";
  if (cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                        error, sizeof(error)) ||
      report.directories != 0u || report.files != 0u ||
      report.directory_depth != 0u || report.file_bytes != 0u || error[0] == '\0')
    return 2;
  manifest.contents.data = (const unsigned char *)"d\nd/f\n";
  if (!cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                         (char *)0, 0u))
    return 3;
  if (cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                        (char *)0, 1u) || report.files != 0u)
    return 4;

  for (index = 0u; index < 512u; index++) {
    paths[index][0] = 'd';
    paths[index][1] = (char)('0' + index / 100u);
    paths[index][2] = (char)('0' + (index / 10u) % 10u);
    paths[index][3] = (char)('0' + index % 10u);
    many[index].path.data = paths[index];
    many[index].path.size = 4u;
    many[index].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    many[index].source = (const ctool_source_t *)0;
    manifest_bytes[index * 5u] = (unsigned char)paths[index][0];
    manifest_bytes[index * 5u + 1u] = (unsigned char)paths[index][1];
    manifest_bytes[index * 5u + 2u] = (unsigned char)paths[index][2];
    manifest_bytes[index * 5u + 3u] = (unsigned char)paths[index][3];
    manifest_bytes[index * 5u + 4u] = '\n';
  }
  manifest.contents.data = manifest_bytes;
  manifest.contents.size = sizeof(manifest_bytes);
  inventory.entries = many;
  inventory.entry_count = 512u;
  if (!cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                         error, sizeof(error)) ||
      report.directories != 513u || report.files != 0u ||
      report.directory_depth != 2u || report.file_bytes != 0u)
    return 5;
  inventory.entry_count = 513u;
  if (cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                        error, sizeof(error)) || report.directories != 0u)
    return 6;
  inventory.entry_count = 512u;
  return cupidbuild_iso_inventory_validate(&manifest, &inventory, &report,
                                            error, sizeof(error)) ? 0 : 7;
}
