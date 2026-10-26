#include "boot_identity.h"

#include <security/crypto/pka/hw_include/cslr_cp_ace.h>
#include <security/crypto/rng/rng.h>
#include <security/crypto/sa2ul/sa2ul.h>

bool JoshuaBootId(uint32_t* boot) {
  if (!boot) return false;
  uint32_t entropy[4];
  SA2UL_engineEnable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
  RNG_Handle rng = RNG_open(0);
  if (!rng) {
    SA2UL_engineDisable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
    return false;
  }
  const bool ok =
      RNG_setup(rng) == RNG_RETURN_SUCCESS && RNG_read(rng, entropy) == RNG_RETURN_SUCCESS;
  RNG_close(rng);
  SA2UL_engineDisable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
  if (ok) *boot = entropy[0] ? entropy[0] : entropy[1];
  return ok && *boot != 0;
}
