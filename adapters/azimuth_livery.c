#include "azimuth_host.h"
#include "logging.h"

#define MASK_HELPER_RVA 0x100b70u
#define MASK_HELPER_SIZE 0x2d2u

static const unsigned char recognize_two_samples[] = {0xf6, 0xc3, 0x06, 0x74, 0x1c};
static const unsigned char select_fragment_bits[] = {0x8b, 0xfb, 0xd1, 0xef, 0x45, 0x8b, 0xda, 0x45,
                                                     0x8b, 0xc2, 0x90, 0x90, 0x90, 0x90, 0x90};
static const HostCodePatch mask_patches[] = {
    {0x4b, sizeof(recognize_two_samples), recognize_two_samples},
    {0xbc, sizeof(select_fragment_bits), select_fragment_bits}};

static const unsigned char original_hash[32] = {
    0x25, 0xc8, 0xa4, 0xc4, 0x90, 0xb9, 0x62, 0x91, 0x7d, 0xed, 0x6c, 0xe2, 0x5f, 0xbc, 0xe7, 0xf2,
    0x92, 0x5c, 0xaa, 0xce, 0x86, 0x77, 0x94, 0x8e, 0x8a, 0x4c, 0xf3, 0x5b, 0x0e, 0x5c, 0x6e, 0x63};
static const unsigned char patched_hash[32] = {
    0xf7, 0x9b, 0x2e, 0x1c, 0x76, 0xb7, 0x3d, 0xcb, 0x14, 0x01, 0x76, 0xdd, 0x7e, 0x3e, 0xed, 0xcc,
    0xe1, 0x2e, 0x70, 0x20, 0x06, 0x78, 0x5c, 0xb9, 0x85, 0x4f, 0x92, 0x7b, 0x28, 0xe9, 0x0e, 0x02};

static const HostPatchSet mask_patch_set = {MASK_HELPER_RVA, MASK_HELPER_SIZE,
                                            original_hash,   patched_hash,
                                            mask_patches,    ARRAY_COUNT(mask_patches)};

static const unsigned char original_hash_097[32] = {
    0x6b, 0x39, 0x07, 0xf0, 0xed, 0x57, 0xca, 0x8c, 0x3c, 0x86, 0x90, 0xa9, 0x16, 0xa1, 0x92, 0x70,
    0xa8, 0xa9, 0xe6, 0x7d, 0xc8, 0x7b, 0xec, 0xf9, 0x15, 0x11, 0x91, 0x66, 0x1e, 0x9f, 0x82, 0xcb};
static const unsigned char patched_hash_097[32] = {
    0x5f, 0x7f, 0x89, 0x5f, 0x0f, 0xd1, 0x40, 0xa7, 0x97, 0x38, 0x05, 0xcf, 0xb6, 0xe0, 0x63, 0xc2,
    0x49, 0x35, 0x87, 0xb3, 0xae, 0xd0, 0x31, 0x4c, 0x7e, 0xed, 0xa1, 0xcb, 0x9f, 0x36, 0x17, 0x3c};
static const HostPatchSet mask_patch_set_097 = {0x104320,          MASK_HELPER_SIZE,
                                                original_hash_097, patched_hash_097,
                                                mask_patches,      ARRAY_COUNT(mask_patches)};

static int apply_livery(unsigned char *base) {
    AzimuthBuild build = azimuth_host_build(base);
    if (build == AZIMUTH_UNKNOWN) {
        return 0;
    }
    return azimuth_patch_host_code(base,
                                   build == AZIMUTH_097 ? &mask_patch_set_097 : &mask_patch_set);
}

void azimuth_apply_livery_compatibility(void) {
    int result = apply_livery((unsigned char *)GetModuleHandleW(NULL));
    if (result > 0) {
        log_append("AZIMUTH: 2x MSAA livery fragment-mask compatibility applied; "
                   "texture resolution and sample count preserved.\r\n");
    } else if (result < 0) {
        log_append("AZIMUTH: livery compatibility could not change memory protection; "
                   "host code left unchanged.\r\n");
    } else {
        log_append("AZIMUTH: 2x MSAA livery compatibility skipped; "
                   "host is not a supported inspected mask-helper build.\r\n");
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestAzimuthLiveryCompatibility(void *image) {
    return apply_livery(image);
}
#endif
