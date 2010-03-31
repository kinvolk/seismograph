/* Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Data structure and API definitions for a verified boot kernel image.
 * (Firmware Portion)
 */

#ifndef VBOOT_REFERENCE_KERNEL_IMAGE_FW_H_
#define VBOOT_REFERENCE_KERNEL_IMAGE_FW_H_

#include <stdint.h>
#include "rsa.h"
#include "sha.h"

#define KERNEL_MAGIC "CHROMEOS"
#define KERNEL_MAGIC_SIZE 8
#define KERNEL_CMD_LINE_SIZE 4096

#define DEV_MODE_ENABLED 1
#define DEV_MODE_DISABLED 0

/* Kernel config file options according to the Chrome OS drive map design. */
typedef struct kconfig_options {
  uint32_t version[2];  /* Configuration file version. */
  uint8_t cmd_line[KERNEL_CMD_LINE_SIZE];  /* Kernel command line option string
                                            * terminated by a NULL character. */
  uint64_t kernel_len;  /* Size of the kernel. */
  uint64_t kernel_load_addr;  /* Load address in memory for the kernel image */
  uint64_t kernel_entry_addr;  /* Address to jump to after kernel is loaded. */
} kconfig_options;

typedef struct KernelImage {
  uint8_t magic[KERNEL_MAGIC_SIZE];
  /* Key header */
  uint16_t header_version;  /* Header version. */
  uint16_t header_len;  /* Length of the header. */
  uint16_t firmware_sign_algorithm;  /* Signature algorithm used by the firmware
                                      * signing key (used to sign this kernel
                                      * header. */
  uint16_t kernel_sign_algorithm;  /* Signature algorithm used by the kernel
                                    * signing key. */
  uint16_t kernel_key_version;  /* Key Version# for preventing rollbacks. */
  uint8_t* kernel_sign_key;  /* Pre-processed public half of signing key. */
  /* TODO(gauravsh): Do we need a choice of digest algorithms for the header
   * checksum? */
  uint8_t header_checksum[SHA512_DIGEST_SIZE];  /* SHA-512 Crytographic hash of
                                                 * the concatenation of the
                                                 * header fields, i.e.
                                                 * [header_len,
                                                 * firmware_sign_algorithm,
                                                 * sign_algorithm, sign_key,
                                                 * key_version] */

  uint8_t* kernel_key_signature;   /* Signature of the header above. */

  uint16_t kernel_version;  /* Kernel Version# for preventing rollbacks. */
  kconfig_options options;  /* Other kernel/bootloader options. */

  uint8_t* config_signature;  /* Signature of the kernel config file. */

  /* The kernel signature comes first as it may allow us to parallelize
   * the kernel data fetch and RSA public key operation.
   */
  uint8_t* kernel_signature;  /* Signature on the concatenation of
                               * [kernel_version], [options] and
                               * [kernel_data]. */
  uint8_t* kernel_data;  /* Actual kernel data. */

} KernelImage;

/* Error Codes for VerifyFirmware. */
#define VERIFY_KERNEL_SUCCESS 0
#define VERIFY_KERNEL_INVALID_IMAGE 1
#define VERIFY_KERNEL_KEY_SIGNATURE_FAILED 2
#define VERIFY_KERNEL_INVALID_ALGORITHM 3
#define VERIFY_KERNEL_CONFIG_SIGNATURE_FAILED 4
#define VERIFY_KERNEL_SIGNATURE_FAILED 5
#define VERIFY_KERNEL_WRONG_MAGIC 6
#define VERIFY_KERNEL_MAX 7  /* Generic catch-all. */

extern char* kVerifyKernelErrors[VERIFY_KERNEL_MAX];

/* Checks for the sanity of the kernel header pointed by [kernel_header_blob].
 * If [dev_mode] is enabled, also checks the firmware key signature using the
 * pre-processed public firmware signing  key [firmware_sign_key_blob].
 *
 * On success, put firmware signature algorithm in [firmware_algorithm],
 * kernel signature algorithm in [kernel_algorithm], kernel header
 * length in [header_len], and return 0.
 * Else, return error code on failure.
 */
int VerifyKernelHeader(const uint8_t* firmware_sign_key_blob,
                       const uint8_t* kernel_header_blob,
                       const int dev_mode,
                       int* firmware_algorithm,
                       int* kernel_algorithm,
                       int* header_len);

/* Checks the kernel config (analogous to preamble for firmware) signature on
 * kernel config pointed by [kernel_config_blob] using the signing key
 * [kernel_sign_key].
 *
 * On success, put kernel length into [kernel_len], and return 0.
 * Else, return error code on failure.
 */
int VerifyKernelConfig(RSAPublicKey* kernel_sign_key,
                       const uint8_t* kernel_config_blob,
                       int algorithm,
                       uint64_t* kernel_len);

/* Checks the signature on the kernel data at location [kernel_data_start].
 * The length of the actual kernel data is kernel _len and it is assumed to
 * be prepended with the signature whose size depends on the signature_algorithm
 * [algorithm].
 *
 * Return 0 on success, error code on failure.
 */
int VerifyKernelData(RSAPublicKey* kernel_sign_key,
                     const uint8_t* kernel_config_start,
                     const uint8_t* kernel_data_start,
                     uint64_t kernel_len,
                     int algorithm);

/* Performs a chained verify of the kernel blob [kernel_blob]. If
 * [dev_mode] is 0 [inactive], then the pre-processed public signing key
 * [root_key_blob] is used to verify the signature of the signing key,
 * else the check is skipped.
 *
 * TODO(gauravsh): Does the dev mode only effect the R/W firmware verification,
 * or kernel verification, or both?
 *
 * Returns 0 on success, error code on failure.
 *
 * NOTE: The length of the kernel blob is derived from reading the fields
 * in the first few bytes of the buffer. This might look risky but in firmware
 * land, the start address of the kernel_blob will always be fixed depending
 * on the memory map on the particular platform. In addition, the signature on
 * length itself is checked early in the verification process for extra safety.
 */
int VerifyKernel(const uint8_t* signing_key_blob,
                 const uint8_t* kernel_blob,
                 const int dev_mode);

/* Returns the logical version of a kernel blob which is calculated as
 * (kernel_key_version << 16 | kernel_version). */
uint32_t GetLogicalKernelVersion(uint8_t* kernel_blob);

#define  BOOT_KERNEL_A_CONTINUE 1
#define  BOOT_KERNEL_B_CONTINUE 2
#define  BOOT_KERNEL_RECOVERY_CONTINUE 3

/* Contains information about the kernel paritition
 * gleaned from the GPT partition table.
 *
 * Based on the Chromium OS Drive Map design document by
 * rspangler@chromium.org.
 *
*/
typedef struct kernel_entry {
  uint8_t* kernel_blob;  /* Pointer to actual kernel. */
  uint8_t boot_priority;  /* 15 = highest, 1 = lowest, 0 = not bootable. */
  uint8_t boot_tries_remaining;  /* Used when boot_priority = 0. */
  uint8_t boot_success_flag;  /* Set to 1 on successful boot by AU. */
} kernel_entry;

/* This function is the driver used by the RW firmware to
 * determine which copy of the kernel to boot from. It performs
 * the requisite priority and remaining tries checking for a specific
 * kernel partition, does rollback index checking, including updating
 * if required.
 *
 * Returns the code path to follow. It is one of:
 * BOOT_KERNEL_A_CONTINUE          Boot from Kenrel A
 * BOOT_KERNEL_B_CONTINUE          Boot from Kernel B
 * BOOT_KERNEL_RECOVERY_CONTINUE   Jump to recovery mode
 */
int VerifyKernelDriver_f(uint8_t* firmware_key_blob,
                         kernel_entry* kernelA,
                         kernel_entry* kernelB,
                         int dev_mode);

#endif  /* VBOOT_REFERENCE_KERNEL_IMAGE_FW_H_ */