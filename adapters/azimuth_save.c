#include "azimuth_host.h"
#include "azimuth_markers.h"
#include "logging.h"

/* v1.0.1 uses the same blob validator for revision selection and actual reads.
   Keep parsing, SHA calculation, buffer sizes, cleanup and save writes native;
   bypass only the final size/hash rejection branches when requested. */
static const unsigned char skip_rejection[] = {0x90,0x90,0x90,0x90,0x90,0x90};
static const HostCodePatch reader_patches[] = {
    {0x8, sizeof(skip_rejection), skip_rejection},
    {0x3bb, sizeof(skip_rejection), skip_rejection}};
static const unsigned char reader_original_hash[32] = {
    0x0c,0x4e,0x17,0xc8,0x19,0xf2,0x70,0xeb,0xd1,0x41,0xe0,0xd8,0x7a,0x5a,0xbb,0xa6,
    0xcb,0x95,0x14,0x80,0x91,0xe4,0x89,0xcd,0x4d,0xd8,0x2f,0x4c,0xdf,0x75,0x71,0x81};
static const unsigned char reader_patched_hash[32] = {
    0x7e,0x3b,0x52,0x3a,0x24,0xa6,0x04,0x0c,0xe7,0xf6,0xe5,0xd0,0xab,0x95,0x4b,0x29,
    0xa9,0x6d,0x22,0x5f,0x31,0xee,0x2f,0xf0,0x85,0x7a,0xb5,0xd4,0x0b,0x8b,0xaf,0xb3};
static const HostPatchSet validation_patch_set = {
    0x5f351b, 0x3c1, reader_original_hash, reader_patched_hash,
    reader_patches, ARRAY_COUNT(reader_patches)};

static int apply_save(unsigned char *base) {
    return azimuth_host_build(base) == AZIMUTH_101
               ? azimuth_patch_host_code(base, &validation_patch_set) : 0;
}

void azimuth_apply_save_compatibility(int disable_checksums) {
    if (!disable_checksums) {
        log_append("AZIMUTH: native save checksum and file-size verification "
                   "enabled.\r\n");
        return;
    }
    int result = apply_save((unsigned char *)GetModuleHandleW(NULL));
    if (result > 0) {
        log_append("AZIMUTH: save checksum and file-size comparisons disabled; "
                   "marker format and normal save writing retained.\r\n");
        azimuth_repair_missing_markers((unsigned char *)GetModuleHandleW(NULL));
    } else if (result < 0) {
        log_append("AZIMUTH: save checksum override could not be applied.\r\n");
    } else {
        log_append("AZIMUTH: save checksum override skipped; "
                   "host is not a supported inspected save-validation build.\r\n");
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestAzimuthSaveCompatibility(void *image, int disable_checksums) {
    return disable_checksums ? apply_save(image) : 0;
}
#endif
