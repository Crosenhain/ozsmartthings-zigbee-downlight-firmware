#include "zb_common.h"

/* Green Power is compiled out to keep the image inside the 512K OTA slot.
 * The prebuilt ZDO library still references this hook on device announce;
 * with no GP proxy/sink tables the real callback (gp.c gpDevAnnceCheckCb)
 * returns FALSE, so do the same (safe whether or not the library checks for NULL).
 * Signature must match gpDeviceAnnounceCheckCb_t in zigbee/gp/dGP_stub.h; gp.h
 * itself can't be included with ZCL_GP_SUPPORT disabled. */
typedef bool (*gp_stub_dev_annce_check_cb_t)(u16 aliasNwkAddr, addrExt_t aliasIeeeAddr);

static bool gp_stub_dev_annce_check(u16 alias_nwk_addr, addrExt_t alias_ieee_addr) {
    (void)alias_nwk_addr;
    (void)alias_ieee_addr;
    return FALSE;
}

gp_stub_dev_annce_check_cb_t g_gpDeviceAnnounceCheckCb = gp_stub_dev_annce_check;
