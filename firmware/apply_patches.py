"""Apply DL41 changes to a copy of nminaylov/zigbee-light-cct.

usage: python3 apply_patches.py <path to zigbee-light-cct copy> <app_build 0..255>
Idempotent: re-running leaves an already patched tree unchanged.
Tested against upstream commit f7441cb4be0f7294e2e175a03159239ef7827f9f. The edits are text
replacements, so the script checks afterwards that every one landed and exits 1 if not.
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def patch(path, fn):
    src = open(path).read()
    out = fn(src)
    if out != src:
        open(path, "w").write(out)
        print(f"patched {path}")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root = sys.argv[1]
    build = int(sys.argv[2])  # APP_BUILD: firmware version 1.0.<build>
    if not 0 <= build <= 255:
        sys.exit("app_build must be 0..255 (APP_BUILD is one byte)")
    src = os.path.join(root, "src")

    for f in sorted(os.listdir(os.path.join(HERE, "src"))):
        shutil.copy(os.path.join(HERE, "src", f), os.path.join(src, f))
        print(f"copied {f}")

    # Upstream makefile has a space-indented recipe (unused rule) that GNU make rejects.
    patch(os.path.join(root, "makefile"),
          lambda m: re.sub(r"\$\(OBJ_DIR\)/bin_updater\.o:.*?\n[ \t]+@objcopy[^\n]*\n", "", m, flags=re.S))

    def main_c(m):
        if "tuya_migrate" in m:
            return m
        m = m.replace("#include <stdint.h>\n", '#include <stdint.h>\n#include "tuya_migrate.h"\n', 1)
        m = m.replace("int main(void) {\n",
                      "_attribute_ram_code_sec_noinline_ int main(void) {\n"
                      "    /* Must run before any flash-resident code (see tuya_migrate.h). */\n"
                      "    tuya_migrate_stage1();\n\n", 1)
        return m.replace("    startup_state_e state = drv_platform_init();\n",
                         "    startup_state_e state = drv_platform_init();\n"
                         "    /* Needs the system timer; must precede any NV access. */\n"
                         "    tuya_migrate_stage2();\n", 1)
    patch(os.path.join(src, "main.c"), main_c)

    def variants(h):
        if "HW_VARIANT_DL41_TEST" in h:
            return h
        return h.replace("#define HW_VARIANT_NONE NULL", """// Oz Smart DL41 bench test: ZTU module only, nothing driven
#define HW_VARIANT_DL41_TEST &hw_config_dl41_test
static const HwConfig hw_config_dl41_test = {
    .cold_pwm_pin = PIN_NC,
    .warm_pwm_pin = PIN_NC,
    .pwm_freq_hz = 1000,

    .cold_temp_k = 5500,
    .warm_temp_k = 4000,

    .status_led_pin = PIN_NC,
    .button_pin = PIN_NC,

    .pin_invert_mask = 0,

    .model_name = {.string = "DL41-CUSTOM-TEST"},
};

#define HW_VARIANT_NONE NULL""")
    patch(os.path.join(src, "hw_variants.h"), variants)

    # Distinct OTA image type so public OTA indexes never match this build.
    patch(os.path.join(src, "version_cfg.h"),
          lambda v: v.replace("#define IMAGE_TYPE_LIGHT_CCT  (0x01)",
                              "#define IMAGE_TYPE_LIGHT_CCT  (0x41) /* DL41 custom build */")
                     .replace("#define APP_BUILD     0x01 //app build 01",
                              f"#define APP_BUILD     0x{build:02x} //app build {build:02d} (DL41)"))

    extra_objs = [os.path.splitext(f)[0] + ".o" for f in sorted(os.listdir(os.path.join(HERE, "src"))) if f.endswith(".c")]

    def project(p):
        for obj in extra_objs:
            line = f"$(OUT_PATH)/$(SRC_PATH)/{obj} \\\n"
            if line not in p:
                p = p.replace("$(OUT_PATH)/$(SRC_PATH)/main.o \\\n", "$(OUT_PATH)/$(SRC_PATH)/main.o \\\n" + line, 1)
        return p
    patch(os.path.join(root, "project.mk"), project)

    # --- Image size trims (512K OTA slot is 0x34000 bytes) ---
    # Green Power proxy (~18 KB) and Touchlink (~11 KB) aren't needed for a Z2M-paired router.
    patch(os.path.join(src, "app_cfg.h"),
          lambda c: c.replace("#define TOUCHLINK_SUPPORT     1", "#define TOUCHLINK_SUPPORT     0")
                     .replace("#define ZCL_GP_SUPPORT                  1", "#define ZCL_GP_SUPPORT                  0"))
    patch(os.path.join(root, "makefile"),
          lambda m: m.replace("-include $(MAKE_INCLUDES)/gp.mk", "# gp.mk removed: Green Power disabled (see src/gp_stub.c)"))

    def app_c(a):
        if "#if ZCL_GP_SUPPORT\n    gp_init" in a:
            return a
        return a.replace("    gp_init(LIGHT_ENDPOINT);\n", "#if ZCL_GP_SUPPORT\n    gp_init(LIGHT_ENDPOINT);\n#endif\n", 1)
    patch(os.path.join(src, "app.c"), app_c)

    def ep_cfg(e):
        if "#if TOUCHLINK_SUPPORT\n    ZCL_CLUSTER_TOUCHLINK_COMMISSIONING" in e:
            return e
        return e.replace("    ZCL_CLUSTER_TOUCHLINK_COMMISSIONING,\n",
                         "#if TOUCHLINK_SUPPORT\n    ZCL_CLUSTER_TOUCHLINK_COMMISSIONING,\n#endif\n", 1)
    patch(os.path.join(src, "zb_ep_cfg.c"), ep_cfg)

    # Every edit above is a text replacement that silently does nothing if upstream changed: verify.
    expected = [
        ("makefile", "gp.mk removed: Green Power disabled"),
        ("src/main.c", "tuya_migrate_stage1();"),
        ("src/main.c", "tuya_migrate_stage2();"),
        ("src/main.c", "_attribute_ram_code_sec_noinline_ int main(void)"),
        ("src/hw_variants.h", "HW_VARIANT_DL41_TEST"),
        ("src/version_cfg.h", "#define IMAGE_TYPE_LIGHT_CCT  (0x41)"),
        ("src/version_cfg.h", f"#define APP_BUILD     0x{build:02x} "),
        ("project.mk", "$(OUT_PATH)/$(SRC_PATH)/tuya_migrate.o"),
        ("src/app_cfg.h", "#define TOUCHLINK_SUPPORT     0"),
        ("src/app_cfg.h", "#define ZCL_GP_SUPPORT                  0"),
        ("src/app.c", "#if ZCL_GP_SUPPORT\n    gp_init"),
        ("src/zb_ep_cfg.c", "#if TOUCHLINK_SUPPORT\n    ZCL_CLUSTER_TOUCHLINK_COMMISSIONING"),
    ]
    missing = [f"{f}: {text!r}" for f, text in expected if text not in open(os.path.join(root, f)).read()]
    if missing:
        sys.exit("patch check failed (upstream changed?):\n  " + "\n  ".join(missing))
    print(f"all patches applied (APP_BUILD {build})")


if __name__ == "__main__":
    main()
