# SPDX-License-Identifier: AGPL-3.0-or-later
# Product identity: the only place for identifiers that depend on the product name (SPEC §2, ADR-002).
# SUBGEN_PRODUCT_NAME: file and bundle names and install paths (letters, digits, ".", "_", "-" only);
# SUBGEN_DISPLAY_NAME: the name hosts, the editor and the installers show. scripts/package.sh reads both lines.
set(SUBGEN_PRODUCT_NAME "SUB-GEN")
set(SUBGEN_DISPLAY_NAME "SAM SUB GEN")
set(SUBGEN_VENDOR "Vendor")
set(SUBGEN_BUNDLE_ID "com.vendor.subgen")
set(SUBGEN_MANUFACTURER_CODE "Vndr")
set(SUBGEN_PLUGIN_CODE "Sbgn")
