// Hardware boot identity shared by the mutually exclusive AM243 artifacts.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Called once at startup, after the generated security drivers are initialized.
// Failure prevents protocol startup; uptime and constant fallbacks are forbidden.
#ifdef __cplusplus
extern "C" {
#endif
bool JoshuaBootId(uint32_t* boot);
#ifdef __cplusplus
}
#endif
